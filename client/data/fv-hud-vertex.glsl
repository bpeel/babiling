/*
 * Babiling
 *
 * Copyright (C) 2014, 2015 Neil Roberts
 *
 * All rights reserved
 */

attribute vec2 position;
attribute vec2 tex_coord_attrib;

varying vec2 tex_coord;

void
main()
{
        gl_Position = vec4(position, 0.0, 1.0);
        tex_coord = tex_coord_attrib;
}

