/*
 * Babiling
 *
 * Copyright (C) 2014, 2015 Neil Roberts
 *
 * All rights reserved
 */

attribute vec3 position;
attribute vec2 tex_coord_attrib;
attribute vec3 normal_attrib;

#if defined(HAVE_INSTANCED_ARRAYS) && defined(HAVE_TEXTURE_2D_ARRAY)
attribute mat4 transform;
attribute mat3 normal_transform;
attribute float tex_layer;
varying vec3 tex_coord;
#else
uniform mat4 transform;
uniform mat3 normal_transform;
varying vec2 tex_coord;
#endif

varying float tint;

void
main()
{
        gl_Position = transform * vec4(position, 1.0);

#if defined(HAVE_INSTANCED_ARRAYS) && defined(HAVE_TEXTURE_2D_ARRAY)
        tex_coord = vec3(tex_coord_attrib, tex_layer);
#else
        tex_coord = tex_coord_attrib;
#endif

        tint = get_lighting_tint(normal_transform, normal_attrib);
}

