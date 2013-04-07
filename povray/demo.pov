#include "colors.inc"    // The include files contain
#include "stones.inc"    // pre-defined scene elements

camera {
    location <0, 2, -3>
    look_at <0, 1, 2>
}

//sphere {
//    <0, 1, 2> 2
//    texture {
//        pigment { color Yellow }
//    }
//}

box {
  <-1, 0, -1> // near left lower corner
  <1, 0.5, 3> // far upper right corner
  texture {
    T_Stone25 // pre-defined from "stones.inc"
    scale 4 // scale 4x in all directions
  }
  rotate y*20 // rotate <0, 20, 0>
}

cone {
  <0, 1, 0>, 0.3 // center and radius of one end
  <1, 2, 3>, 1.0 // center and radius of other end
  open // no end caps
  texture { T_Stone25 scale 4 }
}

plane { 
  <0, 1, 0>, -1 //surface normal, displacement
  //alternative: y, -1
  pigment {
    checker color Red, color Blue
  }
}

light_source { 
  <2 * cos(360*clock), 4, 2*sin(360*clock)>
  color White
}
