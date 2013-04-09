/* This is the main file for the Precomputed Radiance viewer */

#include <iostream>
#include <cstdio>
#include <stack>
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

/* Shaders */
GLuint vertexshader;
GLuint fragmentshader;
GLuint shaderprogram;
GLuint texture;

vector<unsigned char> red_matrix;
vector<unsigned char> green_matrix;
vector<unsigned char> blue_matrix;

void
print_vector(const vec3& v) {
	cout << v[0] << " " << v[1] << " " << v[2] << endl;
}

void
print_matrix(const mat3& m) {
	cout << m[0][0] << "|" << m[0][1] << "| " << m[0][2] << endl;
	cout << m[1][0] << "|" << m[1][1] << "| " << m[1][2] << endl;
	cout << m[2][0] << "|" << m[2][1] << "| " << m[2][2] << endl;
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
			exit(0);
			break;

	}
	glutPostRedisplay();
}

void init() {
	
	width = 680;
	height = 880;
	
	vector<unsigned char> image; //the raw pixels
	const char* filename = string("demo01.png").c_str();
	unsigned error = lodepng::decode(image, width, height, filename);
	
	//if there's an error, display it
	if(error) cout << "decoder error " << error << ": " << lodepng_error_text(error) << endl;
	
	
	for(unsigned int i=0; i<image.size(); i+=4) {
		red_matrix.push_back(image[i]);
		green_matrix.push_back(image[i+1]);
		blue_matrix.push_back(image[i+2]);
	}
	cout << "DEEERP" << endl;

	vertexshader = initshaders(GL_VERTEX_SHADER, "shaders/vert.glsl");
	fragmentshader = initshaders(GL_FRAGMENT_SHADER, "shaders/frag.glsl");
	shaderprogram = initprogram(vertexshader, fragmentshader);
    
	glGenTextures(1, &texture);
	glEnable(GL_TEXTURE_2D) ;
	glBindTexture(GL_TEXTURE_2D, texture);
	glActiveTexture(GL_TEXTURE0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width,height,
	//	0, GL_RGB, GL_UNSIGNED_BYTE, (GLvoid*) &image[0]);
    
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
	
	cout << "derp" << endl;
	
	vector<unsigned char> image;
	
	for (unsigned int i=0; i<red_matrix.size(); i++){
		image.push_back(red_matrix[i]);
		image.push_back(green_matrix[i]);
		image.push_back(blue_matrix[i]);
	}
	
	
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
	//if(argc < 2) {
	//	cerr << "You need at least 1 scene file as the argument" << endl;
	//	exit(1);
	//}
	srand(time(0));
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
	glutCreateWindow("Viewer");
	init();
	display();
	glutDisplayFunc(display);
	glutKeyboardFunc(keyboard);
	glutReshapeFunc(reshape);
	glutReshapeWindow(width,height);
	glutMainLoop();
	return 0;
}
