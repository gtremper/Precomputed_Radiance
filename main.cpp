/* This is the main file for the Precomputed Radiance viewer */

#include <iostream>
#include <string>
#include <vector>
#include "omp.h"
#include <sstream>
#include <time.h>
#include <GLUT/glut.h>

#include "shaders.h"
#include "lodepng.h"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/epsilon.hpp>

#define BPP 24
#define EPSILON 0.00000001
#define BUFFER_OFFSET(i) (reinterpret_cast<void*>(i))

typedef glm::vec3 vec3;
typedef glm::mat3 mat3;

using namespace std;


/* Paramaters */
unsigned int height;
unsigned int width;
unsigned int env_resolution;
int lastx, lasty; // For mouse motion
float trans_x;
float trans_y;

/* Shaders */
GLuint vertexshader;
GLuint fragmentshader;
GLuint shaderprogram;
GLuint texture;

/*
Light transport matricies for each color channel
The array indexes the columns
*/
vector<float>* red_matrix;
vector<float>* green_matrix;
vector<float>* blue_matrix;

vector<float>* red_env;
vector<float>* green_env;
vector<float>* blue_env;

vector< pair<int,float> > lights;


void haar1d(){
	return;
}

/* Creates the light transport matrix from images in 'folder' */
void build_transport_matrix(char *folder, const int num_files) {
	
	red_matrix = new vector<float>[num_files];
	green_matrix = new vector<float>[num_files];
	blue_matrix = new vector<float>[num_files];
	
	vector<unsigned char> image; //the raw pixels
	
	for (int i=0; i<num_files; i++) {
		char filename[24];
		sprintf(filename, "%s/demo%02d.png", folder, i);
		
		lodepng::decode(image, width, height, filename);

		for(unsigned int j=0; j<image.size(); j+=4) {
			red_matrix[i].push_back(image[j]/255.0f);
			green_matrix[i].push_back(image[j+1]/255.0f);
			blue_matrix[i].push_back(image[j+2]/255.0f);
		}
		image.clear();
	}
}



/* Mouse Functions */
void mouseClick(int button, int state, int x, int y) {
	if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN){
		lastx = x;
		lasty = y;
	}
}

void mouse(int x, int y) {
	//int diffx=x-lastx; 
    int diffy=y-lasty; 
    lastx=x; //set lastx to the current x position
    lasty=y; //set lasty to the current y position

	trans_y -= diffy*0.005f;
	trans_y = min(1.0f,trans_y);
	trans_y = max(0.0f,trans_y);
	lights[0].second = 1.0f-trans_y;
	lights[1].second = trans_y;
	glutPostRedisplay();
}


/* Everything below here is openGL boilerplate */

void reshape(int w, int h){
	width = w;
	height = h;
	glViewport(0, 0, w, h);
	glutPostRedisplay();
}

void keyboard(unsigned char key, int x, int y) {
	switch(key){
		case 'l':
			break;
		case 's':
			cout << "Image saved!" << endl;
			break;
		case 'r':
			glutReshapeWindow(width, height);
			break;
		case 27:  // Escape to quit
			delete [] red_matrix;
			delete [] green_matrix;
			delete [] blue_matrix;
			exit(0);
			break;

	}
	glutPostRedisplay();
}

void specialKey(int key,int x,int y) {
	switch(key) {
		case 100: //left
			break;
		case 101: //up
			break;
		case 102: //right
			break;
		case 103: //down
			break;
	}
	glutPostRedisplay();
}

void init() {
	
	
	width = 680;
	height = 880;
	
	trans_y = 0;
	
	char* temp = "test_data";
	build_transport_matrix(temp,2);
	
	lights.push_back(make_pair(0,1));
	lights.push_back(make_pair(1,0));

	vertexshader = initshaders(GL_VERTEX_SHADER, "shaders/vert.glsl");
	fragmentshader = initshaders(GL_FRAGMENT_SHADER, "shaders/frag.glsl");
	shaderprogram = initprogram(vertexshader, fragmentshader);
    
	glGenTextures(1, &texture);
	glEnable(GL_TEXTURE_2D) ;
	glBindTexture(GL_TEXTURE_2D, texture);
	glActiveTexture(GL_TEXTURE0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(-1,1,-1,1,-1,1);
	glMatrixMode(GL_MODELVIEW);
	glm::mat4 mv = glm::lookAt(glm::vec3(0,0,1),glm::vec3(0,0,0),glm::vec3(0,1,0));
	glLoadMatrixf(&mv[0][0]);
	
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
}

void display(){
	glClear(GL_COLOR_BUFFER_BIT);
	cout << trans_y << endl;
	
	/* initialize pixel vector to set as texture */
	vector<unsigned char> image;
	image.reserve(3*width*height);
	memset(&image[0], 0, 3*width*height);
	
	/*Loop through the chosen lights and combine them with their weight */
	for (unsigned int j=0; j<lights.size(); j++) {
		int ind = lights[j].first;
		float weight = lights[j].second;
		for (unsigned int i=0; i<width*height; i++) {
			image[3*i] += min(red_matrix[ind][i]*weight, 1.0f) * 255.0f;
			image[3*i+1] += min(green_matrix[ind][i]*weight, 1.0f) * 255.0f;
			image[3*i+2] += min(blue_matrix[ind][i]*weight, 1.0f) * 255.0f;
		}
	}
	
	/* Draw to screen */
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width,height,
		0, GL_RGB, GL_UNSIGNED_BYTE, (GLvoid*) &image[0]);
	glBegin(GL_QUADS);
	glTexCoord2d(0, 1); glVertex3d(-1, -1, 0);
	glTexCoord2d(0, 0); glVertex3d(-1, 1, 0);
	glTexCoord2d(1, 0); glVertex3d(1, 1, 0);
	glTexCoord2d(1, 1); glVertex3d(1, -1, 0);
	glEnd();

	glutSwapBuffers();
}


int main(int argc, char* argv[]){
	srand(time(0));
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
	glutCreateWindow("Viewer");
	init();
	display();
	glutDisplayFunc(display);
	glutKeyboardFunc(keyboard);
	glutSpecialFunc(specialKey);
	glutReshapeFunc(reshape);
	glutReshapeWindow(width,height);
	glutMotionFunc(mouse);
	glutMouseFunc(mouseClick);
	glutMainLoop();
	return 0;
}
