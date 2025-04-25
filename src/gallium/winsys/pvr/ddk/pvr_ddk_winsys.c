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

#include "renderonly/renderonly.h"
#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_screen.h"

#include "pvr_ddk_public.h"

#include "pvr/pvr_screen.h"

struct pipe_screen *
pvr_ddk_screen_create_renderonly(int fd, struct renderonly *ro,
                                 const struct pipe_screen_config *config)
{
   return u_pipe_screen_lookup_or_create(fd, config, ro, pvr_screen_create);
}

static void pvr_ddk_ro_destroy(struct renderonly *ro)
{
   FREE(ro);
}

struct pipe_screen *
pvr_ddk_screen_create(int fd, int kms_fd, bool use_kms_fd,
                      const struct pipe_screen_config *config)
{
   struct renderonly *ro = CALLOC_STRUCT(renderonly);
   struct pipe_screen *screen;

   if (!ro) {
      debug_printf("%s: Out of memory\n", __func__);
      return NULL;
   }

   ro->gpu_fd = fd;
   ro->kms_fd = kms_fd;
   ro->use_kms_fd = use_kms_fd;
   ro->destroy = pvr_ddk_ro_destroy;

   screen = pvr_ddk_screen_create_renderonly(fd, ro, config);

   return screen;
}

struct renderonly_scanout *
pvr_create_kms_buffer_for_resource(struct pipe_resource *rsc,
                                   struct renderonly *ro,
                                   struct winsys_handle *out_handle)
{
   return NULL;
}
