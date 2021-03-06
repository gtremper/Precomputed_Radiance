/* This is the main file for the Precomputed Radiance viewer */
/*       Written by Graham Tremper and Gabe Fierro           */

#include <iostream>
#include <string>
#include <vector>
#include <glob.h>
#include "omp.h"
#include <GLUT/glut.h>

#include "shaders.h"
#include "lodepng.h"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/epsilon.hpp>

/* Define this if you want to use haar transform */
#define USEHAAR

typedef glm::vec3 vec3;

using namespace std;

/* Paramaters */
unsigned int height;
unsigned int width;
unsigned int env_resolution;
float max_light;
char* scenefolder = "scenes/tree";
int env_move_rate;
int num_wavelets = 150;
int scene_resolution = 256;

/* used for counting files in directory */
glob_t gl;
size_t numSceneFiles = 0;

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

/* Average Intensity of each picture (after haar) */
vector<float> red_means;
vector<float> green_means;
vector<float> blue_means;
enum {NAIVE, WEIGHTED};
int sort_mode = NAIVE;

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


/* One iteration of 1d haar transform for use in haar2d */
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
        if(num_files > 10000)
          sprintf(filename, "%s/%05d.png", folder, i);
        else if(num_files > 1000)
          sprintf(filename, "%s/%04d.png", folder, i);
        else
          sprintf(filename, "%s/%03d.png", folder, i);
		clog << "Loading file: " << filename << "\r";
		
		unsigned error = lodepng::decode(image, width, height, filename);
		
		if(error) {
			cout << "decoder error " << error
		 	<< ": " << lodepng_error_text(error) << endl;
			exit(1);
		}

		for(unsigned int j=0; j<image.size(); j+=4) {
			red_matrix[i].push_back(image[j]/255.0f);
			green_matrix[i].push_back(image[j+1]/255.0f);
			blue_matrix[i].push_back(image[j+2]/255.0f);
		}
		image.clear();
	}
	
	clog << "\n";
	
	/* Haar transform rows of matrix */
	vector<float> red_row;
	vector<float> green_row;
	vector<float> blue_row;
	for (unsigned int pixel=0; pixel<width*height; pixel++) {
		clog << "Haar transforming row " << pixel << " of " <<width*height<<"\r";
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
	clog << "\nAlmost done...\n";
	
	/* Fine average intensities for weighting*/
	for (unsigned int i=0; i<num_files; i++) {
		float red_total = 0.0f;
		float green_total = 0.0f;
		float blue_total = 0.0f;
		for(unsigned int pixel=0; pixel<width*height; pixel++) {
			red_total += red_matrix[i][pixel];
			green_total += green_matrix[i][pixel];
			blue_total += blue_matrix[i][pixel];
		}
		red_total /= float(width*height);
		green_total /= float(width*height);
		blue_total /= float(width*height);
		
		red_means.push_back(red_total);
		green_means.push_back(green_total);
		blue_means.push_back(blue_total);
	}
}

void build_environment_vector(char *folder) {
	red_env.clear();
	green_env.clear();
	blue_env.clear();
	max_light = 0;
	
	unsigned int NUM_FACES = 6;
	unsigned int resolution = scene_resolution;
	
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

bool naive_sort(pair<int,float> i, pair<int,float> j){
	return (i.second>j.second);
}

/* One sort function for each color channel */
bool red_weighted_sort(pair<int,float> i, pair<int,float> j){
	float comp1 = i.second * red_means[i.first];
	float comp2 = j.second * red_means[j.first];
	return comp1>comp2;
}
bool green_weighted_sort(pair<int,float> i, pair<int,float> j){
	float comp1 = i.second * green_means[i.first];
	float comp2 = j.second * green_means[j.first];
	return comp1>comp2;
}
bool blue_weighted_sort(pair<int,float> i, pair<int,float> j){
	float comp1 = i.second * blue_means[i.first];
	float comp2 = j.second * blue_means[j.first];
	return comp1>comp2;
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
	for (unsigned int i=0; i<red_env.size(); i++) {
		red_lights.push_back( make_pair(i, red_haar[i]) );
		green_lights.push_back( make_pair(i, green_haar[i]) );
		blue_lights.push_back( make_pair(i, blue_haar[i]) );
	}
	
	if (sort_mode == NAIVE) {
		sort(red_lights.begin(),red_lights.end(), naive_sort);
		sort(green_lights.begin(),green_lights.end(), naive_sort);
		sort(blue_lights.begin(),blue_lights.end(), naive_sort);
	} else {
		sort(red_lights.begin(),red_lights.end(), red_weighted_sort);
		sort(green_lights.begin(),green_lights.end(), green_weighted_sort);
		sort(blue_lights.begin(),blue_lights.end(), blue_weighted_sort);
	}
	
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


/* Everything below here is openGL boilerplate */

void reshape(int w, int h){
	glViewport(0, 0, w, h);
	glutPostRedisplay();
}

void print_help() {
	cout << "\n*******************************************************************\n";
	cout << "Welcome to Graham and Gabe's Precomputed Relighter!\n";
	cout << "Press the left and right arrow to move the environment map\n";
	cout << "Press the up and down arrow to change the environment map move rate\n";
	cout << "Press 'a' for the Grace Cathedral environment map\n";
	cout << "Press 's' for the Eucalyptus Grove environment map\n";
	cout << "Press 'd' for the Beach environment map\n";
	cout << "Press 'f' for the area light environment map\n";
	cout << "Press 'w' to change the sorting function for important wavelets\n";
	cout << "Press 'o' to use less wavelets per frame\n";
	cout << "Press 'p' to use more wavelets per frame\n";
	cout << "Press 'esc' to exit the program\n";
	cout << "Press 'h' to see this help again!\n";
	cout << "*******************************************************************\n";
	cout << endl;
}

void keyboard(unsigned char key, int x, int y) {
	char *filename;
	switch(key){
		case 'w':
            sort_mode = sort_mode == NAIVE ? WEIGHTED : NAIVE;
			if (sort_mode==NAIVE){
				cout << "Now using Naive sort (Sorted by wavelet coefficients)" << endl;
			} else {
				cout << "Now using transport-weighted sorting" << endl;
			}
			break;
		case 'a':
			filename = "Grace";
			cout << "Environment Map: Grace Cathedral" << endl;
			build_environment_vector(filename);
			break;
		case 's':
			filename = "Grove";
			cout << "Environment Map: Eucalyptus Grove" << endl;
			build_environment_vector(filename);
			break;
		case 'd':
			filename = "Beach";
			cout << "Environment Map: Beach" << endl;
			build_environment_vector(filename);
			break;
        case 'f':
            filename = "AreaLight";
			cout << "Environment Map: Area Light" << endl;
			build_environment_vector(filename);
			break;
		case 'o':
			num_wavelets -= 10;
			num_wavelets = max(num_wavelets, 10);
			cout << "Now using " << num_wavelets << " wavelets per frame" << endl;
			break;
		case 'p':
			num_wavelets += 10;
			num_wavelets = min(num_wavelets, (int)red_means.size());
			cout << "Now using " << num_wavelets << " wavelets per frame" << endl;
			break;
		case 'h':
			print_help();
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
			shift_env_map(-env_move_rate);
			break;
		case 101: //up
			env_move_rate = min(env_move_rate+1, (int)env_resolution);
			cout << "Environment shift rate is " << env_move_rate << endl;
			break;
		case 102: //right
			shift_env_map(env_move_rate);
			break;
		case 103: //down
			env_move_rate = max(env_move_rate-1, 1);
			cout << "Environment shift rate is " << env_move_rate << endl;
			break;
	}
	glutPostRedisplay();
}

void init() {
	width = scene_resolution;
	height = scene_resolution;
	max_light = 0;
	env_move_rate = 1;

    /** calculate env_resolution based on number of files in scenefolder */
    string pngs = string(scenefolder) + "/*.png";
    if(glob(pngs.c_str(), GLOB_NOSORT, NULL, &gl) == 0)
        numSceneFiles = gl.gl_pathc;
    globfree(&gl);

    num_wavelets = min(num_wavelets, (int)numSceneFiles);
	
    env_resolution = sqrt(numSceneFiles / 6.0);
	
	build_transport_matrix(scenefolder, numSceneFiles);
	char* temp = "Grace";
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
	
	print_help();
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
	
	/* Calculate weights for 'lights' vector */
	calculate_lights_used();
	
	/* initialize pixel vector to set as texture */
	vector<float> pre_image;
	pre_image.resize(3*width*height, 0);
	
	/* Loop through the chosen lights and combine them with their weight */
	
	for (unsigned int j=0; j<num_wavelets; j++) {
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
	
    float light_normal;
    if(max_light < 1.0f)
        light_normal = 255.0f;
    else
	    light_normal = (1.0f/max_light) * 255.0f;
	
	for (unsigned int i=0; i<width*height; i++) {
		image.push_back( max(0.0f, pre_image[3*i] * light_normal));
		image.push_back( max(0.0f, pre_image[3*i+1] * light_normal));
		image.push_back( max(0.0f, pre_image[3*i+2] * light_normal));
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

void
parse_command_line(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if(strcmp(argv[i],"-r") == 0) {
            scene_resolution = atoi(argv[i+1]);
            i++;
        } else if (strcmp(argv[i],"-f") == 0) {
            scenefolder = argv[i+1];
            i++;
        } else if (strcmp(argv[i],"-h") == 0) {
            cout << "Command Line Options:" << endl;
            cout << "-f [path/to/scene/folder]" << endl;
            cout << "   Defaults to povray/tree_16x16/sharp_tree_images" << endl;
            cout << "-r [resolution]" << endl;
            cout << "   Resolution for the scene images. Defaults to 256" << endl;
            exit(0);
        }
    }
}


int main(int argc, char* argv[]){
    parse_command_line(argc, argv);
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
	glutMainLoop();
	return 0;
}
