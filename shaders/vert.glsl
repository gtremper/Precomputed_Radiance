# version 120

// Mine is an old machine.  For version 130 or higher, do 
// out vec4 color ;  
// That is certainly more modern

varying vec4 color ; 

void main() {
	gl_TexCoord[0] = gl_MultiTexCoord0 ; 
    gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * gl_Vertex ; 
    color = gl_Color ; 
}

