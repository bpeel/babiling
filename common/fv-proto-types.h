/*
 * Babiling
 * Copyright (C) 2015  Neil Roberts
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

/* This header is included multiple times with different definitions
 * of the FV_PROTO_TYPE macro. The macro will be called like this:
 *
 * FV_PROTO_TYPE(enum_name - the name of the type in fv_proto_type
 *               type_name - the name of the C type
 *               ap_type_name - the name of a type that can be used to
 *                              retrieve the value with va_arg)
 */

#define FV_PROTO_TYPE_SIMPLE(enum_name, type_name)      \
        FV_PROTO_TYPE(enum_name, type_name, type_name)

FV_PROTO_TYPE(FV_PROTO_TYPE_UINT8, uint8_t, unsigned int)
FV_PROTO_TYPE(FV_PROTO_TYPE_UINT16, uint16_t, unsigned int)
FV_PROTO_TYPE_SIMPLE(FV_PROTO_TYPE_UINT32, uint32_t)
FV_PROTO_TYPE_SIMPLE(FV_PROTO_TYPE_UINT64, uint64_t)

#undef FV_PROTO_TYPE_SIMPLE
