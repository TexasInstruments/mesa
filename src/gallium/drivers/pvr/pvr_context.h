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

#ifndef PVR_CONTEXT_H
#define PVR_CONTEXT_H

#include "main/glconfig.h"

#include "frontend/api.h"

#include "pipe/p_context.h"

#include "pvr/ddk/pvr_dri_support.h"

struct DRISUPContext;

struct pvr_context {
   struct pipe_context base;
   struct dri_context *dctx;
   struct DRISUPContext *drisup_context;
   struct gl_config gl_mode;
   PVRDRIAPIType api;
};

static inline struct pvr_context *
pvr_context(struct pipe_context *p)
{
   return (struct pvr_context *) p;
}

struct st_context_attribs;

struct pipe_context *pvr_context_create_pvr(struct pipe_screen *pscreen,
                                            struct dri_context *dctx,
                                            const struct st_context_attribs *attribs,
                                            const struct gl_config *mode,
                                            struct pipe_context *shared_ctx,
                                            enum st_context_error *error);

struct dri_context *pvr_get_current_dri_context(struct pipe_screen *pscreen);

#endif /* PVR_CONTEXT_H */
