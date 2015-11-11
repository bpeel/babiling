/*
 * Babiling
 *
 * Copyright (C) 2014, 2015 Neil Roberts
 *
 * All rights reserved
 */

attribute vec3 position;
attribute vec3 color_attrib;
attribute vec3 normal_attrib;

#ifdef HAVE_INSTANCED_ARRAYS
attribute mat4 transform;
attribute mat3 normal_transform;
#else /* HAVE_INSTANCED_ARRAYS */
uniform mat4 transform;
uniform mat3 normal_transform;
#endif

varying vec3 color;

void
main()
{
        gl_Position = transform * vec4(position, 1.0);
        color = color_attrib * get_lighting_tint(normal_transform,
                                                 normal_attrib);
}

