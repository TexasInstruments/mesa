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

#include "main/menums.h"
#include "main/glconfig.h"
#include "state_tracker/st_context.h"

#include "frontend/api.h"

#include "util/u_debug.h"
#include "util/format/u_formats.h"
#include "util/ralloc.h"

#include "pvr/ddk/pvr_ddk_private.h"
#include "pvr/ddk/pvr_dri_compat.h"
#include "pvr/ddk/pvr_dri_support.h"
#include "pvr/ddk/pvr_dri_support_ddk.h"

#include "pvr_context.h"
#include "pvr_drawable.h"
#include "pvr_fence.h"
#include "pvr_mesa_dispatch.h"
#include "pvr_resource.h"
#include "pvr_screen.h"

static void
pvr_context_destroy(struct pipe_context *pcontext)
{
   struct pvr_context *context = pvr_context(pcontext);

   if (context->drisup_context)
      DRISUPDestroyContext(context->drisup_context);

   pvr_screen_unref(pvr_screen(pcontext->screen));

   ralloc_free(context);
}

static bool
pvr_context_make_current(struct pipe_context *pcontext,
                         struct pipe_drawable *pwrite,
                         struct pipe_drawable *pread)
{
   struct pvr_context *context = pvr_context(pcontext);
   struct pvr_drawable *write = pvr_drawable(pwrite);
   struct pvr_drawable *read = pvr_drawable(pread);
   struct pvr_context *old_context = pvrdri_get_current_context();

   assert(pcontext->screen->is_pvr);

   /*
    * The current context needs to be set now, as the DDK may call
    * MODSUPGetBuffers as part of drawable initialisation.
    */
   pvrdri_set_current_context(context);

   if (DRISUPMakeCurrent(context->drisup_context,
                         write ? write->drisup_drawable : NULL,
                         read ? read->drisup_drawable : NULL)) {

      pvrdri_set_dispatch_table(context);

      return true;
   } else {
      pvrdri_set_current_context(old_context);

      return false;
   }
}

static void
pvr_context_unbind(struct pipe_context *pcontext)
{
   struct pvr_context *context = pvr_context(pcontext);

   assert(pcontext->screen->is_pvr);

   pvrdri_set_null_current_context();
   pvrdri_set_null_dispatch_table();
   DRISUPUnbindContext(context->drisup_context);
}

static void
pvr_context_flush(struct pipe_context *pcontext,
                  struct pipe_fence_handle **fence,
                  unsigned flags)
{
   if (fence != NULL || flags != 0)
      debug_printf("%s: Context flush not supported: &fence %p flags %u\n",
                   __func__, fence, flags);
}

static void
pvr_set_tex_buffer(struct pipe_context *pcontext, int target,
                   int format, struct pipe_drawable *pdrawable)
{
   struct pvr_context *context = pvr_context(pcontext);
   struct pvr_drawable *drawable = pvr_drawable(pdrawable);

   DRISUPSetTexBuffer2(context->drisup_context, target, format,
                       drawable->drisup_drawable);
}

static void
pvr_release_tex_buffer(struct pipe_context *pcontext, int target,
                       struct pipe_drawable *pdrawable)
{
   struct pvr_context *context = pvr_context(pcontext);
   struct pvr_drawable *drawable = pvr_drawable(pdrawable);

   DRISUPReleaseTexBuffer(context->drisup_context, target,
                          drawable->drisup_drawable);
}

struct pipe_context *
pvr_context_create_pvr(struct pipe_screen *pscreen,
                       struct dri_context *dctx,
                       const struct st_context_attribs *attribs,
                       const struct gl_config *mode,
                       struct pipe_context *shared_ctx,
                       enum st_context_error *error)
{
   struct pvr_screen *screen = pvr_screen(pscreen);
   struct pvr_context *context = NULL;
   PVRDRIAPIType api;
   struct PVRDRIContextConfig sCtxConfig = {0};
   unsigned int drisup_error;

   switch (attribs->profile) {
   case API_OPENGLES:
      api = PVRDRI_API_GLES1;
      break;
   case API_OPENGLES2:
      api = PVRDRI_API_GLES2;
      break;
   case API_OPENGL_COMPAT:
      api = PVRDRI_API_GL_COMPAT;
      break;
   case API_OPENGL_CORE:
      api = PVRDRI_API_GL_CORE;
      break;
   default:
      debug_printf("%s: Unsupported API: %d\n",
                       __func__, (int) attribs->profile);
      *error = ST_CONTEXT_ERROR_BAD_API;
      goto exit_fail;
   }

   if (attribs->flags & ~(ST_CONTEXT_FLAG_DEBUG |
                          ST_CONTEXT_FLAG_FORWARD_COMPATIBLE |
                          ST_CONTEXT_FLAG_RELEASE_NONE)) {
      debug_printf("%s: Unsupported context state tracker flags: 0x%lx\n",
                       __func__, (unsigned long) attribs->flags);
      *error = ST_CONTEXT_ERROR_UNKNOWN_FLAG;
      goto exit_fail;
   }

   if (attribs->context_flags & ~(PIPE_CONTEXT_HIGH_PRIORITY |
                                  PIPE_CONTEXT_LOSE_CONTEXT_ON_RESET |
                                  PIPE_CONTEXT_LOW_PRIORITY |
                                  PIPE_CONTEXT_PROTECTED |
                                  PIPE_CONTEXT_REALTIME_PRIORITY |
                                  PIPE_CONTEXT_ROBUST_BUFFER_ACCESS)) {
      debug_printf("%s: Unsupported context pipe flags: 0x%lx\n",
                       __func__, (unsigned long) attribs->context_flags);
      *error = ST_CONTEXT_ERROR_UNKNOWN_FLAG;
      goto exit_fail;
   }

   context = rzalloc(NULL, struct pvr_context);
   if (!context) {
      *error = ST_CONTEXT_ERROR_NO_MEMORY;
      goto exit_fail;
   }

   context->base.screen = pscreen;
   context->base.destroy = pvr_context_destroy;

   context->base.make_current = pvr_context_make_current;
   context->base.unbind = pvr_context_unbind;
   context->base.flush = pvr_context_flush;

   context->base.create_fence_fd = pvr_fence_create_fd;
   context->base.fence_server_sync = pvr_fence_server_sync;

   context->base.create_fence = pvr_fence_create;

   context->base.set_tex_buffer = pvr_set_tex_buffer;
   context->base.release_tex_buffer = pvr_release_tex_buffer;

   context->dctx = dctx;
   context->api = api;

   if (mode)
      context->gl_mode = *mode;

   sCtxConfig.uMajorVersion = attribs->major;
   sCtxConfig.uMinorVersion = attribs->minor;

   sCtxConfig.uFlags = 0;
   if (attribs->flags & ST_CONTEXT_FLAG_DEBUG)
      sCtxConfig.uFlags |= PVRDRI_CONTEXT_FLAG_DEBUG;
   if (attribs->flags & ST_CONTEXT_FLAG_FORWARD_COMPATIBLE)
      sCtxConfig.uFlags |= PVRDRI_CONTEXT_FLAG_FORWARD_COMPATIBLE;
   if (attribs->context_flags & PIPE_CONTEXT_ROBUST_BUFFER_ACCESS)
      sCtxConfig.uFlags |= PVRDRI_CONTEXT_FLAG_ROBUST_BUFFER_ACCESS;

   sCtxConfig.iResetStrategy = (attribs->context_flags &
                                PIPE_CONTEXT_LOSE_CONTEXT_ON_RESET) ?
                               PVRDRI_CONTEXT_RESET_LOSE_CONTEXT :
                               PVRDRI_CONTEXT_RESET_NO_NOTIFICATION;

   sCtxConfig.iReleaseBehavior = (attribs->flags &
                                  ST_CONTEXT_FLAG_RELEASE_NONE) ?
                                 PVRDRI_CONTEXT_RELEASE_BEHAVIOR_NONE :
                                 PVRDRI_CONTEXT_RELEASE_BEHAVIOR_FLUSH;

   switch (attribs->context_flags & (PIPE_CONTEXT_LOW_PRIORITY |
                                     PIPE_CONTEXT_HIGH_PRIORITY |
                                     PIPE_CONTEXT_REALTIME_PRIORITY)) {
   case PIPE_CONTEXT_LOW_PRIORITY:
      sCtxConfig.uPriority = PVRDRI_CONTEXT_PRIORITY_LOW;
      break;
   case PIPE_CONTEXT_HIGH_PRIORITY:
      sCtxConfig.uPriority = PVRDRI_CONTEXT_PRIORITY_HIGH;
      break;
   case PIPE_CONTEXT_REALTIME_PRIORITY:
      sCtxConfig.uPriority = PVRDRI_CONTEXT_PRIORITY_REALTIME;
      break;
   default:
      sCtxConfig.uPriority = PVRDRI_CONTEXT_PRIORITY_MEDIUM;
      break;
   }

   sCtxConfig.bProtected = (attribs->context_flags &
                            PIPE_CONTEXT_PROTECTED) != 0;

   drisup_error = DRISUPCreateContext(api,
                                     (PVRDRIConfig *)&context->gl_mode,
                                     &sCtxConfig,
                                     (struct __DRIcontextRec *)context,
                                     shared_ctx ? pvr_context(shared_ctx)->drisup_context : NULL,
                                     screen->drisup_screen,
                                     &context->drisup_context);

   if (drisup_error !=  PVRDRI_CONTEXT_ERROR_SUCCESS) {
      debug_printf("%s: Couldn't create DRISUP context: %u\n",
                   __func__, drisup_error);

      switch (drisup_error) {
      case PVRDRI_CONTEXT_ERROR_NO_MEMORY:
         *error = ST_CONTEXT_ERROR_NO_MEMORY;
         break;
      case PVRDRI_CONTEXT_ERROR_BAD_API:
         *error = ST_CONTEXT_ERROR_BAD_API;
         break;
      case PVRDRI_CONTEXT_ERROR_BAD_VERSION:
         *error = ST_CONTEXT_ERROR_BAD_VERSION;
         break;
      case PVRDRI_CONTEXT_ERROR_BAD_FLAG:
         *error = ST_CONTEXT_ERROR_BAD_FLAG;
         break;
      case PVRDRI_CONTEXT_ERROR_UNKNOWN_ATTRIBUTE:
         *error = ST_CONTEXT_ERROR_UNKNOWN_ATTRIBUTE;
         break;
      case PVRDRI_CONTEXT_ERROR_UNKNOWN_FLAG:
         *error = ST_CONTEXT_ERROR_UNKNOWN_FLAG;
         break;
      default:
         *error = ST_CONTEXT_ERROR_NO_MEMORY;
         break;
      }

      goto exit_fail;
   }

   if (!pvrdri_create_dispatch_table(screen, api)) {
      debug_printf("%s: Couldn't create dispatch table\n", __func__);

      goto exit_fail;
   }

   pvr_resource_context_init(context);

   pvr_screen_ref(pvr_screen(pscreen));

   return &context->base;

exit_fail:
   if (context) {
      if (context->drisup_context)
         DRISUPDestroyContext(context->drisup_context);

      ralloc_free(context);
   }
   return NULL;
}

struct dri_context *
pvr_get_current_dri_context(struct pipe_screen *pscreen)
{
   struct pvr_context *context = pvrdri_get_current_context();

   return context ? context->dctx : NULL;
}
