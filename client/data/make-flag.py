#!/usr/bin/python3

# Babiling
#
# Copyright (C) 2015 Neil Roberts
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

from PIL import Image
import sys

IMAGE_WIDTH = 64
IMAGE_HEIGHT = 32
LEFT_MARGIN = 4

image_in = Image.open(sys.argv[1])
scaled_image = image_in.resize((IMAGE_WIDTH - LEFT_MARGIN,
                                IMAGE_HEIGHT),
                               Image.BICUBIC)

image_out = Image.new('RGB', (IMAGE_WIDTH, IMAGE_HEIGHT))
image_out.paste(scaled_image, (LEFT_MARGIN, 0))

# Copy the left edge of the image to half of the margin so that when
# the mipmap is generated it will bleed less of the metal colour
left_edge = image_out.crop((LEFT_MARGIN, 0, LEFT_MARGIN + 1, IMAGE_HEIGHT))
for i in range(LEFT_MARGIN // 2, LEFT_MARGIN):
    image_out.paste(left_edge, (i, 0))

# Fill the rest of the margin with a metallic colour which will be
# used as the flag post.
image_out.paste((0xa2, 0xa2, 0xa2), (0, 0, LEFT_MARGIN // 2, IMAGE_HEIGHT))

image_out.save(sys.argv[2])
