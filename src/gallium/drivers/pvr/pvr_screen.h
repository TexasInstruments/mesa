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

#ifndef PVR_SCREEN_H
#define PVR_SCREEN_H

#include "pipe/p_screen.h"
#include "pipe/p_state.h"

#include "renderonly/renderonly.h"

#include "util/u_inlines.h"

struct DRISUPScreen;
struct pvr_config;
struct _glapi_table;

struct pvr_screen {
   struct pipe_screen base;

   struct pipe_reference ref;

   int gpu_fd;
   int display_fd;
   struct renderonly *ro;
   struct DRISUPScreen *drisup_screen;
   struct pvr_config *config;
   int gles1_version;
   int gles2_version;

   void *dri_screen_loader_private;
   unsigned char (*validateEGLImage)(void *image, void *loaderPrivate);
   struct dri_image *(*lookupEGLImageValidated)(void *image, void *loaderPrivate);
   struct pipe_resource *(*resource_from_image)(struct dri_image *);

   struct _glapi_table *ogles1_dispatch;
   struct _glapi_table *ogles2_dispatch;
   struct _glapi_table *ogl_dispatch;

   int num_dmabuf_formats;
   int *dmabuf_formats;

   bool driver_name_is_inferred;
};

static inline struct pvr_screen *
pvr_screen(struct pipe_screen *p)
{
   return (struct pvr_screen *)p;
}

struct pipe_screen *pvr_screen_create(int fd,
                                      const struct pipe_screen_config *config,
                                      struct renderonly *ro);
void
pvr_release_screen(struct pvr_screen *screen);

static inline void
pvr_screen_ref(struct pvr_screen *screen)
{
   (void) pipe_reference(NULL, &screen->ref);
}

static inline bool
pvr_screen_unref_ret(struct pvr_screen *screen)
{
   bool ret = pipe_reference(&screen->ref, NULL);

   if (ret)
      pvr_release_screen(screen);

   return ret;
}

static inline void
pvr_screen_unref(struct pvr_screen *screen)
{
   (void) pvr_screen_unref_ret(screen);
}

#endif /* PVR_SCREEN_H */
