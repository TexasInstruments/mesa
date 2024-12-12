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

#ifndef PVR_FENCE_H
#define PVR_FENCE_H

#include <stdint.h>

#include "pipe/p_defines.h"

struct pipe_screen;
struct pipe_fence_handle;
struct pipe_context;

void pvr_fence_reference(struct pipe_screen *pscreen,
                         struct pipe_fence_handle **ptr,
                         struct pipe_fence_handle *fence);

bool pvr_fence_finish(struct pipe_screen *pscreen, struct pipe_context *pctx,
                 struct pipe_fence_handle *fence, uint64_t timeout);

int pvr_fence_get_fd(struct pipe_screen *pscreen,
                     struct pipe_fence_handle *fence);

void pvr_fence_create_fd(struct pipe_context *pctx,
                         struct pipe_fence_handle **pfence, int fd,
                         enum pipe_fd_type type);

void pvr_fence_create(struct pipe_context *pctx,
                      struct pipe_fence_handle **pfence);

void pvr_fence_server_sync(struct pipe_context *pctx,
                           struct pipe_fence_handle *f,
                           uint64_t value);

void *pvr_get_fence_from_cl_event(struct pipe_screen *pscreen,
                                  intptr_t cl_event);

#endif /* PVR_FENCE_H */
