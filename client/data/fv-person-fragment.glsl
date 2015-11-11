/*
 * Babiling
 *
 * Copyright (C) 2014, 2015 Neil Roberts
 *
 * All rights reserved
 */

#if defined(HAVE_INSTANCED_ARRAYS) && defined(HAVE_TEXTURE_2D_ARRAY)
varying vec3 tex_coord;
uniform sampler2DArray tex;
#define textureFunc texture2DArray
#else
varying vec2 tex_coord;
uniform sampler2D tex;
#define textureFunc texture2D
#endif

varying float tint;

void
main()
{
        gl_FragColor = vec4(textureFunc(tex, tex_coord).rgb * tint, 1.0);
}
