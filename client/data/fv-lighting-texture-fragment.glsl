/*
 * Babiling
 *
 * Copyright (C) 2014, 2015 Neil Roberts
 *
 * All rights reserved
 */

varying vec2 tex_coord;
varying float tint;

uniform sampler2D tex;

void
main()
{
        gl_FragColor = vec4(texture2D(tex, tex_coord).rgb * tint, 1.0);
}
