/*
 * Babiling
 *
 * Copyright (C) 2014, 2015 Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FV_IMAGE_H
#define FV_IMAGE_H

#include <stdint.h>
#include <GL/gl.h>

enum fv_image_data_image {
#include "data/fv-image-data-enum.h"
};

enum fv_image_data_result {
        FV_IMAGE_DATA_SUCCESS,
        FV_IMAGE_DATA_FAIL,
};

struct fv_image_data;

struct fv_image_data *
fv_image_data_new(uint32_t loaded_event);

void
fv_image_data_get_size(struct fv_image_data *data,
                       enum fv_image_data_image image,
                       int *width,
                       int *height);

void
fv_image_data_set_2d(struct fv_image_data *data,
                     GLenum target,
                     GLint level,
                     GLint internal_format,
                     enum fv_image_data_image image);

void
fv_image_data_set_sub_2d(struct fv_image_data *data,
                         GLenum target,
                         GLint level,
                         GLint x_offset, GLint y_offset,
                         enum fv_image_data_image image);

void
fv_image_data_set_sub_3d(struct fv_image_data *data,
                         GLenum target,
                         GLint level,
                         GLint x_offset, GLint y_offset, GLint z_offset,
                         enum fv_image_data_image image);

void
fv_image_data_free(struct fv_image_data *data);

#endif /* FV_IMAGE_H */
