/*
 * Copyright (c) Imagination Technologies Ltd.
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef PVR_RESOURCE_H
#define PVR_RESOURCE_H

#include "pipe/p_state.h"

struct __DRIimageRec;

struct pvr_context;
struct pvr_screen;

struct pvr_resource {
   struct pipe_resource base;

   struct __DRIimageRec *drisup_image;
   int export_fd;
};

static inline struct pvr_resource *
pvr_resource(struct pipe_resource *p)
{
   return (struct pvr_resource *) p;
}

void
pvr_resource_screen_init(struct pvr_screen *screen);

void
pvr_resource_context_init(struct pvr_context *context);

void
pvr_resource_screen_destroy(struct pvr_screen *screen);

int
pvr_pipe_format_to_fourcc(enum pipe_format format);

#endif /* PVR_RESOURCE_H */
