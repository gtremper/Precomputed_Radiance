# version 120 

// Mine is an old machine.  For version 130 or higher, do 
// in vec4 color ;  
// That is certainly more modern

varying vec4 color ;
uniform sampler2D tex ;

void main (void) 
{        
	//gl_FragColor = color ;
	gl_FragColor = texture2D(tex, gl_TexCoord[0].st);
}
