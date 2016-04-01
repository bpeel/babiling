#!/usr/bin/env python3

import cairo
import subprocess
import os
import os.path
import sys
import re
import math

from glob import glob

TEX_WIDTH = 1024
TEX_HEIGHT = 1024

# All of the flags will be centered into a rectangle that has this
# ratio regardless of their actual ratio. Any excess space will be
# made transparent.
FLAG_RATIO_X = 12
FLAG_RATIO_Y = 8
PADDING_RATIO_X = 3
PADDING_RATIO_Y = 2
BORDER_RATIO = 4

def calculate_units(n_columns, n_images):
    # Each flag has padding on either side to avoid filtering in
    # pixels from the neighboring flag, except the flags at the edges
    # because in that case we can just rely on the texture clamping.
    width_units = (n_columns * (PADDING_RATIO_X * 2 + FLAG_RATIO_X) -
                   PADDING_RATIO_X * 2)
    n_rows = (n_images + n_columns - 1) // n_columns
    height_units = (n_rows * (PADDING_RATIO_Y * 2 + FLAG_RATIO_Y) -
                   PADDING_RATIO_Y * 2)

    return (width_units, height_units, n_rows)

def calculate_wasted_space(n_columns, n_images):
    (width_units, height_units, n_rows) = calculate_units(n_columns, n_images)

    if width_units / height_units > TEX_WIDTH / TEX_HEIGHT:
        # Fit the width
        unit_size = TEX_WIDTH / width_units
    else:
        # Fit the height
        unit_size = TEX_HEIGHT / height_units

    used_space = width_units * height_units * unit_size * unit_size

    remainder = n_images // n_columns
    if remainder > 0:
        used_space -= (((FLAG_RATIO_X + PADDING_RATIO_X * 2) * n_columns -
                        PADDING_RATIO_X) *
                       (FLAG_RATIO_Y + PADDING_RATIO_Y) *
                       unit_size * unit_size)

    return TEX_WIDTH * TEX_HEIGHT - used_space

# Tries all possible numbers of columns to find the one which would
# waste the least space
def calculate_n_columns(n_images):
    best_n_columns = 1
    best_wasted_space = TEX_WIDTH * TEX_HEIGHT

    for n_columns in range(1, n_images + 1):
        wasted_space = calculate_wasted_space(n_columns, n_images)
        if wasted_space < best_wasted_space:
            best_n_columns = n_columns
            best_wasted_space = wasted_space

    return best_n_columns

def fill_flag_surface(cr, filename, width_units, height_units):
    result = subprocess.call(["inkscape", "--export-png=temp.png", filename])
    if result != 0:
        raise(Exception("Inkscape export failed for " + filename))

    flag_surface = cairo.ImageSurface.create_from_png("temp.png")

    flag_width = flag_surface.get_width()
    flag_height = flag_surface.get_height()

    if flag_width / flag_height > FLAG_RATIO_X / FLAG_RATIO_Y:
        # Fill the width
        unit_width = FLAG_RATIO_X
        unit_height = unit_width * flag_height / flag_width
    else:
        # Fill the height
        unit_height = FLAG_RATIO_Y
        unit_width = unit_height * flag_width / flag_height

    pixel_width = unit_width * TEX_WIDTH / width_units
    pixel_height = unit_height * TEX_HEIGHT / height_units

    m = cairo.Matrix()
    m.scale(flag_width / pixel_width, flag_height / pixel_height)
    m.translate(-(FLAG_RATIO_X / 2 - unit_width / 2) *
                TEX_WIDTH / width_units,
                -(FLAG_RATIO_Y / 2 - unit_height / 2) *
                TEX_HEIGHT / height_units)
    pattern = cairo.SurfacePattern(flag_surface)
    pattern.set_matrix(m)

    cr.set_source(pattern)
    cr.rectangle(0.0, 0.0,
                 mini_surface.get_width(),
                 mini_surface.get_height())
    cr.fill()

    os.unlink("temp.png")

def fill_border_surface(cr, width_units, height_units):
    cr.save()

    cr.scale(TEX_WIDTH / width_units, TEX_HEIGHT / height_units)

    cr.move_to(FLAG_RATIO_X - BORDER_RATIO / 2, FLAG_RATIO_Y)
    cr.line_to(FLAG_RATIO_X - BORDER_RATIO / 2, BORDER_RATIO)
    cr.arc_negative(FLAG_RATIO_X - BORDER_RATIO,
                    BORDER_RATIO,
                    BORDER_RATIO / 2,
                    math.pi * 2.0,
                    math.pi * 1.5)
    cr.line_to(BORDER_RATIO, BORDER_RATIO / 2)
    cr.arc_negative(BORDER_RATIO,
                    BORDER_RATIO,
                    BORDER_RATIO / 2,
                    math.pi * 1.5,
                    math.pi)
    cr.line_to(BORDER_RATIO / 2, FLAG_RATIO_Y)
    cr.set_source_rgba(104 / 255.0, 231 / 255.0, 226 / 255.0, 0.6)
    cr.fill_preserve()

    cr.line_to(0, FLAG_RATIO_Y)
    cr.line_to(0, BORDER_RATIO)
    cr.arc(BORDER_RATIO, BORDER_RATIO, BORDER_RATIO, math.pi, math.pi * 1.5)
    cr.line_to(FLAG_RATIO_X - BORDER_RATIO, 0)
    cr.arc(FLAG_RATIO_X - BORDER_RATIO,
           BORDER_RATIO,
           BORDER_RATIO,
           math.pi * 1.5,
           math.pi * 2.0)
    cr.line_to(FLAG_RATIO_X, FLAG_RATIO_Y)
    cr.close_path()

    cr.set_source_rgba(0.0, 0.5, 0.5, 0.6)
    cr.fill()
    cr.restore()

files = list(sorted(glob(os.path.join(sys.argv[1], '*.svg'))))

n_flag_spaces = len(files) + 1
n_columns = calculate_n_columns(n_flag_spaces)
(width_units, height_units, n_rows) = calculate_units(n_columns, n_flag_spaces)

header = open('flag-layout.h', 'w', encoding='utf-8')
header.write("/* Automatically generated by generate-flag-texture.py */\n"
             "#define FV_FLAG_TEXTURE_FLAG_RATIO_X {}\n"
             "#define FV_FLAG_TEXTURE_FLAG_RATIO_Y {}\n"
             "#define FV_FLAG_TEXTURE_PADDING_RATIO_X {}\n"
             "#define FV_FLAG_TEXTURE_PADDING_RATIO_Y {}\n"
             "#define FV_FLAG_TEXTURE_BORDER_RATIO {}\n"
             "#define FV_FLAG_TEXTURE_WIDTH_UNITS {}\n"
             "#define FV_FLAG_TEXTURE_HEIGHT_UNITS {}\n"
             "#define FV_FLAG_TEXTURE_N_COLUMNS {}\n"
             "#define FV_FLAG_TEXTURE_N_ROWS {}\n"
             "static const uint32_t\n"
             "fv_flag_texture_flags[] = {{\n".format(
                 FLAG_RATIO_X,
                 FLAG_RATIO_Y,
                 PADDING_RATIO_X,
                 PADDING_RATIO_Y,
                 BORDER_RATIO,
                 width_units,
                 height_units,
                 n_columns,
                 n_rows))
for file in files:
    md = re.search(r'([0-9a-f]{8})\.svg$', file)
    header.write("\t0x{},\n".format(md.group(1)))
header.write("};\n")
header.close()

surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, TEX_WIDTH, TEX_HEIGHT)
cr = cairo.Context(surface)

cr.save()
cr.set_source_rgba(0.0, 0.0, 0.0, 0.0)
cr.set_operator(cairo.OPERATOR_SOURCE)
cr.paint()
cr.restore()

for file_num in range(0, n_flag_spaces):
    mini_surface = cairo.ImageSurface(cairo.FORMAT_ARGB32,
                                      FLAG_RATIO_X * TEX_WIDTH // width_units,
                                      FLAG_RATIO_Y * TEX_HEIGHT // height_units)
    mini_cr = cairo.Context(mini_surface)
    mini_cr.save()
    mini_cr.set_source_rgba(0.0, 0.0, 0.0, 0.0)
    mini_cr.set_operator(cairo.OPERATOR_SOURCE)
    mini_cr.paint()
    mini_cr.restore()

    if file_num >= len(files):
        fill_border_surface(mini_cr, width_units, height_units)
    else:
        fill_flag_surface(mini_cr,
                          files[file_num],
                          width_units,
                          height_units)

    pattern = cairo.SurfacePattern(mini_surface)
    m = cairo.Matrix()
    column = file_num % n_columns
    row = file_num // n_columns
    off_x = column * (FLAG_RATIO_X + PADDING_RATIO_X * 2)
    off_y = row * (FLAG_RATIO_Y + PADDING_RATIO_Y * 2)
    m.translate(-off_x * TEX_WIDTH / width_units,
                -off_y * TEX_HEIGHT / height_units)
    pattern.set_matrix(m)
    pattern.set_extend(cairo.EXTEND_PAD)

    cr.save()
    cr.set_source(pattern)

    cr.rectangle((off_x - PADDING_RATIO_X) * TEX_WIDTH / width_units,
                 (off_y - PADDING_RATIO_Y) * TEX_HEIGHT / height_units,
                 (FLAG_RATIO_X + PADDING_RATIO_X * 2) *
                 TEX_WIDTH / width_units,
                 (FLAG_RATIO_Y + PADDING_RATIO_Y * 2) *
                 TEX_HEIGHT / height_units)
    cr.fill()

    cr.restore()

surface.write_to_png('flag-texture.png')
