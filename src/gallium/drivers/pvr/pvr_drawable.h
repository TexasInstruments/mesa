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

#ifndef PVR_DRAWABLE_H
#define PVR_DRAWABLE_H

#include <stdbool.h>

#include "frontend/api.h"

#include "main/glconfig.h"

#include "pipe/p_drawable.h"

#include "util/u_inlines.h"

struct dri_context;
struct dri_drawable;
struct pipe_resource;

struct DRISUPDrawable;

struct pvr_drawable {
   struct pipe_drawable base;

   struct DRISUPDrawable *drisup_drawable;

   struct dri_drawable *ddrawable;
   void (*ddrawable_ref)(struct dri_drawable *drawable);
   void (*ddrawable_unref)(struct dri_drawable *drawable);
   bool (*dri_framebuffer_validate)(struct dri_context *ctx,
                                    struct dri_drawable *drawable,
                                    const enum st_attachment_type *statts,
                                    unsigned count,
                                    struct pipe_resource **out,
                                    struct pipe_resource **resolve);
   bool (*dri_flush_frontbuffer)(struct dri_context *ctx,
                                 struct dri_drawable *drawable,
                                 enum st_attachment_type statt);

   struct gl_config visual;

   struct pipe_resource *back;
   struct pipe_resource *front;

   int flush_front_count;
};

static inline struct pvr_drawable *
pvr_drawable(struct pipe_drawable *p)
{
   return (struct pvr_drawable *) p;
}

struct pipe_drawable *pvr_drawable_create(struct pipe_screen *pscreen,
                                          struct dri_drawable *drawable,
                                          const struct gl_config *mode,
                                          bool isPixmap,
                                          void (*drawable_ref)(struct dri_drawable *drawable),
                                          void (*drawable_unref)(struct dri_drawable *drawable),
                                          bool (*dri_framebuffer_validate)(struct dri_context *ctx,
                                                 struct dri_drawable *drawable,
                                                 const enum st_attachment_type *statts,
                                                 unsigned count,
                                                 struct pipe_resource **out,
                                                 struct pipe_resource **resolve),
                                          bool (*dri_flush_frontbuffer)(struct dri_context *ctx,
                                                                        struct dri_drawable *drawable,
                                                                        enum st_attachment_type statt));
#endif /* PVR_DRAWABLE_H */
