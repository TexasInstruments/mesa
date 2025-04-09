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

#include <unistd.h>

#include "drm-uapi/drm_fourcc.h"
#include "mesa_interface.h"

#include "util/macros.h"
#include "util/ralloc.h"
#include "util/u_screen.h"

#include "pvr/ddk/pvr_ddk_private.h"
#include "pvr/ddk/pvr_dri_compat.h"
#include "pvr/ddk/pvr_dri_support.h"
#include "pvr/ddk/pvr_dri_support_ddk.h"

#include "pvr_dri_callbacks.h"
#include "pvr_dri_callbacks_util.h"
#include "pvr_context.h"
#include "pvr_drawable.h"
#include "pvr_fence.h"
#include "pvr_mesa_dispatch.h"
#include "pvr_resource.h"
#include "pvr_screen.h"

static int
pvr_get_screen_fd(struct pipe_screen *pscreen)
{
   struct pvr_screen *screen = pvr_screen(pscreen);

   return screen->gpu_fd;
}

static const char *
pvr_get_name(struct pipe_screen *pscreen)
{
   struct pvr_screen *screen = pvr_screen(pscreen);
   const char *name;

   return DRISUPQueryRendererString(screen->drisup_screen,
                                    PVRDRI_RENDERER_DEVICE_ID,
                                    &name) ? NULL : name;
}

static const char *
pvr_get_vendor(struct pipe_screen *pscreen)
{
   return "Mesa";
}

static const char *
pvr_get_device_vendor(struct pipe_screen *pscreen)
{
   struct pvr_screen *screen = pvr_screen(pscreen);
   const char *vendor;

   return DRISUPQueryRendererString(screen->drisup_screen,
                                    PVRDRI_RENDERER_DEVICE_ID,
                                    &vendor) ? NULL : vendor;
}

static int
pvr_get_driver_query_info(struct pipe_screen *pscreen,
                          unsigned index,
                          struct pipe_driver_query_info *info)
{
   return 0;
}

static void
pvr_init_screen_caps(struct pvr_screen *screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&screen->base.caps;

   u_init_pipe_screen_caps(&screen->base, 1);

   {
      unsigned int value;

      if (DRISUPQueryRendererInteger(screen->drisup_screen,
                                     PVRDRI_RENDERER_HAS_CONTEXT_PRIORITY,
                                     &value) != -1)
         caps->context_priority_mask = value;

      if (DRISUPQueryRendererInteger(screen->drisup_screen,
                                     PVRDRI_RENDERER_DEVICE_ID,
                                     &value) != -1)
         caps->device_id = value;

      /* The EGL code tests whether the returned value is non-zero for
       * enabling the KHR_gl_texture_3D_image extension. Given that this
       * driver is only providing window system support, and no rendering
       * capabilities, this should be adequate. */
      if (DRISUPQueryRendererInteger(screen->drisup_screen,
                                     PVRDRI_RENDERER_HAS_TEXTURE_3D,
                                     &value) != -1)
         caps->max_texture_3d_levels = value;

      if (DRISUPQueryRendererInteger(screen->drisup_screen,
                                     PVRDRI_RENDERER_HAS_PROTECTED_SURFACE,
                                     &value) != -1)
         caps->device_protected_surface = value != 0;

      if (DRISUPQueryRendererInteger(screen->drisup_screen,
                                     PVRDRI_RENDERER_HAS_PROTECTED_CONTENT,
                                     &value) != -1)
         caps->device_protected_context = value != 0;

      if (DRISUPQueryRendererInteger(screen->drisup_screen,
                                     PVRDRI_RENDERER_PREFER_BACK_BUFFER_REUSE,
                                     &value) != -1)
         caps->prefer_back_buffer_reuse = value != 0;

      if (DRISUPQueryRendererInteger(screen->drisup_screen,
                                     PVRDRI_RENDERER_VENDOR_ID,
                                     &value) != -1)
         caps->vendor_id = value;
   }

   caps->npot_textures = true;
   caps->device_reset_status_query = true;
   caps->robust_buffer_access_behavior = true;

   caps->mesa_gl_interop = false;
   caps->context_no_error = false;
   caps->image_dmabuf_export = false;

   caps->mixed_color_depth_bits = !screen->config->color_depth_match;

   {
      int drisup_cap = DRISUPGetImageCapabilities(screen->drisup_screen);
      unsigned int pipe_cap = 0;

      if ((drisup_cap & PVRDRI_IMAGE_CAP_PRIME_IMPORT) != 0)
         pipe_cap |= DRM_PRIME_CAP_IMPORT;

      if ((drisup_cap & PVRDRI_IMAGE_CAP_PRIME_EXPORT) != 0)
         pipe_cap |= DRM_PRIME_CAP_EXPORT;

      caps->dmabuf = pipe_cap;
   }

   caps->native_fence_fd = (DRISUPGetFenceCapabilities(screen->drisup_screen) &
                            PVRDRI_FENCE_CAP_NATIVE_FD) != 0;

   caps->accumulation_buffer = screen->config->enable_accum;

   /* Not implemented */
   caps->uma = true;
   /* Not implemented */
   caps->video_memory = 0;
}

static void
pvr_query_dmabuf_modifiers(struct pipe_screen *pscreen,
                           enum pipe_format format,
                           int max,
                           uint64_t *modifiers,
                           unsigned int *external_only,
                           int *out_count)
{
   struct pvr_screen *screen = pvr_screen(pscreen);
   int fourcc = pvr_pipe_format_to_fourcc(format);

   if (!DRISUPQueryDMABufModifiers(screen->drisup_screen, fourcc, max,
                                   modifiers, external_only, out_count))
      *out_count = 0;
}

static bool
pvr_is_dmabuf_modifier_supported(struct pipe_screen *pscreen,
                                 uint64_t modifier,
                                 enum pipe_format format,
                                 bool *external_only)
{
   struct pvr_screen *screen = pvr_screen(pscreen);
   int fourcc = pvr_pipe_format_to_fourcc(format);
   uint64_t *modifiers = NULL;
   unsigned int *external = NULL;
   bool res = false;
   int max, count;

   /* DRISUPQueryDMABufFormatModifierAttribs could be enhanced to
    * support queries for external only, avoiding the need to call
    * DRISUPQueryDMABufModifiers. The current scheme could then be
    * used as a fallback.
    */

   if (!DRISUPQueryDMABufModifiers(screen->drisup_screen, fourcc, 0,
                                   NULL, NULL, &max))
      goto exit;

   modifiers = ralloc_array(screen, uint64_t, max);
   external = ralloc_array(screen, unsigned int, max);

   if (!modifiers || !external ||
       !DRISUPQueryDMABufModifiers(screen->drisup_screen, fourcc, max,
                                   modifiers, external, &count))
      goto exit;

   for (unsigned int i = 0; i < count; i++) {
      if (modifiers[i] == modifier) {
         res = true;

         if (external_only)
            *external_only = external[i] != 0;

	 break;
      }
   }

exit:
      ralloc_free(modifiers);
      ralloc_free(external);

      return res;
}

static void
pvr_set_damage_region(struct pipe_screen *screen,
                      struct pipe_resource *resource,
                      unsigned int nrects,
                      const struct pipe_box *rects)
{
   /* The DDK requires the damage region to be posted for the drawable,
    * rather than the resource, but currently ignores the information anyway.
    */
}

static void
pvr_flush_pvr(struct pipe_context *pcontext,
              struct pipe_drawable *pdrawable,
              unsigned int flags,
              unsigned int reason)
{
   struct pvr_context *context = pvr_context(pcontext);
   struct pvr_drawable *drawable = pvr_drawable(pdrawable);
   unsigned int drisup_flags = 0;
   unsigned int drisup_reason;

   if (flags & __DRI2_FLUSH_CONTEXT)
      drisup_flags |= PVRDRI_FLUSH_CONTEXT;

   if (flags & __DRI2_FLUSH_DRAWABLE)
      drisup_flags |= PVRDRI_FLUSH_DRAWABLE;

   switch (reason) {
   case __DRI2_THROTTLE_SWAPBUFFER:
      drisup_reason = PVRDRI_THROTTLE_SWAPBUFFER;
      break;
   case __DRI2_THROTTLE_COPYSUBBUFFER:
      drisup_reason = PVRDRI_THROTTLE_COPYSUBBUFFER;
      break;
   case __DRI2_THROTTLE_FLUSHFRONT:
      drisup_reason = PVRDRI_THROTTLE_FLUSHFRONT;
      break;
   default:
      drisup_reason = 0;
      break;
   }

   DRISUPFlushWithFlags(context->drisup_context,
                        drawable ? drawable->drisup_drawable : NULL,
                        drisup_flags, drisup_reason);
}

static void
pvr_api_query_versions(struct pipe_screen *pscreen,
                       struct st_config_options *options,
                       int *gl_core_version,
                       int *gl_compat_version,
                       int *gl_es1_version,
                       int *gl_es2_version)
{
   struct pvr_screen *screen = pvr_screen(pscreen);

   *gl_core_version = DRISUPGetAPIVersion(screen->drisup_screen,
                                          PVRDRI_API_GL_COMPAT);
   *gl_compat_version = DRISUPGetAPIVersion(screen->drisup_screen,
                                            PVRDRI_API_GL_CORE);

   *gl_es1_version = screen->gles1_version;
   *gl_es2_version = screen->gles2_version;
}

static void
pvr_set_dri_image_params(struct pipe_screen *pscreen,
                         void *loader_private,
                         unsigned char (*validateEGLImage)(void *image, void *loaderPrivate),
                         struct dri_image *(*lookupEGLImageValidated)(void *image, void *loaderPrivate),

                         struct pipe_resource *(*resource_from_image)(struct dri_image *))
{
   struct pvr_screen *screen = pvr_screen(pscreen);

   screen->dri_screen_loader_private = loader_private;
   screen->validateEGLImage = validateEGLImage;
   screen->lookupEGLImageValidated = lookupEGLImageValidated;
   screen->resource_from_image = resource_from_image;
}

void
pvr_release_screen(struct pvr_screen *screen)
{
   pvr_resource_screen_destroy(screen);

   pvrdri_free_dispatch_tables(screen);

   if (screen->drisup_screen)
      DRISUPDestroyScreen(screen->drisup_screen);

   if (screen->ro)
      screen->ro->destroy(screen->ro);

   PVRDRICompatDeinit();

   if (screen->display_fd != -1)
      close(screen->display_fd);

   ralloc_free(screen);
}

static bool
pvr_check_driver_compatibility(struct pipe_screen *pscreen,
                               int fd_render_gpu,
                               const char *driver_name_render_gpu,
                               int fd_display_gpu,
                               const char *driver_name_display_gpu)
{
   bool compat;

   debug_printf("%s: Render driver name: %s FD: %d\n", __func__,
                    driver_name_render_gpu, fd_render_gpu);
   debug_printf("%s: Display driver name: %s FD: %d\n", __func__,
                    driver_name_display_gpu ? driver_name_display_gpu : "",
                    fd_display_gpu);

   if (!driver_name_display_gpu)
      compat = false;
   else if (!strcmp(driver_name_display_gpu, driver_name_render_gpu))
      compat = true;
   else
      compat = pvr_ddk_is_driver_compat_name(driver_name_display_gpu);

   debug_printf("%s: Render and display drivers are %s\n", __func__,
                compat ? "compatible" : "incompatible");

   return compat;
}

static void
pvr_destroy_screen(struct pipe_screen *pscreen)
{
   if (!pvr_screen_unref_ret(pvr_screen(pscreen)))
      debug_printf("%s: Screen resources will not be freed until all references are released\n", __func__);
}

struct pipe_screen *pvr_screen_create(int fd,
                                      const struct pipe_screen_config *config,
                                      struct renderonly *ro)
{
   const struct PVRDRICallbacksV2 pvrdri_callbacks = {
      /* Version 0 callbacks */
      .v0.RegisterSupportInterface = MODSUPRegisterSupportInterfaceV2,
      .v0.GetBuffers = MODSUPGetBuffers,
      .v0.CreateConfigs = MODSUPCreateConfigs,
      .v0.ConcatConfigs = MODSUPConcatConfigs,
      .v0.ConfigQuery = MODSUPConfigQuery,
      .v0.LookupEGLImage = MODSUPLookupEGLImage,
      .v0.GetCapability = MODSUPGetCapability,
      /* Version 1 callbacks */
      .v1.FlushFrontBuffer = MODSUPFlushFrontBuffer,
      /* Version 2 callbacks */
      .v2.GetDisplayFD = MODSUPGetDisplayFD,
      /* Version 3 callbacks */
      .v3.DrawableGetReferenceHandle = MODSUPDrawableGetReferenceHandle,
      .v3.DrawableAddReference = MODSUPDrawableAddReference,
      .v3.DrawableRemoveReference = MODSUPDrawableRemoveReference,
      /* Version 4 callbacks */
      .v4.DestroyLoaderImageState = MODSUPDestroyLoaderImageState,
      /* Version 5 has no callbacks */
      /* Version 6 callbacks */
      .v6.IsExplicitDriverLoad = MODSUPIsExplicitDriverLoad,
   };
   struct pvr_screen *screen = rzalloc(NULL, struct pvr_screen);
   struct pvr_config **configs;
   int num_dmabuf_formats;

   if (!screen)
      return NULL;

   screen->gpu_fd = fd;
   screen->display_fd = -1;
   screen->ro = ro;
   screen->driver_name_is_inferred = config->driver_name_is_inferred;

   screen->base.is_pvr = true;

   screen->base.destroy = pvr_destroy_screen;

   screen->base.get_screen_fd = pvr_get_screen_fd;
   screen->base.get_name = pvr_get_name;
   screen->base.get_vendor = pvr_get_vendor;
   screen->base.get_device_vendor = pvr_get_device_vendor;
   screen->base.get_driver_query_info = pvr_get_driver_query_info;
   screen->base.get_timestamp = u_default_get_timestamp;
   screen->base.is_format_supported = pvr_is_format_supported;
   screen->base.query_dmabuf_modifiers = pvr_query_dmabuf_modifiers;
   screen->base.is_dmabuf_modifier_supported = pvr_is_dmabuf_modifier_supported;
   screen->base.context_create_pvr = pvr_context_create_pvr;
   screen->base.api_query_versions_pvr = pvr_api_query_versions;
   screen->base.flush_pvr = pvr_flush_pvr;
   screen->base.get_current_dri_context = pvr_get_current_dri_context;
   screen->base.set_damage_region = pvr_set_damage_region;
   screen->base.fence_reference = pvr_fence_reference;
   screen->base.fence_finish = pvr_fence_finish;
   screen->base.fence_get_fd = pvr_fence_get_fd;
   screen->base.get_fence_from_cl_event = pvr_get_fence_from_cl_event;

   screen->base.drawable_create = pvr_drawable_create;
   screen->base.set_dri_image_params = pvr_set_dri_image_params;

   screen->base.check_driver_compatibility = pvr_check_driver_compatibility;

   if (!PVRDRICompatInit(&pvrdri_callbacks, 6, 0))
      return NULL;

   pipe_reference_init(&screen->ref, 1);

   pvr_resource_screen_init(screen);

   screen->drisup_screen = DRISUPCreateScreen((struct __DRIscreenRec *)screen,
                                              fd, true,
                                              (void *)screen,
                                              (const struct __DRIconfigRec ***)&configs,
                                              &screen->gles1_version,
                                              &screen->gles2_version);
   if (!screen->drisup_screen) {
      debug_printf("%s: Couldn't create DRISUP screen\n",
                   __func__);
      goto fail;
   }

   if (!DRISUPQueryDMABufFormats(screen->drisup_screen,
                                 0, NULL,
                                 &screen->num_dmabuf_formats)) {
      debug_printf("%s: Couldn't query number of dma-buf formats\n",
                   __func__);
      goto fail;
   }

   screen->dmabuf_formats = rzalloc_array_size(screen,
                                               sizeof(*screen->dmabuf_formats),
                                               screen->num_dmabuf_formats);
   if (!screen->dmabuf_formats) {
      debug_printf("%s: Failed to allocate memory for dma-buf formats\n",
                   __func__);
      goto fail;
   }

   if (!DRISUPQueryDMABufFormats(screen->drisup_screen,
                                 screen->num_dmabuf_formats,
                                 screen->dmabuf_formats,
                                 &num_dmabuf_formats)) {
      debug_printf("%s: Couldn't query dma-buf formats\n",
                   __func__);
      goto fail;
   }
   assert(screen->num_dmabuf_formats == num_dmabuf_formats);

   pvr_init_screen_caps(screen);

   return &screen->base;

fail:
   pvr_destroy_screen(&screen->base);
   return NULL;
}
