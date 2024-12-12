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

#include <assert.h>
#include <stdlib.h>

#include "pipe/p_state.h"

#include "util/u_inlines.h"

#include "pvr/ddk/pvr_dri_support.h"

#include "pvr_context.h"
#include "pvr_fence.h"
#include "pvr_screen.h"

enum pvr_fence_type {
   PVR_FENCE_TYPE_TASK = 1,
   PVR_FENCE_TYPE_FD,
   PVR_FENCE_TYPE_CL
};

struct pipe_fence_handle {
   struct pipe_reference reference;
   void *drisup_fence;
   enum pvr_fence_type type;
};

void
pvr_fence_reference(struct pipe_screen *pscreen,
                    struct pipe_fence_handle **ptr,
                    struct pipe_fence_handle *fence)
{
   struct pipe_fence_handle *old = *ptr;

   if (pipe_reference(&old->reference, &fence->reference)) {
      struct pvr_screen *screen = pvr_screen(pscreen);

      DRISUPDestroyFence(screen->drisup_screen, old->drisup_fence);
      free(old);
   }

   *ptr = fence;
}

bool
pvr_fence_finish(struct pipe_screen *pscreen, struct pipe_context *pctx,
                 struct pipe_fence_handle *fence, uint64_t timeout)
{
   struct pvr_context *ctx = pvr_context(pctx);

   return DRISUPClientWaitSync(ctx ? ctx->drisup_context : NULL,
                               fence->drisup_fence, 0, timeout);
}

int
pvr_fence_get_fd(struct pipe_screen *pscreen, struct pipe_fence_handle *fence)
{
   struct pvr_screen *screen = pvr_screen(pscreen);

   switch (fence->type) {
   case PVR_FENCE_TYPE_FD:
      return DRISUPGetFenceFD(screen->drisup_screen, fence->drisup_fence);
   default:
      return -1;
   }
}

static struct pipe_fence_handle *
pvr_fence_create_common(struct pipe_screen *pscreen, struct pipe_context *pctx,
                        enum pvr_fence_type type, intptr_t obj)
{
   struct pvr_screen *screen = pvr_screen(pscreen);
   struct pvr_context *ctx = pvr_context(pctx);
   struct pipe_fence_handle *fence;

   fence = calloc(1, sizeof(*fence));
   if (!fence)
      return NULL;

   switch (type) {
   case PVR_FENCE_TYPE_TASK:
      fence->drisup_fence = DRISUPCreateFence(ctx->drisup_context);
      break;
   case PVR_FENCE_TYPE_FD:
      fence->drisup_fence = DRISUPCreateFenceFD(ctx->drisup_context,
                                                (int)obj);
      break;
   case PVR_FENCE_TYPE_CL:
      fence->drisup_fence = DRISUPGetFenceFromCLEvent(screen->drisup_screen,
                                                      obj);
      break;
   default:
      debug_printf("%s: unknown PVR fence type: %d\n", __func__, (int)type);
      goto err_free_fence;
   }

   if (!fence->drisup_fence)
      goto err_free_fence;

   pipe_reference_init(&fence->reference, 1);

   fence->type = type;

   return fence;

err_free_fence:
   free(fence);
   return NULL;
}

void
pvr_fence_create_fd(struct pipe_context *pctx,
                    struct pipe_fence_handle **pfence, int fd,
                    enum pipe_fd_type type)
{
   switch (type) {
   case PIPE_FD_TYPE_NATIVE_SYNC:
      *pfence = pvr_fence_create_common(NULL, pctx,
                                        PVR_FENCE_TYPE_FD, (intptr_t)fd);
      break;
   default:
      debug_printf("%s: unsupported pipe_fd_type: %d\n", __func__, (int)type);
      *pfence = NULL;
      break;
   }
}

void
pvr_fence_create(struct pipe_context *pctx, struct pipe_fence_handle **pfence)
{
   *pfence = pvr_fence_create_common(NULL, pctx,
                                     PVR_FENCE_TYPE_TASK, (intptr_t)-1);
}

void *
pvr_get_fence_from_cl_event(struct pipe_screen *pscreen, intptr_t cl_event)
{
   return pvr_fence_create_common(pscreen, NULL, PVR_FENCE_TYPE_CL, cl_event);
}

void
pvr_fence_server_sync(struct pipe_context *pctx,
                      struct pipe_fence_handle *fence,
                      uint64_t value)
{
   struct pvr_context *ctx = pvr_context(pctx);

   assert(!value);

   DRISUPServerWaitSync(ctx->drisup_context, fence->drisup_fence, 0);
}
