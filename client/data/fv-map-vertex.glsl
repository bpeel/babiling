/*
 * Babiling
 *
 * Copyright (C) 2014 Neil Roberts
 *
 * All rights reserved
 */

attribute vec4 position;
attribute vec2 tex_coord_attrib;

uniform mat4 transform;
uniform mat3 normal_transform;

varying vec2 tex_coord;
varying float tint;

void
main()
{
        vec3 normal;

        if (position[3] < 1.0) {
                normal = vec3(0.0, 0.0, 1.0);
        } else {
                float normal_index = position[3] - 128.0;
                float normal_sign = sign(normal_index);

                normal.z = 0.0;

                if (abs(normal_index) > 100.0)
                        normal.xy = vec2(normal_sign, 0.0);
                else
                        normal.xy = vec2(0.0, normal_sign);
        }

        tint = get_lighting_tint(normal_transform, normal);

        gl_Position = transform * vec4(position.xyz, 1.0);
        tex_coord = tex_coord_attrib;
}

