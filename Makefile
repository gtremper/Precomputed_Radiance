#makefile

#Basic stuff
CC = g++ -g -Wall -O3 -fmessage-length=0
ifeq ($(shell sw_vers 2>/dev/null | grep Mac | awk '{ print $$2}'),Mac)
	CFLAGS = -g -DGL_GLEXT_PROTOTYPES -I./include/ -I./lib/mac -I/usr/X11/include -DOSX
	LDFLAGS = -framework GLUT -framework OpenGL -L./lib/mac/ \
    	-L"/System/Library/Frameworks/OpenGL.framework/Libraries" \
    	-lGL -lGLU -lm -lstdc++ -lGLEW
else
	CFLAGS = -g -DGL_GLEXT_PROTOTYPES -I./include/ -I/usr/X11R6/include -I/sw/include \
					 -I/usr/sww/include -I/usr/sww/pkg/Mesa/include
	LDFLAGS = -L./lib/nix -L/usr/X11R6/lib -L/sw/lib -L/usr/sww/lib \
						-L/usr/sww/bin -L/usr/sww/pkg/Mesa/lib -lglut -lGLU -lGL -lX11 -lGLEW
endif

#Libraries
CCOPTS = -c -I./headers/ -I./glm-0.9.4.2 -fopenmp -I./eigen -I./GL $(CFLAGS)
LDOPTS = -L./lib/mac -lfreeimage -fopenmp $(LDFLAGS) 

#Final Files and Intermediate .o Files
OBJECTS = main.o Scene.o Transform.o Shapes.o KDTree.o Shaders.o
TARGET = raytracer

#------------------------------------------------------
all: raytracer

raytracer: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDOPTS) $(OBJECTS) -o $(TARGET)

main.o: main.cpp Scene.cpp
	$(CC) $(CCOPTS) main.cpp

Scene.o: Scene.cpp 
	$(CC) $(CCOPTS) Scene.cpp

Transform.o: Transform.cpp
	$(CC) $(CCOPTS) Transform.cpp

Shapes.o: Shapes.cpp
	$(CC) $(CCOPTS) Shapes.cpp

#Light.o: Light.cpp
#	$(CC) $(CCOPTS) Light.cpp

KDTree.o: KDTree.cpp
	$(CC) $(CCOPTS) KDTree.cpp

Shaders.o: shaders.cpp
	$(CC) $(CCOPTS) shaders.cpp

default: $(TARGET)

clean:
	rm -f *.o $(TARGET)