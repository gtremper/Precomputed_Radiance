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
CCOPTS = -c -I./glm-0.9.4.2 -fopenmp -I./GL $(CFLAGS)
LDOPTS = -L./lib/mac -lfreeimage -fopenmp $(LDFLAGS) 

#Final Files and Intermediate .o Files
OBJECTS = main.o shaders.o lodepng.o
TARGET = viewer

#------------------------------------------------------
all: viewer

viewer: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDOPTS) $(OBJECTS) -o $(TARGET)

main.o: main.cpp
	$(CC) $(CCOPTS) main.cpp

shaders.o: shaders.cpp
	$(CC) $(CCOPTS) shaders.cpp

lodepng.o: lodepng.cpp
	$(CC) $(CCOPTS) lodepng.cpp

default: $(TARGET)

clean:
	rm -f *.o $(TARGET)