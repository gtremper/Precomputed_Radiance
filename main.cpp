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

/* Define this if you want to use haar transform */
#define USEHAAR

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

/*
Environment map for lighting
This can also just be an area light
*/
vector<float> red_env;
vector<float> green_env;
vector<float> blue_env;

vector< pair<int,float> > red_lights;
vector< pair<int,float> > green_lights;
vector< pair<int,float> > blue_lights;

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
	
	/* Load files into matrix */
	for (int i=0; i<num_files; i++) {
		char filename[50];
		sprintf(filename, "%s/%03d.png", folder, i);
		cout << filename << endl;
		
		unsigned error = lodepng::decode(image, width, height, filename);
		
		if(error) std::cout << "decoder error " << error
		 	<< ": " << lodepng_error_text(error) << std::endl;

		for(unsigned int j=0; j<image.size(); j+=4) {
			red_matrix[i].push_back(image[j]/255.0f);
			green_matrix[i].push_back(image[j+1]/255.0f);
			blue_matrix[i].push_back(image[j+2]/255.0f);
		}
		image.clear();
	}
	
	/* Haar transform rows of matrix */
	vector<float> red_row;
	vector<float> green_row;
	vector<float> blue_row;
	for (unsigned int pixel=0; pixel<width*height; pixel++) {
		for (int i=0; i<num_files; i++) {
			red_row.push_back(red_matrix[i][pixel]);
			green_row.push_back(green_matrix[i][pixel]);
			blue_row.push_back(blue_matrix[i][pixel]);
		}
		
		#ifdef USEHAAR
		haar2d(red_row);
		haar2d(green_row);
		haar2d(blue_row);
		#endif
		
		for (int i=0; i<num_files; i++) {
			red_matrix[i][pixel] = red_row[i];
			green_matrix[i][pixel] = green_row[i];
			blue_matrix[i][pixel] = blue_row[i];
		}
		red_row.clear();
		green_row.clear();
		blue_row.clear();
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
		
		/*Insert this side of cubemap into envirornmap vector */
		red_env.insert(red_env.end(),red_env_face.begin(),red_env_face.end());
		green_env.insert(green_env.end(),green_env_face.begin(),green_env_face.end());
		blue_env.insert(blue_env.end(),blue_env_face.begin(),blue_env_face.end());
		red_env_face.clear();
		green_env_face.clear();
		blue_env_face.clear();
	}
}

/* Calculate values for light vector */
void calculate_lights_used(){
	/* Remove old values */
	red_lights.clear();
	green_lights.clear();
	blue_lights.clear();
	
	/* make copy to use with haar */
	vector<float> red_haar(red_env);
	vector<float> green_haar(green_env);
	vector<float> blue_haar(blue_env);
	
	#ifdef USEHAAR
	haar2d(red_haar);
	haar2d(green_haar);
	haar2d(blue_haar);
	#endif
	
	/* Create new lights vectors. This just uses all of them right now */
	int count = 0;
	for (unsigned int i=0; i<red_env.size(); i++) {
		if (red_haar[i]>EPSILON) {
			red_lights.push_back( make_pair(i, red_haar[i]) );
			count++;
		}
		if (green_haar[i]>EPSILON) {
			green_lights.push_back( make_pair(i, green_haar[i]) );
			count++;
		}
		if (blue_haar[i]>EPSILON) {
			blue_lights.push_back( make_pair(i, blue_haar[i]) );
			count++;
		}
	}
	cout <<"NUMZERO: " << red_env.size()*3 - count << endl;
	
}

/* Shift environment map to provide dynamic lighting */
/* num is the number of rows to shift the picture over */
void shift_env_map(int num) {
	int amount = env_resolution * num;
	if (amount>0) {
		rotate(red_env.begin(), red_env.begin() + amount, red_env.end());
		rotate(green_env.begin(), green_env.begin() + amount, green_env.end());
		rotate(blue_env.begin(), blue_env.begin() + amount, blue_env.end());
	} else {
		rotate(red_env.begin(), red_env.end() + amount, red_env.end());
		rotate(green_env.begin(), green_env.end() + amount, green_env.end());
		rotate(blue_env.begin(), blue_env.end() + amount, blue_env.end());
	}
}

/* Thise is for moving around an area light */
void shift_area_light(int amount) {
	for (unsigned int i=0; i<red_lights.size(); i++) {
		red_lights[i].first += amount;
		red_lights[i].first = red_lights[i].first % red_env.size();
	}
	for (unsigned int i=0; i<green_lights.size(); i++) {
		green_lights[i].first += (amount+green_env.size()) % (green_env.size());
	}
	for (unsigned int i=0; i<blue_lights.size(); i++) {
		blue_lights[i].first += (amount+blue_env.size()) % (blue_env.size());
	}
	cout << red_lights[0].first << endl;
	cout << green_lights[0].first << endl;
	cout << blue_lights[0].first << endl;
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
	
	red_lights.clear();
	green_lights.clear();
	blue_lights.clear();
	
	red_lights.push_back(make_pair(0,1.0f-trans_y));
	red_lights.push_back(make_pair(1,trans_y));
	green_lights.push_back(make_pair(0,1.0f-trans_y));
	green_lights.push_back(make_pair(1,trans_y));
	blue_lights.push_back(make_pair(0,1.0f-trans_y));
	blue_lights.push_back(make_pair(1,trans_y));
	
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
		case 'w':
			shift_area_light(env_resolution);
			break;
		case 'a':
			shift_area_light(-1);
			break;
		case 's':
			shift_area_light(-env_resolution);
			break;
		case 'd':
			shift_area_light(1);
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
			shift_env_map(-2);
			break;
		case 101: //up
			break;
		case 102: //right
			shift_env_map(2);
			break;
		case 103: //down
			break;
	}
	glutPostRedisplay();
}

void init() {
	width = 256;
	height = 256;
	trans_y = 0;
	
	env_resolution = 16;
	
	char* temp = "tree_images";
	cout << "Building trasport matrix...   ";
	build_transport_matrix(temp,env_resolution*env_resolution*6);
	cout << "done" << endl;
	temp = "Grove";
	build_environment_vector(temp);

	vertexshader = initshaders(GL_VERTEX_SHADER, "shaders/vert.glsl");
	fragmentshader = initshaders(GL_FRAGMENT_SHADER, "shaders/frag.glsl");
	shaderprogram = initprogram(vertexshader, fragmentshader);
    
	glGenTextures(1, &texture);
	glEnable(GL_TEXTURE_2D) ;
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

/* Draws the environment map in the corner of the screen */
void draw_env_map() {
	vector<unsigned char> envmap;
	for (unsigned int i=0; i<red_env.size(); i++) {
		envmap.push_back( red_env[i]* 255.0f);
		envmap.push_back( green_env[i]* 255.0f);
		envmap.push_back( blue_env[i]* 255.0f);
	}
	
	int offset = env_resolution*env_resolution*3;
	
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, env_resolution,env_resolution,
		0, GL_RGB, GL_UNSIGNED_BYTE, (GLvoid*) &envmap[0]);
	glBegin(GL_QUADS);
	glTexCoord2d(0, 1); glVertex3d(.5, -.25, .1);
	glTexCoord2d(0, 0); glVertex3d(.5, 0, .1);
	glTexCoord2d(1, 0); glVertex3d(.75, 0, .1);
	glTexCoord2d(1, 1); glVertex3d(.75, -.25, .1);
	glEnd();
	
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, env_resolution,env_resolution,
		0, GL_RGB, GL_UNSIGNED_BYTE, (GLvoid*) &envmap[offset]);
	glBegin(GL_QUADS);
	glTexCoord2d(0, 1); glVertex3d(.25, -.5, .1);
	glTexCoord2d(0, 0); glVertex3d(.25, -.25, .1);
	glTexCoord2d(1, 0); glVertex3d(.5, -.25, .1);
	glTexCoord2d(1, 1); glVertex3d(.5, -.5, .1);
	glEnd();
	
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, env_resolution,env_resolution,
		0, GL_RGB, GL_UNSIGNED_BYTE, (GLvoid*) &envmap[offset*2]);
	glBegin(GL_QUADS);
	glTexCoord2d(0, 1); glVertex3d(.5, -.5, .1);
	glTexCoord2d(0, 0); glVertex3d(.5, -.25, .1);
	glTexCoord2d(1, 0); glVertex3d(.75, -.25, .1);
	glTexCoord2d(1, 1); glVertex3d(.75, -.5, .1);
	glEnd();
	
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, env_resolution,env_resolution,
		0, GL_RGB, GL_UNSIGNED_BYTE, (GLvoid*) &envmap[offset*3]);
	glBegin(GL_QUADS);
	glTexCoord2d(0, 1); glVertex3d(.75, -.5, .1);
	glTexCoord2d(0, 0); glVertex3d(.75, -.25, .1);
	glTexCoord2d(1, 0); glVertex3d(1, -.25, .1);
	glTexCoord2d(1, 1); glVertex3d(1, -.5, .1);
	glEnd();
	
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, env_resolution,env_resolution,
		0, GL_RGB, GL_UNSIGNED_BYTE, (GLvoid*) &envmap[offset*4]);
	glBegin(GL_QUADS);
	glTexCoord2d(0, 1); glVertex3d(.5, -.75, .1);
	glTexCoord2d(0, 0); glVertex3d(.5, -.5, .1);
	glTexCoord2d(1, 0); glVertex3d(.75, -.5, .1);
	glTexCoord2d(1, 1); glVertex3d(.75, -.75, .1);
	glEnd();
	
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, env_resolution,env_resolution,
		0, GL_RGB, GL_UNSIGNED_BYTE, (GLvoid*) &envmap[offset*5]);
	glBegin(GL_QUADS);
	glTexCoord2d(0, 1); glVertex3d(.5, -1, .1);
	glTexCoord2d(0, 0); glVertex3d(.5, -.75, .1);
	glTexCoord2d(1, 0); glVertex3d(.75, -.75, .1);
	glTexCoord2d(1, 1); glVertex3d(.75, -1, .1);
	glEnd();
}

void display(){
	glClear(GL_COLOR_BUFFER_BIT);
	//cout << trans_y << endl;
	
	/* Calculate weights for 'lights' vector */
	calculate_lights_used();
	
	/* initialize pixel vector to set as texture */
	vector<float> pre_image;
	pre_image.reserve(3*width*height);
	memset(&pre_image[0], 0, 3*width*height*sizeof(float));
	
	
	/* Loop through the chosen lights and combine them with their weight */
	float max_light = 0;
	for (unsigned int j=0; j<red_lights.size(); j++) {
		int r_ind = red_lights[j].first;
		int g_ind = green_lights[j].first;
		int b_ind = blue_lights[j].first;
		
		float r_weight = red_lights[j].second;
		float g_weight = green_lights[j].second;
		float b_weight = blue_lights[j].second;
		
		for (unsigned int i=0; i<width*height; i++) {
			pre_image[3*i] += red_matrix[r_ind][i]*r_weight;
			pre_image[3*i+1] += green_matrix[g_ind][i]*g_weight;
			pre_image[3*i+2] += blue_matrix[b_ind][i]*b_weight;
			max_light = max(pre_image[3*i],max_light);
			max_light = max(pre_image[3*i+1],max_light);
			max_light = max(pre_image[3*i+2],max_light);
		}
	}
	
	vector<unsigned char> image;
	image.reserve(3*width*height);
	
	cout << "MAXLIGHT: " << max_light << endl;
	float light_normal = (1.0f/max_light) * 255.0f;
	
	for (unsigned int i=0; i<width*height; i++) {
		image.push_back( pre_image[3*i] * light_normal);
		image.push_back( pre_image[3*i+1] * light_normal);
		image.push_back( pre_image[3*i+2] * light_normal);
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
	
	/* Draw environment map */
	draw_env_map();
	
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
