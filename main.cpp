/* This is the main file for the raytracer */

#include <iostream>
#include <cstdio>
#include <stack>
#include <string>
#include <vector>
#include "omp.h"
#include <sstream>
#include <time.h>
#include <GLUT/glut.h>

#include "FreeImage.h"
#include "shaders.h"
#include "Shapes.h"
#include "Intersection.h"
#include "Light.h"
#include "Scene.h"

#define BPP 24
#define EPSILON 0.0000001
#define BUFFER_OFFSET(i) (reinterpret_cast<void*>(i))

using namespace std;

/* Paramaters */
double rays_cast;
Scene* scene;
int height;
int width;
int numFrames = 0;

/* Shaders */
GLuint vertexshader;
GLuint fragmentshader;
GLuint shaderprogram;
GLuint texture;
FIBITMAP* bitmap;
vec3 * pixels;
vec3 * direct_pixels;
int update;

const vec3 X = vec3(1,0,0);
const vec3 Y = vec3(0,1,0);
const vec3 Z = vec3(0,0,1);


inline double average(vec3& v){
	return (v[0] + v[1] + v[2]) / 3.0;
}

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

/** rotate the Z vector in the direction of norm */
mat3 rotate_axis(const vec3& sample, const vec3& reflected_dir) {
  double u1 = ((double)rand()/(double)RAND_MAX);
  double u2 = ((double)rand()/(double)RAND_MAX);
  double u3 = ((double)rand()/(double)RAND_MAX);
  vec3 randompoint = vec3(u1, u2, u3);
  vec3 u = glm::normalize(glm::cross(randompoint , reflected_dir));
  vec3 v = glm::normalize(glm::cross(reflected_dir , u));
  mat3 rot = mat3(u, v, reflected_dir);
  rot = glm::transpose(rot);
  return rot;
}

mat3 rotate_around_axis(vec3& rotation_axis, double theta, vec3& vec_in) {
    rotation_axis = glm::normalize(rotation_axis);
    double common_factor = sin(theta*0.5);
    double a = cos(theta*0.5);
    double b = rotation_axis[0] * common_factor;
    double c = rotation_axis[1] * common_factor;
    double d = rotation_axis[2] * common_factor;

    mat3 m = mat3(a*a + b*b - c*c -d*d, 2*(b*c-a*d), 2*(b*d+a*c),
                  2*(b*c+a*d), a*a-b*b+c*c-d*d, 2*(c*d-a*b),
                  2*(b*d-a*c), 2*(c*d+a*b), a*a-b*b-c*d+d*d);
    return m;

}

/* Sample a hemisphere f(r diffuse ray */
vec3 cos_weighted_hem(vec3& norm){
	double u1 = ((double)rand()/(double)RAND_MAX);
	double u2 = ((double)rand()/(double)RAND_MAX);

    vec3 y = vec3(norm);
    vec3 h = vec3(norm);
    double theta = acos(sqrt(1.0 - u1));
    double phi = 2.0 * M_PI * u2;
    double xs = sin(theta) * cos(phi);
    double ys = cos(theta);
    double zs = sin(theta) * sin(phi);
    if ((abs(h[0]) <= abs(h[1])) && (abs(h[0]) <= abs(h[2])))
        h[0] = 1.0;
    else if ((abs(h[1]) <= abs(h[0])) && (abs(h[1]) <= abs(h[2])))
        h[1] = 1.0;
    else
        h[2] = 1.0;
    vec3 x = glm::cross(h,y);
    vec3 z = glm::cross(x,y);

      vec3 direction = xs * x + ys * y + zs * z;
    return direction;
}

/* Sample a hemispehre for specular ray */
vec3 specular_weighted_hem(vec3& reflection, const vec3& norm, double n){
	double u1 = ((double)rand()/(double)RAND_MAX);
	double u2 = ((double)rand()/(double)RAND_MAX);
    vec3 y = vec3(norm);
    vec3 h = vec3(norm);
    double alpha = acos(pow(u1, 1.0/(n+1.0)));
    double phi = 2.0 * M_PI * u2;
    double xs = sin(alpha) * cos(phi);
    double ys = cos(alpha);
    double zs = sin(alpha) * sin(phi);
    if ((abs(h[0]) <= abs(h[1])) && (abs(h[0]) <= abs(h[2])))
        h[0] = 1.0;
    else if ((abs(h[1]) <= abs(h[0])) && (abs(h[1]) <= abs(h[2])))
        h[1] = 1.0;
    else
        h[2] = 1.0;
    vec3 x = glm::cross(h,y);
    vec3 z = glm::cross(x,y);

      vec3 direction = xs * x + ys * y + zs * z;
    return direction;
}

vec3 uniform_sample_hem(vec3& norm) {
	double u1 = ((double)rand()/(double)RAND_MAX);
	double u2 = ((double)rand()/(double)RAND_MAX);
    double phi = 2.0* M_PI * u2;
    double r = sqrt(1.0 - (u1 * u1));
    vec3 dir = vec3(cos(phi) * r, sin(phi) * r, u1);
    if (glm::dot(dir, norm) < 0)
      return -dir;
    return dir;
    //dir = rotate_axis(dir, norm);
    //return dir;
}

/* Shoot ray at scene */
vec3 findColor(Scene* scene, Ray& ray, double weight) {

	/* Intersect scene */
	Intersection hit = scene->KDTree->intersect(ray);
	if(!hit.primative) {
		return vec3(0,0,0); //background color
	}
	
	if( average(hit.primative->emission) > EPSILON ){
		return hit.primative->emission;
	}

	/* Russian Roulette */
	double russian = 1.0;
	const double cutoff = 0.25;
	if (weight < 0.001) {
		return vec3(0,0,0);
		double u1 = ((double)rand()/(double)RAND_MAX);
		if (u1 > cutoff) {
			return vec3(0,0,0);
		} else {
			russian = 1.0/cutoff;
		}
	}
	
	vec3 color = vec3(0,0,0);
	/*********************************************
	Add direct lighting contribution
	*********************************************/
	if (weight < 1.0 && weight > 0.01){
		int numLights = scene->lights.size();
		color += scene->lights[rand() % numLights]->shade(hit, scene->KDTree, true);
	}
	weight *= 0.95; // make sure it doesnt go forever


	/*********************************************
	Importance sample global illumination
	*********************************************/
	double diffWeight = average(hit.primative->diffuse);
	double specWeight = average(hit.primative->specular);
	double threshold = diffWeight / (diffWeight + specWeight);
	double u1 = ((double)rand()/(double)RAND_MAX);

	/* Importance sample on macro level to choose diffuse or specular */
	vec3 normal = hit.primative->getNormal(hit.point);
    Ray newRay;
	if (u1 < threshold) {
		vec3 newDirection = cos_weighted_hem(normal);
		newRay = Ray(hit.point+EPSILON*normal, newDirection);
		double prob = 1.0/threshold;
		color += prob * hit.primative->diffuse * findColor(scene, newRay, weight*diffWeight);
	} else {
		vec3 reflect = glm::reflect(ray.direction, normal);
		//newRay = Ray(hit.point+EPSILON*normal, reflect);
		//color += findColor(scene, newRay, specWeight*weight);

		vec3 newDirection = specular_weighted_hem(reflect, normal, hit.primative->shininess);
		//vec3 newDirection = uniform_sample_hem(normal);
		newRay = Ray(hit.point+EPSILON*normal, newDirection);

		double dot = glm::dot(normal, newDirection);
		if (dot < 0.0) {
			return vec3(0,0,0);
		}

		double n = hit.primative->shininess;

		double multiplier = (n + 2.0) / (n + 1.0);
		multiplier *= 1.0/(1.0-threshold);
		multiplier *= dot;
		color +=  multiplier * hit.primative->specular * findColor(scene, newRay, specWeight*weight);

	}

	return russian*color;
}

/* Main raytracing function. Shoots ray for each pixel with anialiasing */
/* ouputs bitmap to global variable*/
void raytrace(double rayscast) {
	double subdivisions = scene->antialias;
	double subdivide = 1.0/subdivisions;

	double old_weight = rayscast/(rayscast+1.0);
	double new_weight = 1.0 - old_weight;

	#pragma omp parallel for
	for (int j=0; j<scene->height; j++){
		int tid = omp_get_thread_num();
		if(tid == 0) {
		   clog << "Progress: "<< (j*100*omp_get_num_threads())/scene->height <<"%"<<"\r";
		}
		RGBQUAD rgb;
		for (int i=0; i<scene->width; i++) {
			vec3 color;
			for(double a=0; a<subdivisions; a+=1) {
				for(double b=0; b<subdivisions; b+=1) {
					double randomNum1 = ((double)rand()/(double)RAND_MAX) * subdivide;
					double randomNum2 = ((double)rand()/(double)RAND_MAX) * subdivide;
					Ray ray = scene->castEyeRay(i+(a*subdivide) + randomNum1,j+(b*subdivide)+randomNum2);
					color += findColor(scene, ray, 1.0);
				}
			}
			color *= (subdivide * subdivide);
			
			color[0] = min(color[0],1.0);
			color[1] = min(color[1],1.0);
			color[2] = min(color[2],1.0);
			
			pixels[i + scene->width*j] *= old_weight;
			pixels[i + scene->width*j] += new_weight*color;
			color = pixels[i + scene->width*j] + direct_pixels[i + scene->width*j];
			rgb.rgbRed = min(color[0],1.0)*255.0;
			rgb.rgbGreen = min(color[1],1.0)*255.0;
			rgb.rgbBlue = min(color[2],1.0)*255.0;
			FreeImage_SetPixelColor(bitmap,i,j,&rgb);
		}
	}
}

/* Only the direct lighting */
vec3 direct_lighting(Scene* scene, Ray& ray){

	/* Intersect scene */
	Intersection hit = scene->KDTree->intersect(ray);

	if(!hit.primative) {
		return vec3(0,0,0); //background color
	}
	if( glm::length(hit.primative->emission) > EPSILON ){
		return hit.primative->emission;
	}

	vec3 color = vec3(0,0,0);
	for (unsigned int i = 0; i < scene->lights.size(); i++) {
		color += scene->lights[i]->shade(hit, scene->KDTree, false);
	}
	return color;
}


/* Calculate the direct lighting seperatly */
void direct_raytrace() {
	double subdivisions = scene->antialias;
	double subdivide = 1.0/subdivisions;

	#pragma omp parallel for
	for (int j=0; j<scene->height; j++){
		int tid = omp_get_thread_num();
		if(tid == 0) {
		   clog << "Direct Lighting: "<< (j*100*omp_get_num_threads())/scene->height <<"%"<<"\r";
		}
		RGBQUAD rgb;
		for (int i=0; i<scene->width; i++) {
			vec3 color;
			for(double a=0; a<subdivisions; a+=1) {
				for(double b=0; b<subdivisions; b+=1) {
					double randomNum1 = ((double)rand()/(double)RAND_MAX) * subdivide;
					double randomNum2 = ((double)rand()/(double)RAND_MAX) * subdivide;
					Ray ray = scene->castEyeRay(i+(a*subdivide) + randomNum1,j+(b*subdivide)+randomNum2);
					color += direct_lighting(scene, ray);
				}
			}
			color *= (subdivide * subdivide);
			color[0] = min(color[0],1.0);
			color[1] = min(color[1],1.0);
			color[2] = min(color[2],1.0);
			
			
			direct_pixels[i + scene->width*j] = color;
			rgb.rgbRed = min(color[0],1.0)*255.0;
			rgb.rgbGreen = min(color[1],1.0)*255.0;
			rgb.rgbBlue = min(color[2],1.0)*255.0;
			FreeImage_SetPixelColor(bitmap,i,j,&rgb);
		}
	}
	clog << "\n	done\n";
}

/* Everything below here is openGL boilerplate */

void reshape(int w, int h){
	width = w;
	height = h;
	glViewport(0, 0, w, h);
	glutPostRedisplay();
}

void keyboard(unsigned char key, int x, int y) {
	stringstream ss;
	switch(key){
		case 'l':
			update = 1;
			break;
		case 's':
			ss << scene->filename<<"_"<< int(rays_cast) << ".png";
			FreeImage_Save(FIF_PNG, bitmap, ss.str().c_str(), 0);
			cout << "Image saved!" << endl;
			break;
		case 'f':
			update = 10000;
			break;
		case 'h':
			update = 0;
			break;
		case 'r':
			glutReshapeWindow(scene->width,scene->height);
			break;
		case 27:  // Escape to quit
			FreeImage_DeInitialise();
			delete scene;
			delete pixels;
			delete direct_pixels;
			exit(0);
			break;

	}
	glutPostRedisplay();
}

void init(char* filename) {
	scene = new Scene(filename);
	rays_cast = 0.0;
	update = numFrames ? numFrames : false;
	width = scene->width;
	height = scene->height;
	pixels = new vec3[width*height];
	direct_pixels = new vec3[width*height];
	memset(pixels, 0, sizeof(vec3)*width*height);
	memset(direct_pixels, 0, sizeof(vec3)*width*height);

	FreeImage_Initialise();
	bitmap = FreeImage_Allocate(width, height, BPP);

    if (!numFrames) {
        vertexshader = initshaders(GL_VERTEX_SHADER, "shaders/vert.glsl");
        fragmentshader = initshaders(GL_FRAGMENT_SHADER, "shaders/frag.glsl");
        shaderprogram = initprogram(vertexshader, fragmentshader);

        glGenTextures(1, &texture);
        glEnable(GL_TEXTURE_2D) ;
        glBindTexture(GL_TEXTURE_2D, texture);
        glActiveTexture(GL_TEXTURE0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        BYTE* bits = FreeImage_GetBits(bitmap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, scene->width, scene->height,
            0, GL_BGR, GL_UNSIGNED_BYTE, (GLvoid*)bits);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-1,1,-1,1,-1,1);
        glMatrixMode(GL_MODELVIEW);
        glm::mat4 mv = glm::lookAt(glm::vec3(0,0,1),glm::vec3(0,0,0),glm::vec3(0,1,0));
        glLoadMatrixf(&mv[0][0]);
    }

}

void display(){
    if (!numFrames)
      glClear(GL_COLOR_BUFFER_BIT);

    int repetitions = numFrames;
	if (update){
        do {
          cout << "Iterations left: " << update << endl;
          time_t seconds = time(NULL);
          if (!rays_cast) {
              direct_raytrace();
          }
          raytrace(rays_cast);
          BYTE* bits = FreeImage_GetBits(bitmap);
          if (!numFrames)
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, scene->width, scene->height,
              0, GL_BGR, GL_UNSIGNED_BYTE, (GLvoid*)bits);
          rays_cast += 1.0;
          cout << "Number of Samples: " << rays_cast <<
          "\tTime: " << time(NULL)-seconds <<" seconds" << endl;
          update -= 1;
          if (!numFrames) glutPostRedisplay();
          else repetitions -= 1;
        } while (repetitions);
	}
    if (numFrames && !update) {
        stringstream saved;
        saved << scene->filename<<"_"<< int(rays_cast) << ".png";
        FreeImage_Save(FIF_PNG, bitmap, saved.str().c_str(), 0);
        cout << "Image saved to " << saved.str() << "!" << endl;
        exit(0);
    }

	glBegin(GL_QUADS);
	glTexCoord2d(0, 0); glVertex3d(-1, -1, 0);
	glTexCoord2d(0, 1); glVertex3d(-1, 1, 0);
	glTexCoord2d(1, 1); glVertex3d(1, 1, 0);
	glTexCoord2d(1, 0); glVertex3d(1, -1, 0);
	glEnd();

	glutSwapBuffers();
}

void parse_command_line(int argc, char* argv[]) {
    for (int i = 1; i < argc-1 ; i++) {
      if (strcmp(argv[i],"-f") == 0) {
          numFrames = atoi(argv[i+1]);
          cout << "Running " << numFrames << " times" << endl;
          break;
      }
    }
}

int main(int argc, char* argv[]){
	if(argc < 2) {
		cerr << "You need at least 1 scene file as the argument" << endl;
		exit(1);
	}
	srand(time(0));
    parse_command_line(argc, argv);
    if (!numFrames) {
      glutInit(&argc, argv);
      glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
      glutCreateWindow("Path Tracer");
    }
	init(argv[argc-1]);
    if (numFrames)
      display();
	glutDisplayFunc(display);
	glutKeyboardFunc(keyboard);
	glutReshapeFunc(reshape);
	glutReshapeWindow(width,height);
	glutMainLoop();
	return 0;
}
