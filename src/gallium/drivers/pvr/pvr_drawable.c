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

#include "util/u_atomic.h"
#include "util/u_debug.h"
#include "util/ralloc.h"

#include "pvr/ddk/pvr_ddk_private.h"
#include "pvr/ddk/pvr_dri_compat.h"
#include "pvr/ddk/pvr_dri_support.h"
#include "pvr/ddk/pvr_dri_support_ddk.h"

#include "pvr_context.h"
#include "pvr_drawable.h"
#include "pvr_dri_callbacks.h"
#include "pvr_mesa_dispatch.h"
#include "pvr_resource.h"
#include "pvr_screen.h"

bool
MODSUPConfigQuery(const PVRDRIConfig *psConfig,
                  PVRDRIConfigAttrib eConfigAttrib,
                  unsigned int *puValueOut)
{
   const struct gl_config *visual = (const struct gl_config *)psConfig;

   if (!psConfig || !puValueOut)
      return false;

   switch (eConfigAttrib) {
   case PVRDRI_CONFIG_ATTRIB_RENDERABLE_TYPE:
      /* The supported APIS. No longer used by the DDK , so just return zero. */
      *puValueOut = 0;
      return true;
   case PVRDRI_CONFIG_ATTRIB_DOUBLE_BUFFER_MODE:
      *puValueOut = visual->doubleBufferMode;
      return true;
   case PVRDRI_CONFIG_ATTRIB_RED_BITS:
      *puValueOut = visual->redBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_GREEN_BITS:
      *puValueOut = visual->greenBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_BLUE_BITS:
      *puValueOut = visual->blueBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_ALPHA_BITS:
      *puValueOut = visual->alphaBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_RGB_BITS:
      *puValueOut = visual->rgbBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_DEPTH_BITS:
      *puValueOut = visual->depthBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_STENCIL_BITS:
      *puValueOut = visual->stencilBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_SAMPLE_BUFFERS:
      *puValueOut = !!visual->samples;
      return true;
   case PVRDRI_CONFIG_ATTRIB_SAMPLES:
      *puValueOut = visual->samples;
      return true;
   case PVRDRI_CONFIG_ATTRIB_BIND_TO_TEXTURE_RGB:
      *puValueOut = GL_TRUE;
      return true;
   case PVRDRI_CONFIG_ATTRIB_BIND_TO_TEXTURE_RGBA:
      *puValueOut = GL_TRUE;
      return true;
   case PVRDRI_CONFIG_ATTRIB_RGB_MODE:
      *puValueOut = visual->rgbMode;
      return true;
   case PVRDRI_CONFIG_ATTRIB_YUV_ORDER:
      *puValueOut = visual->YUVOrder;
      return true;
   case PVRDRI_CONFIG_ATTRIB_YUV_NUM_OF_PLANES:
      *puValueOut = visual->YUVNumberOfPlanes;
      return true;
   case PVRDRI_CONFIG_ATTRIB_YUV_SUBSAMPLE:
      *puValueOut = visual->YUVSubsample;
      return true;
   case PVRDRI_CONFIG_ATTRIB_YUV_DEPTH_RANGE:
      *puValueOut = visual->YUVDepthRange;
      return true;
   case PVRDRI_CONFIG_ATTRIB_YUV_CSC_STANDARD:
      *puValueOut = visual->YUVCSCStandard;
      return true;
   case PVRDRI_CONFIG_ATTRIB_YUV_PLANE_BPP:
      *puValueOut = visual->YUVPlaneBPP;
      return true;
   case PVRDRI_CONFIG_ATTRIB_RED_MASK:
      *puValueOut = visual->redMask;
      return true;
   case PVRDRI_CONFIG_ATTRIB_GREEN_MASK:
      *puValueOut = visual->greenMask;
      return true;
   case PVRDRI_CONFIG_ATTRIB_BLUE_MASK:
      *puValueOut = visual->blueMask;
      return true;
   case PVRDRI_CONFIG_ATTRIB_ALPHA_MASK:
      *puValueOut = visual->alphaMask;
      return true;
   case PVRDRI_CONFIG_ATTRIB_SRGB_CAPABLE:
      *puValueOut = visual->sRGBCapable;
      return true;
   case PVRDRI_CONFIG_ATTRIB_INVALID:
      debug_printf("%s: Invalid attribute\n", __func__);
      return false;
   default:
      return false;
   }
}

void *
MODSUPDrawableGetReferenceHandle(struct __DRIdrawableRec *psDRIDrawable)
{
   struct pvr_drawable *drawable = (struct pvr_drawable *)psDRIDrawable;

   return (void *)drawable;
}

void
MODSUPDrawableAddReference(void *pvReferenceHandle)
{
   struct pvr_drawable *drawable = (struct pvr_drawable *)pvReferenceHandle;

   drawable->ddrawable_ref(drawable->ddrawable);
}

void
MODSUPDrawableRemoveReference(void *pvReferenceHandle)
{
   struct pvr_drawable *drawable = (struct pvr_drawable *)pvReferenceHandle;

   drawable->ddrawable_unref(drawable->ddrawable);
}

int
MODSUPGetBuffers(struct __DRIdrawableRec *psDRIDrawable,
                 unsigned int uFourCC,
                 uint32_t *puStamp,
                 void *pvLoaderPrivate,
                 uint32_t uBufferMask,
                 struct PVRDRIImageList *psImageList)
{
   struct pvr_drawable *drawable = (struct pvr_drawable *)psDRIDrawable;
   struct pvr_context *context = pvrdri_get_current_context();
   enum st_attachment_type atts[2] = {0};
   struct pipe_resource *bufs[2] = {0};
   unsigned int count = 0;
   unsigned int back = 0;
   unsigned int front = 0;
   if ((uBufferMask & PVRDRI_IMAGE_BUFFER_BACK) != 0) {
      atts[count] = ST_ATTACHMENT_BACK_LEFT;
      back = count++;

      assert(count < sizeof(atts));
   }
   if ((uBufferMask & PVRDRI_IMAGE_BUFFER_FRONT) != 0) {
      atts[count] = ST_ATTACHMENT_FRONT_LEFT;
      front = count++;

      assert(count < sizeof(atts));
   }


   if (!drawable->dri_framebuffer_validate(context->dctx,
                                           drawable->ddrawable,
                                           atts,
                                           count,
                                           bufs,
                                           NULL))
      return 0;

   psImageList->uImageMask = 0;

   if ((uBufferMask & PVRDRI_IMAGE_BUFFER_BACK) != 0) {
      pipe_resource_reference(&drawable->back, bufs[back]);

      /* dri_framebuffer_validate took a reference on our behalf, drop it */
      pipe_resource_reference(&bufs[back], NULL);

      if (drawable->back) {
         psImageList->psBack = pvr_resource(drawable->back)->drisup_image;
         psImageList->uImageMask |= PVRDRI_IMAGE_BUFFER_BACK;
      }
   }

   if ((uBufferMask & PVRDRI_IMAGE_BUFFER_FRONT) != 0) {
      pipe_resource_reference(&drawable->front, bufs[front]);

      /* dri_framebuffer_validate took a reference on our behalf, drop it */
      pipe_resource_reference(&bufs[front], NULL);

      if (drawable->front) {
         psImageList->psFront = pvr_resource(drawable->front)->drisup_image;
         psImageList->uImageMask |= PVRDRI_IMAGE_BUFFER_FRONT;
      }
   }

   return 1;
}

void
MODSUPFlushFrontBuffer(struct __DRIdrawableRec *psDRIDrawable,
                       void *pvLoaderPrivate)
{
   struct pvr_drawable *drawable = (struct pvr_drawable *)psDRIDrawable;

   if (!drawable->visual.doubleBufferMode) {
      if (p_atomic_inc_return(&drawable->flush_front_count) == 1) {
         if (!drawable->dri_flush_frontbuffer(NULL,
                                              drawable->ddrawable,
                                              ST_ATTACHMENT_FRONT_LEFT))
           debug_printf("%s: Couldn't flush front buffer\n", __func__);
      }
      p_atomic_dec(&drawable->flush_front_count);
   }
}

static void
pvr_drawable_invalidate(struct pipe_drawable *pdrawable)
{
   struct pvr_drawable *drawable = pvr_drawable(pdrawable);

   DRISUPInvalidate(drawable->drisup_drawable);
}

static void
pvr_drawable_destroy(struct pipe_drawable *pdrawable)
{
   struct pvr_drawable *drawable = pvr_drawable(pdrawable);

   assert(pdrawable->screen->is_pvr);

   pipe_resource_reference(&drawable->back, NULL);
   pipe_resource_reference(&drawable->front, NULL);

   DRISUPDestroyDrawable(drawable->drisup_drawable);

   pvr_screen_unref(pvr_screen(drawable->base.screen));

   ralloc_free(drawable);

}

struct pipe_drawable *pvr_drawable_create(struct pipe_screen *pscreen,
                                          struct dri_drawable *ddrawable,
                                          const struct gl_config *visual,
                                          bool isPixmap,
                                          void (*ddrawable_ref)(struct dri_drawable *ddrawable),
                                          void (*ddrawable_unref)(struct dri_drawable *ddrawable),
                                          bool (*dri_framebuffer_validate)(struct dri_context *ctx,
                                                 struct dri_drawable *drawable,
                                                 const enum st_attachment_type *statts,
                                                 unsigned count,
                                                 struct pipe_resource **out,
                                                 struct pipe_resource **resolve),
                                          bool (*dri_flush_frontbuffer)(struct dri_context *ctx,
                                                                        struct dri_drawable *drawable,
                                                                        enum st_attachment_type statt))
{
   struct pvr_screen *screen = pvr_screen(pscreen);
   struct pvr_drawable *drawable;

   drawable = rzalloc(NULL, struct pvr_drawable);
   if (!drawable)
      return NULL;

   drawable->base.screen = pscreen;

   drawable->base.destroy = pvr_drawable_destroy;
   drawable->base.invalidate = pvr_drawable_invalidate;

   drawable->ddrawable = ddrawable;
   drawable->ddrawable_ref = ddrawable_ref;
   drawable->ddrawable_unref = ddrawable_unref;
   drawable->dri_framebuffer_validate = dri_framebuffer_validate;
   drawable->dri_flush_frontbuffer = dri_flush_frontbuffer;

   drawable->visual = *visual;

   drawable->drisup_drawable =
      DRISUPCreateDrawableType((struct __DRIdrawableRec *)drawable,
                               screen->drisup_screen,
                               NULL,
                               (PVRDRIConfig *)&drawable->visual,
                               isPixmap);
   if (!drawable->drisup_drawable)
      goto exit_fail;

   pvr_screen_ref(pvr_screen(pscreen));

   return &drawable->base;

exit_fail:
   ralloc_free(drawable);

   return NULL;
}
