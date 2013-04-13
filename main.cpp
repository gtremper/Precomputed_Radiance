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

int pic;

/*
Light transport matricies for each color channel
The array indexes the columns
*/
vector<float>* red_matrix;
vector<float>* green_matrix;
vector<float>* blue_matrix;

vector<float> red_env;
vector<float> green_env;
vector<float> blue_env;

vector< pair<int,float> > lights;

/*
1d haar transform. Vec must be a power of 2 length
*/
void haar1d(vector<float>::iterator vec, int w, bool is_col){
	float *tmp = new float[w];
	memset(tmp, 0, sizeof(float)*w);
	
	int offset = is_col ? w : 1;
	
	while (w>1) {
		w /= 2;
		for (int i=0; i<w; i++) {
			tmp[i] = (vec[2*i*offset] + vec[(2*i+1)*offset]) / sqrt(2.0);
			tmp[i+w] = (vec[2*i*offset] - vec[(2*i+1)*offset]) / sqrt(2.0);
		}
		for (int i=0; i<2*w; i++) {
			vec[i*offset] = tmp[i];
		}
	}
	delete [] tmp;
}

/* Modified verstion of haar1d for 2d haar transform */
void haar(vector<float>::iterator vec, int w, int res, bool is_col){
	float *tmp = new float[w];
	memset(tmp, 0, sizeof(float)*w);
	
	int offset = is_col ? res : 1;
	
	w /= 2;
	for (int i=0; i<w; i++) {
		tmp[i] = (vec[2*i*offset] + vec[(2*i+1)*offset]) / sqrt(2.0);
		tmp[i+w] = (vec[2*i*offset] - vec[(2*i+1)*offset]) / sqrt(2.0);
	}
	for (int i=0; i<2*w; i++) {
		vec[i*offset] = tmp[i];
	}
	delete [] tmp;
}

/*
2d haar transform on each face of a cubemap
*/
void haar2d(vector<float>& vec){
	
	int resolution = sqrt(vec.size());
	
	int w = resolution;
	
	while (w>1)	{
		vector<float>::iterator row_iter = vec.begin();
		for (int i=0; i<resolution; i++){
			haar(row_iter,w,resolution,false);
			row_iter += resolution;
		}
		vector<float>::iterator col_iter = vec.begin();
		for (int i=0; i<resolution; i++){
			haar(col_iter,w,resolution,true);
			col_iter += 1;
		}
		w /= 2;
	}
}

/* Creates the light transport matrix from images in 'folder' */
void build_transport_matrix(char *folder, const int num_files) {
	
	red_matrix = new vector<float>[num_files];
	green_matrix = new vector<float>[num_files];
	blue_matrix = new vector<float>[num_files];
	
	vector<unsigned char> image; //the raw pixels
	
	for (int i=0; i<num_files; i++) {
		char filename[50];
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

void build_environment_vector(char *folder) {
	unsigned int NUM_FACES = 6;
	unsigned int resolution = 256;
	
	vector<unsigned char> image; //the raw pixels
	
	vector<float> red_env_face;
	vector<float> green_env_face;
	vector<float> blue_env_face;
	
	for (unsigned int i=0; i<NUM_FACES; i++) {
		char filename[50];
		sprintf(filename, "environment_maps/%s/%s%d.png", folder, folder, i);
		
		lodepng::decode(image, resolution, resolution, filename);

		for(unsigned int j=0; j<image.size(); j+=4) {
			red_env_face.push_back(image[j]/255.0f);
			green_env_face.push_back(image[j+1]/255.0f);
			blue_env_face.push_back(image[j+2]/255.0f);
		}
		image.clear();
		
		/* Downsample to desired resolution */
		vector<float> new_red;
		vector<float> new_green;
		vector<float> new_blue;
		while (resolution > env_resolution) {
			resolution /= 2;
			for (unsigned int y=0; y<resolution; y++) {
				for (unsigned int x=0; x<resolution; x++){
					int p0 = 2*x + 4*y*resolution;
					int p1 = p0 + 1;
					int p2 = p0 + 2*resolution;
					int p3 = p0 + 2*resolution + 1;

					float ave;
					ave = red_env_face[p0] + red_env_face[p1] + red_env_face[p2] + red_env_face[p3];
					ave /= 4.0f;
					new_red.push_back(ave);
					ave = green_env_face[p0] + green_env_face[p1] + green_env_face[p2] + green_env_face[p3];
					ave /= 4.0f;
					new_green.push_back(ave);
					ave = blue_env_face[p0] + blue_env_face[p1] + blue_env_face[p2] + blue_env_face[p3];
					ave /= 4.0f;
					new_blue.push_back(ave);
				}
			}
			red_env_face = new_red;
			green_env_face = new_green;
			blue_env_face = new_blue;
			new_red.clear();
			new_green.clear();
			new_blue.clear();	
		}
		red_env.insert(red_env.end(),red_env_face.begin(),red_env_face.end());
		green_env.insert(green_env.end(),green_env_face.begin(),green_env_face.end());
		blue_env.insert(blue_env.end(),blue_env_face.begin(),blue_env_face.end());
		red_env_face.clear();
		green_env_face.clear();
		blue_env_face.clear();
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
	//width = w;
	//height = h;
	glViewport(0, 0, w, h);
	glutPostRedisplay();
}

void keyboard(unsigned char key, int x, int y) {
	switch(key){
		case 'l':
			break;
		case 's':
			pic++;
			if (pic==6) pic=0;
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
	pic = 0;
	
	env_resolution = 128;
	
	char* temp = "test_data";
	build_transport_matrix(temp,2);
	temp = "Grace";
	build_environment_vector(temp);
	
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
	
	/*
	char* filename ="environment_maps/Grace/grace_cross2.png";
	vector<unsigned char> image; //the raw pixels
	lodepng::decode(image, width, height, filename);
	
	vector<float> red;
	vector<float> green;
	vector<float> blue;
	
	for(unsigned int j=0; j<image.size(); j+=4) {
		red.push_back(image[j]/255.0f);
		//green.push_back(image[j+1]/255.0f);
		//blue.push_back(image[j+2]/255.0f);
		green.push_back(0.0f);
		blue.push_back(0.0f);
	}
	image.clear();
	
	haar2d(red);
	//haar2d(green);
	//haar2d(blue);
	
	for (unsigned int i=0; i<width*height; i++) {
		image[3*i] += min(red_env[i], 1.0f) * 255.0f;
		image[3*i+1] += min(green_env[i], 1.0f) * 255.0f;
		image[3*i+2] += min(blue_env[i], 1.0f) * 255.0f;
	}
	*/
	
	
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
