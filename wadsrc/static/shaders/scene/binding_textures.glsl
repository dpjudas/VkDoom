
#ifdef USE_LEVELMESH

layout(set = 2, binding = 0) uniform sampler2D textures[];

#define tex 0
#define texture2 1
#define texture3 2
#define texture4 3
#define texture5 4
#define texture6 5
#define texture7 6
#define texture8 7
#define texture9 8
#define texture10 9
#define texture11 10

#else

// textures
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 2, binding = 1) uniform sampler2D texture2;
layout(set = 2, binding = 2) uniform sampler2D texture3;
layout(set = 2, binding = 3) uniform sampler2D texture4;
layout(set = 2, binding = 4) uniform sampler2D texture5;
layout(set = 2, binding = 5) uniform sampler2D texture6;
layout(set = 2, binding = 6) uniform sampler2D texture7;
layout(set = 2, binding = 7) uniform sampler2D texture8;
layout(set = 2, binding = 8) uniform sampler2D texture9;
layout(set = 2, binding = 9) uniform sampler2D texture10;
layout(set = 2, binding = 10) uniform sampler2D texture11;

#endif
