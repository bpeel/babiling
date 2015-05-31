/*
 * Finvenkisto
 * Copyright (C) 2013  Neil Roberts
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef FV_FILE_ERROR_H
#define FV_FILE_ERROR_H

#include "fv-error.h"

extern struct fv_error_domain
fv_file_error;

enum fv_file_error {
  FV_FILE_ERROR_EXIST,
  FV_FILE_ERROR_ISDIR,
  FV_FILE_ERROR_ACCES,
  FV_FILE_ERROR_NAMETOOLONG,
  FV_FILE_ERROR_NOENT,
  FV_FILE_ERROR_NOTDIR,
  FV_FILE_ERROR_AGAIN,
  FV_FILE_ERROR_INTR,
  FV_FILE_ERROR_PERM,
  FV_FILE_ERROR_PFNOSUPPORT,
  FV_FILE_ERROR_AFNOSUPPORT,

  FV_FILE_ERROR_OTHER
};

enum fv_file_error
fv_file_error_from_errno(int errnum);

FV_PRINTF_FORMAT(3, 4) void
fv_file_error_set(struct fv_error **error,
                   int errnum,
                   const char *format,
                   ...);

#endif /* FV_FILE_ERROR_H */
