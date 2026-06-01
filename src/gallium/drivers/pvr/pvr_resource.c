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
#include <unistd.h>

#include "drm-uapi/drm_fourcc.h"
#include "mesa_interface.h"

#include "pipe/p_defines.h"

#include "util/os_file.h"
#include "util/ralloc.h"
#include "util/u_debug.h"
#include "util/u_resource.h"

#include "pvr/ddk/pvr_ddk_private.h"
#include "pvr/ddk/pvr_dri_compat.h"
#include "pvr/ddk/pvr_dri_support.h"
#include "pvr/ddk/pvr_dri_support_ddk.h"

#include "pvr_context.h"
#include "pvr_dri_callbacks.h"
#include "pvr_resource.h"
#include "pvr_screen.h"

struct pvr_transfer {
   struct pipe_transfer base;
   void *drisup_data;
};

static inline struct pvr_transfer *
pvr_transfer(struct pipe_transfer *p)
{
   return (struct pvr_transfer *)p;
}

int
pvr_pipe_format_to_fourcc(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_B5G6R5_UNORM:
      return DRM_FORMAT_RGB565;
   case PIPE_FORMAT_BGRX8888_UNORM:
      return DRM_FORMAT_XRGB8888;
   case PIPE_FORMAT_BGRA8888_UNORM:
      return DRM_FORMAT_ARGB8888;
   case PIPE_FORMAT_RGBA8888_UNORM:
      return DRM_FORMAT_ABGR8888;
   case PIPE_FORMAT_RGBX8888_UNORM:
      return DRM_FORMAT_XBGR8888;
   case PIPE_FORMAT_R8_UNORM:
      return DRM_FORMAT_R8;
   case PIPE_FORMAT_RG88_UNORM:
      return DRM_FORMAT_GR88;
   case PIPE_FORMAT_NONE:
      return 0;
   case PIPE_FORMAT_B10G10R10X2_UNORM:
      return DRM_FORMAT_XRGB2101010;
   case PIPE_FORMAT_B10G10R10A2_UNORM:
      return DRM_FORMAT_ARGB2101010;
   case PIPE_FORMAT_BGRA8888_SRGB:
      return __DRI_IMAGE_FOURCC_SARGB8888;
   case PIPE_FORMAT_B5G5R5A1_UNORM:
      return DRM_FORMAT_ARGB1555;
   case PIPE_FORMAT_R16_UNORM:
      return DRM_FORMAT_R16;
   case PIPE_FORMAT_RG1616_UNORM:
      return DRM_FORMAT_GR1616;
   case PIPE_FORMAT_YUYV:
      return DRM_FORMAT_YUYV;
   case PIPE_FORMAT_R10G10B10X2_UNORM:
      return DRM_FORMAT_XBGR2101010;
   case PIPE_FORMAT_R10G10B10A2_UNORM:
      return DRM_FORMAT_ABGR2101010;
   case PIPE_FORMAT_RGBA8888_SRGB:
      return __DRI_IMAGE_FOURCC_SABGR8888;
   case PIPE_FORMAT_UYVY:
      return DRM_FORMAT_UYVY;
   case PIPE_FORMAT_B4G4R4A4_UNORM:
      return DRM_FORMAT_ARGB4444;
   case PIPE_FORMAT_B8G8R8_UNORM:
      return DRM_FORMAT_RGB888;
   case PIPE_FORMAT_NV12:
      return DRM_FORMAT_NV12;
   case PIPE_FORMAT_NV21:
      return DRM_FORMAT_NV21;
   case PIPE_FORMAT_IYUV:
      return DRM_FORMAT_YUV420;
   case PIPE_FORMAT_YV12:
      return DRM_FORMAT_YVU420;
   case PIPE_FORMAT_YVYU:
      return DRM_FORMAT_YVYU;
   case PIPE_FORMAT_VYUY:
      return DRM_FORMAT_VYUY;
   case PIPE_FORMAT_Y8_U8_V8_444_UNORM:
      return DRM_FORMAT_YUV444;
   default:
      return 0;
   }
}

static void
pvr_resource_destroy_common(struct pvr_screen *screen,
                            struct pvr_resource *resource)
{
   pvr_screen_unref(screen);

   if (resource->export_fd != -1)
      close(resource->export_fd);

   ralloc_free(resource);
}

static struct pvr_resource *
pvr_resource_create_common(struct pvr_screen *screen,
                           const struct pipe_resource *template,
                           int *fourcc_out,
                           unsigned int *usage_out,
                           unsigned int *flags_out)
{
   struct pvr_resource *resource;
   unsigned int usage = 0;
   unsigned int flags = 0;

   resource = rzalloc(NULL, struct pvr_resource);
   if (!resource)
      return NULL;

   if (template)
      resource->base = *template;

   resource->base.screen = &screen->base;

   pipe_reference_init(&resource->base.reference, 1);

   resource->export_fd = -1;

   if (resource->base.bind & ~(PIPE_BIND_RENDER_TARGET |
                               PIPE_BIND_SAMPLER_VIEW |
                               PIPE_BIND_DISPLAY_TARGET |
                               PIPE_BIND_CURSOR |
                               PIPE_BIND_SHARED |
                               PIPE_BIND_SCANOUT |
                               PIPE_BIND_LINEAR |
                               PIPE_BIND_PROTECTED |
                               PIPE_BIND_PRIME_BLIT_DST |
                               PIPE_BIND_USE_FRONT_RENDERING)) {
      debug_printf("%s: Unsupported resource binding flags: 0x%x\n",
                    __func__, (unsigned int)(template->bind));
      goto exit_fail;
   }

   if (resource->base.bind & PIPE_BIND_DISPLAY_TARGET)
      usage |= PVRDRI_BUFFER_USE_SCANOUT;

   if (resource->base.bind & PIPE_BIND_CURSOR)
      usage |= PVRDRI_BUFFER_USE_CURSOR;

   if (resource->base.bind & PIPE_BIND_SHARED)
      usage |= PVRDRI_BUFFER_USE_SHARE;

   if (resource->base.bind & PIPE_BIND_SCANOUT)
      usage |= PVRDRI_BUFFER_USE_SCANOUT;

   if (resource->base.bind & PIPE_BIND_LINEAR)
      usage |= PVRDRI_BUFFER_USE_LINEAR;

   if (resource->base.bind & PIPE_BIND_PROTECTED) {
      usage |= PVRDRI_BUFFER_USE_PROTECTED;
      flags |= PVRDRI_IMAGE_PROTECTED_CONTENT_FLAG;
   }

   if (resource->base.bind & PIPE_BIND_PRIME_BLIT_DST)
      flags |= PVRDRI_IMAGE_PRIME_LINEAR_BUFFER;

   if (fourcc_out) {
      if (template)
         *fourcc_out = pvr_pipe_format_to_fourcc(template->format);
      else
         *fourcc_out = DRM_FORMAT_INVALID;
   }

   if (usage_out)
      *usage_out = usage;

   if (flags_out)
      *flags_out = flags;

   pvr_screen_ref(screen);

   return resource;

exit_fail:
   ralloc_free(resource);

   return NULL;
}

static void
pvr_query_drisup_image_int(__DRIimage *image, int attrib, int *value)
{
   bool ret;

   *value = 0;

   ret = DRISUPQueryImage(image, attrib, value);

   assert(ret);
}

static int
pvr_get_plane_export_fd(__DRIimage *drisup_image, int import_fd)
{
   int export_fd = -1;

   if (import_fd != -1) {
      if (!DRISUPQueryImage(drisup_image,
                            PVRDRI_IMAGE_ATTRIB_FD, &export_fd)) {
         debug_printf("%s: couldn't get export FD, saving dup of import FD\n",
                       __func__);

         export_fd = os_dupfd_cloexec(import_fd);
         if (export_fd == -1)
            debug_printf("%s: couldn't duplicate import FD\n", __func__);
      } else {
         close(export_fd);
         export_fd = -1;
      }
   }

   return export_fd;
}

/* PVR Services does not support the re-exporting of resources imported from
 * other drivers. The DRI Support library tries to work around this by
 * importing such resources into the display driver, but this may fail if
 * the resource isn't compatible with the display driver, and isn't possible
 * anyway if the library doesn't have a display driver available to it.
 * If it isn't possible to export a resource, save a duplicate of the FD used
 * to import the resource, which can then itself be duplicated if the resource
 * needs to be exported.
 */
static void
pvr_fixup_image_export(struct pvr_resource *resource, const int *fds, int plane)
{
   if (fds != NULL) {
      const int import_fd = fds[plane];
      __DRIimage *drisup_image;

      /* The first image in a multi-plane resource represents the whole
       * resource, on which FD queries are not supported, so create a subimage
       * of the first plane for such queries.
       */
      if (plane == 0 && util_format_is_yuv(resource->base.format)) {
         drisup_image = DRISUPFromPlanar(resource->drisup_image, plane,
                                         NULL);
         if (!drisup_image) {
            debug_printf("%s: DRISUPFromPlanar failed\n", __func__);
            return;
         }
      } else {
         drisup_image = resource->drisup_image;
      }

      resource->export_fd = pvr_get_plane_export_fd(drisup_image, import_fd);

      if (drisup_image != resource->drisup_image)
         DRISUPDestroyImage(drisup_image);
   }
}

static struct pipe_resource *
pvr_resource_add_sub_resources(struct pvr_screen *screen,
                               const struct pipe_resource *template,
                               struct pvr_resource *resource,
                               int *fds)
{
   struct pipe_resource *presource = &resource->base;
   struct pvr_resource *prev;
   int num_planes, plane;
   int width, height;

   pvr_query_drisup_image_int(resource->drisup_image,
                              PVRDRI_IMAGE_ATTRIB_NUM_PLANES, &num_planes);

   for (plane = 1, prev = resource; plane < num_planes; plane++) {
      struct  pvr_resource *next;

      next = pvr_resource_create_common(screen, template,
                                        NULL, NULL, NULL);
      if (!next)
         goto exit_fail;

      next->drisup_image = DRISUPFromPlanar(resource->drisup_image, plane,
                                            NULL);
      if (!next->drisup_image) {
         debug_printf("%s: DRISUPFromPlanar failed\n", __func__);

         pvr_resource_destroy_common(screen, next);
         goto exit_fail;
      }

      pvr_query_drisup_image_int(next->drisup_image,
                                 PVRDRI_IMAGE_ATTRIB_WIDTH, &width);
      next->base.width0 = width;

      pvr_query_drisup_image_int(next->drisup_image,
                                 PVRDRI_IMAGE_ATTRIB_HEIGHT, &height);
      next->base.height0 = height;

      pvr_fixup_image_export(next, fds, plane);

      prev->base.next = &next->base;
      prev = next;
   }

   return presource;

exit_fail:
   pipe_resource_reference(&presource, NULL);
   return NULL;
}

static struct pipe_resource *
pvr_resource_create_with_modifiers(struct pipe_screen *pscreen,
                                   const struct pipe_resource *template,
                                   const uint64_t *modifiers, int count)
{
   struct pvr_screen *screen = pvr_screen(pscreen);
   struct pvr_resource *resource;
   int fourcc;
   unsigned int usage;
   unsigned int flags;

   resource = pvr_resource_create_common(screen, template,
                                         &fourcc, &usage, &flags);
   if (!resource)
      return NULL;

   if (modifiers != NULL && count !=0)
      resource->drisup_image =
         DRISUPCreateImageWithModifiers2(screen->drisup_screen,
                                         template->width0,
                                         template->height0,
                                         fourcc,
                                         modifiers,
                                         count,
                                         usage,
                                         NULL);
   else
      resource->drisup_image =
         DRISUPCreateImage(screen->drisup_screen,
                           template->width0,
                           template->height0,
                           fourcc,
                           usage,
                           NULL);
   if (!resource->drisup_image) {
      debug_printf("%s: DRISUPCreateImage failed\n", __func__);
      goto exit_fail;
   }

   return pvr_resource_add_sub_resources(screen, template, resource, NULL);

exit_fail:
   pvr_resource_destroy_common(screen, resource);

   return NULL;
}

static struct pipe_resource *
pvr_resource_create(struct pipe_screen *pscreen,
                    const struct pipe_resource *template)
{
   return pvr_resource_create_with_modifiers(pscreen, template, NULL, 0);
}

static void
pvr_resource_destroy(struct pipe_screen *pscreen,
                     struct pipe_resource *presource)
{
   struct pvr_screen *screen = pvr_screen(pscreen);
   struct pvr_resource *resource = pvr_resource(presource);

   DRISUPDestroyImage(resource->drisup_image);

   pvr_resource_destroy_common(screen, resource);
}

static unsigned int
pvr_dri_yuv_color_space(unsigned int dri_yuv_color_space)
{
   switch (dri_yuv_color_space) {
   case __DRI_YUV_COLOR_SPACE_UNDEFINED:
      return PVRDRI_YUV_COLOR_SPACE_UNDEFINED;
   case __DRI_YUV_COLOR_SPACE_ITU_REC601:
      return PVRDRI_YUV_COLOR_SPACE_ITU_REC601;
   case __DRI_YUV_COLOR_SPACE_ITU_REC709:
      return PVRDRI_YUV_COLOR_SPACE_ITU_REC709;
   case __DRI_YUV_COLOR_SPACE_ITU_REC2020:
      return PVRDRI_YUV_COLOR_SPACE_ITU_REC2020;
   default:
      debug_printf("%s: Unknown DRI YUV color space: %u\n",
                    __func__, dri_yuv_color_space);
      return PVRDRI_YUV_COLOR_SPACE_UNDEFINED;
   }
}

static unsigned int
pvr_dri_yuv_sample_range(unsigned int dri_yuv_sample_range)
{
   switch (dri_yuv_sample_range) {
   case __DRI_YUV_RANGE_UNDEFINED:
      return PVRDRI_YUV_RANGE_UNDEFINED;
   case __DRI_YUV_FULL_RANGE:
      return PVRDRI_YUV_FULL_RANGE;
   case __DRI_YUV_NARROW_RANGE:
      return PVRDRI_YUV_NARROW_RANGE;
   default:
      debug_printf("%s: Unknown DRI YUV sample range: %u\n",
                    __func__, dri_yuv_sample_range);
      return PVRDRI_YUV_RANGE_UNDEFINED;
   }
}

static unsigned int
pvr_dri_yuv_chroma_siting(unsigned int dri_yuv_chroma_siting)
{
   switch (dri_yuv_chroma_siting)
   {
   case __DRI_YUV_CHROMA_SITING_UNDEFINED:
      return PVRDRI_YUV_CHROMA_SITING_UNDEFINED;
   case __DRI_YUV_CHROMA_SITING_0:
      return PVRDRI_YUV_CHROMA_SITING_0;
   case __DRI_YUV_CHROMA_SITING_0_5:
      return PVRDRI_YUV_CHROMA_SITING_0_5;
   default:
      debug_printf("%s: Unknown DRI YUV chroma siting: %u\n",
                    __func__, dri_yuv_chroma_siting);
      return PVRDRI_YUV_CHROMA_SITING_UNDEFINED;
   }
}

static struct pipe_resource *
pvr_resource_from_fds(struct pipe_screen *pscreen,
                      const struct pipe_resource *template,
                      uint64_t modifier, int *fds, int num_fds,
                      int *strides, int *offsets,
                      unsigned int yuv_color_space,
                      unsigned int sample_range,
                      unsigned int horizontal_siting,
                      unsigned int vertical_siting)
{
   struct pvr_screen *screen = pvr_screen(pscreen);
   struct pvr_resource *resource;
   int fourcc;
   unsigned int usage;
   unsigned int flags;
   unsigned int error;
   int width, height;

   resource = pvr_resource_create_common(screen, template,
                                         &fourcc, &usage, &flags);
   if (!resource)
      return NULL;

   resource->drisup_image =
      DRISUPCreateImageFromDMABufs3(screen->drisup_screen,
                                    template->width0,
                                    template->height0,
                                    fourcc,
                                    modifier,
                                    fds,
                                    num_fds,
                                    strides,
                                    offsets,
                                    pvr_dri_yuv_color_space(yuv_color_space),
                                    pvr_dri_yuv_sample_range(sample_range),
                                    pvr_dri_yuv_chroma_siting(horizontal_siting),
                                    pvr_dri_yuv_chroma_siting(vertical_siting),
                                    flags,
                                    &error,
                                    NULL);
   if (!resource->drisup_image) {
      debug_printf("%s: DRISUPCreateImageFromDMABufs3 failed: %u\n",
                    __func__, error);
      goto exit_fail;
   }

   pvr_query_drisup_image_int(resource->drisup_image,
                                 PVRDRI_IMAGE_ATTRIB_WIDTH, &width);
   resource->base.width0 = width;

   pvr_query_drisup_image_int(resource->drisup_image,
                                 PVRDRI_IMAGE_ATTRIB_HEIGHT, &height);
   resource->base.height0 = height;

   pvr_fixup_image_export(resource, fds, 0);

   return pvr_resource_add_sub_resources(screen, template, resource, fds);

exit_fail:
   pvr_resource_destroy_common(screen, resource);

   return NULL;
}

static struct pipe_resource *
pvr_resource_from_handle(struct pipe_screen *pscreen,
                         const struct pipe_resource *template,
                         struct winsys_handle *whandle,
                         unsigned int pusage)
{
   uint64_t modifier = whandle->modifier;
   int fd = whandle->handle;
   int stride = whandle->stride;
   int offset = whandle->offset;

   assert(whandle->type == WINSYS_HANDLE_TYPE_FD);

   return pvr_resource_from_fds(pscreen, template, modifier,
                                &fd, 1, &stride, &offset,
                                PVRDRI_YUV_COLOR_SPACE_UNDEFINED,
                                PVRDRI_YUV_RANGE_UNDEFINED,
                                PVRDRI_YUV_CHROMA_SITING_UNDEFINED,
                                PVRDRI_YUV_CHROMA_SITING_UNDEFINED);
}

static bool
pvr_resource_get_handle(struct pipe_screen *pscreen,
                        struct pipe_context *ctx,
                        struct pipe_resource *pt,
                        struct winsys_handle *handle,
                        unsigned int usage)
{
   struct pipe_resource *presource = util_resource_at_index(pt, handle->plane);
   struct pvr_resource *resource = pvr_resource(presource);
   bool ret = false;
   __DRIimage *drisup_image;
   int value;

   /* The first plane in a multi-plane resource is actually the whole
    * resource, on which some queries are not supported, so create
    * a subimage for the first plane for such queries.
    */
   if (handle->plane == 0 && util_format_is_yuv(presource->format)) {
      drisup_image = DRISUPFromPlanar(resource->drisup_image, handle->plane,
                                      NULL);
      if (!drisup_image) {
         debug_printf("%s: DRISUPFromPlanar failed\n", __func__);
         return false;
      }
   } else {
      drisup_image = resource->drisup_image;
   }

   switch (handle->type) {
   case WINSYS_HANDLE_TYPE_KMS:
      if (!DRISUPQueryImage(drisup_image,
                            PVRDRI_IMAGE_ATTRIB_HANDLE, &value))
         goto fail;

      handle->handle = value;
      break;
   case WINSYS_HANDLE_TYPE_FD:
      if (resource->export_fd != -1) {
         value = os_dupfd_cloexec(resource->export_fd);
         if (value == -1)
            goto fail;
      } else if (!DRISUPQueryImage(drisup_image,
                                   PVRDRI_IMAGE_ATTRIB_FD, &value)) {
            goto fail;
      }

      handle->handle = value;
      break;
   default:
      debug_printf("%s: Unknown handle type: %u\n",
                    __func__, handle->type);
      goto fail;
   }

   ret = DRISUPQueryImage(drisup_image,
                          PVRDRI_IMAGE_ATTRIB_MODIFIER_UPPER,
                          &value);
   assert(ret);
   handle->modifier = (uint64_t)value << 32;

   ret = DRISUPQueryImage(drisup_image,
                          PVRDRI_IMAGE_ATTRIB_MODIFIER_LOWER,
                          &value);
   assert(ret);
   handle->modifier |= (uint32_t)value;

   ret = DRISUPQueryImage(drisup_image,
                          PVRDRI_IMAGE_ATTRIB_STRIDE,
                          &value);
   assert(ret);
   handle->stride = value;

   ret = DRISUPQueryImage(drisup_image,
                          PVRDRI_IMAGE_ATTRIB_OFFSET,
                          &value);
   assert(ret);
   handle->offset = value;

fail:
   if (drisup_image != resource->drisup_image)
      DRISUPDestroyImage(drisup_image);

   return ret;
}

static bool
pvr_resource_query_image(__DRIimage *drisup_image,
                         int attrib,
                         uint64_t *pvalue)
{
   int value;
   bool ret;

   ret = DRISUPQueryImage(drisup_image, attrib, &value);

   /* Avoid sign extension when casting to uint64_t by casting to uint32_t */
   if (ret)
      *pvalue = (uint32_t)value;

   return ret;
}

static bool
pvr_resource_get_param(struct pipe_screen *pscreen,
                       struct pipe_context *pctx,
                       struct pipe_resource *prsc, unsigned plane,
                       unsigned layer, unsigned level,
                       enum pipe_resource_param param, unsigned usage,
                       uint64_t *pvalue)
{
   struct pipe_resource *presource = util_resource_at_index(prsc, plane);
   struct pvr_resource *resource = pvr_resource(presource);
   uint64_t value = 0;
   __DRIimage *drisup_image;
   bool ret;

   /* The first plane in a multi-plane resource is actually the whole
    * resource, on which some queries are not supported, so create
    * a subimage for the first plane for such queries.
    */
   if (plane == 0 && util_format_is_yuv(presource->format)) {
      switch (param) {
      case PIPE_RESOURCE_PARAM_OFFSET:
         drisup_image = DRISUPFromPlanar(resource->drisup_image, plane, NULL);
         if (!drisup_image) {
            debug_printf("%s: DRISUPFromPlanar failed\n", __func__);
            return false;
         }
         break;
      case PIPE_RESOURCE_PARAM_HANDLE_TYPE_FD:
      case PIPE_RESOURCE_PARAM_HANDLE_TYPE_KMS:
      case PIPE_RESOURCE_PARAM_HANDLE_TYPE_SHARED:
         /* The above queries are not supported by this function. If they
          * ever were to be, they should be moved so as to share the code
          * for PIPE_RESOURCE_PARAM_OFFSET above.
          */
      default:
         drisup_image = resource->drisup_image;
         break;
      }
   } else {
      drisup_image = resource->drisup_image;
   }

   switch (param) {
   case PIPE_RESOURCE_PARAM_STRIDE:
      ret = pvr_resource_query_image(drisup_image,
                                     PVRDRI_IMAGE_ATTRIB_STRIDE,
                                     &value);
      break;
   case PIPE_RESOURCE_PARAM_OFFSET:
      ret = pvr_resource_query_image(drisup_image,
                                     PVRDRI_IMAGE_ATTRIB_OFFSET,
                                     &value);
      break;
   case PIPE_RESOURCE_PARAM_MODIFIER:
   {
      uint64_t mod_hi = 0;

      ret = pvr_resource_query_image(drisup_image,
                                     PVRDRI_IMAGE_ATTRIB_MODIFIER_UPPER,
                                     &mod_hi);

      ret &= pvr_resource_query_image(drisup_image,
                                      PVRDRI_IMAGE_ATTRIB_MODIFIER_LOWER,
                                      &value);
      value |= mod_hi << 32;

      break;
   }
   case PIPE_RESOURCE_PARAM_NPLANES:
      ret = pvr_resource_query_image(drisup_image,
                                     PVRDRI_IMAGE_ATTRIB_NUM_PLANES,
                                     &value);
      break;
   default:
      ret = false;
      break;
   }

   if (ret)
      *pvalue = value;

   if (drisup_image != resource->drisup_image)
      DRISUPDestroyImage(drisup_image);

   return ret;
}

static void
pvr_flush_resource(struct pipe_context *pcontext,
                   struct pipe_resource *presource)
{
}

static void
pvr_invalidate_resource(struct pipe_context *pcontext,
                        struct pipe_resource *presource)
{
}

static unsigned int
pvr_image_error_from_drisup_error(unsigned int error)
{
   switch (error) {
   case PVRDRI_IMAGE_ERROR_SUCCESS:
      return __DRI_IMAGE_ERROR_SUCCESS;
   case PVRDRI_IMAGE_ERROR_BAD_ALLOC:
      return __DRI_IMAGE_ERROR_BAD_ALLOC;
   case PVRDRI_IMAGE_ERROR_BAD_MATCH:
      return __DRI_IMAGE_ERROR_BAD_MATCH;
   case PVRDRI_IMAGE_ERROR_BAD_PARAMETER:
      return __DRI_IMAGE_ERROR_BAD_PARAMETER;
   case PVRDRI_IMAGE_ERROR_BAD_ACCESS:
      return __DRI_IMAGE_ERROR_BAD_ACCESS;
   default:
      debug_printf("%s: Unknown PVR DRI Image error: %d\n",
                   __func__, error);
      return __DRI_IMAGE_ERROR_BAD_PARAMETER;
   }
}

static struct pipe_resource *
pvr_resource_from_texture(struct pipe_context *ctx,
                          int target, unsigned int texture,
                          int depth, int level,
                          unsigned int *error)
{
   struct pvr_context *context = pvr_context(ctx);
   struct pvr_screen *screen = pvr_screen(ctx->screen);
   int drisup_target;
   struct __DRIimageRec *drisup_image;
   struct pvr_resource *resource;
   unsigned int drisup_error;

   switch (target) {
   case GL_TEXTURE_2D:
      drisup_target = PVRDRI_GL_TEXTURE_2D;
      break;
   case GL_TEXTURE_3D:
      drisup_target = PVRDRI_GL_TEXTURE_3D;
      break;
   case GL_TEXTURE_CUBE_MAP:
      drisup_target = PVRDRI_GL_TEXTURE_CUBE_MAP_POSITIVE_X;
      break;
   default:
      debug_printf("%s: Target %d not supported\n",
                   __func__, target);
      *error = __DRI_IMAGE_ERROR_BAD_PARAMETER;
      return NULL;
   }

   drisup_image = DRISUPCreateImageFromTexture(context->drisup_context,
                                               drisup_target, texture, depth,
                                               level, &drisup_error, NULL);
   if (!drisup_image) {
      debug_printf("%s: DRISUPCreateImageFromTexture failed: %d\n",
                   __func__, drisup_error);
      *error = pvr_image_error_from_drisup_error(drisup_error);
      return NULL;
   }

   resource = pvr_resource_create_common(screen, NULL, NULL, NULL, NULL);
   if (!resource) {
      goto exit_destroy_image;
   }

   resource->drisup_image = drisup_image;

   *error = __DRI_IMAGE_ERROR_SUCCESS;
   return &resource->base;

exit_destroy_image:
   DRISUPDestroyImage(drisup_image);

   return NULL;
}

static struct pipe_resource *
pvr_resource_from_renderbuffer(struct pipe_context *ctx,
                               int renderbuffer,
                               unsigned int *error)
{
   struct pvr_context *context = pvr_context(ctx);
   struct pvr_screen *screen = pvr_screen(ctx->screen);
   struct __DRIimageRec *drisup_image;
   struct pvr_resource *resource;
   unsigned int drisup_error;

   drisup_image = DRISUPCreateImageFromRenderBuffer2(context->drisup_context,
                                                     renderbuffer,
                                                     NULL, &drisup_error);
   if (!drisup_image) {
      debug_printf("%s: DRISUPCreateImageFromRenderBuffer2 failed: %d\n",
                   __func__, drisup_error);
      *error = pvr_image_error_from_drisup_error(drisup_error);
      return NULL;
   }

   resource = pvr_resource_create_common(screen, NULL, NULL, NULL, NULL);
   if (!resource) {
      goto exit_destroy_image;
   }

   resource->drisup_image = drisup_image;

   *error = __DRI_IMAGE_ERROR_SUCCESS;
   return &resource->base;

exit_destroy_image:
   DRISUPDestroyImage(drisup_image);

   return NULL;
}

__DRIimage *
MODSUPLookupEGLImage(struct __DRIscreenRec *psDRIScreen,
                     void *pvImage,
                     void *pvLoaderPrivate)
{
   struct pvr_screen *screen = (struct pvr_screen *)psDRIScreen;
   void *loader_private = screen->dri_screen_loader_private;
   struct pvr_resource *resource;
   struct dri_image *img;

   if (!screen->validateEGLImage || !screen->lookupEGLImageValidated)
      return NULL;

   if (!screen->validateEGLImage(pvImage, loader_private))
      return NULL;

   img = screen->lookupEGLImageValidated(pvImage, loader_private);

   resource = pvr_resource(screen->resource_from_image(img));

   return resource->drisup_image;
}

static void
pvr_blit_image(struct pipe_context *pctx,
               struct pipe_resource *pdst, struct pipe_resource *psrc,
               int dstx0, int dsty0, int dstwidth, int dstheight,
               int srcx0, int srcy0, int srcwidth, int srcheight,
               int flush_flag)
{
   struct pvr_context *context = pvr_context(pctx);
   struct pvr_resource *dst = pvr_resource(pdst);
   struct pvr_resource *src = pvr_resource(psrc);
   int drisup_flush_flags;

   switch (flush_flag) {
   case __BLIT_FLAG_FLUSH:
      drisup_flush_flags = PVRDRI_BLIT_FLAG_FLUSH;
      break;
   case __BLIT_FLAG_FINISH:
      drisup_flush_flags = PVRDRI_BLIT_FLAG_FINISH;
      break;
   default:
      drisup_flush_flags = 0;
      break;
   }

   DRISUPBlitImage(context->drisup_context,
                   dst->drisup_image, src->drisup_image,
                   dstx0, dsty0, dstwidth, dstheight,
                   srcx0, srcy0, srcwidth, srcheight,
                   drisup_flush_flags);
}

static void *
pvr_texture_map(struct pipe_context *pctx,
                struct pipe_resource *prsc,
                unsigned int level, unsigned int usage,
                const struct pipe_box *box,
                struct pipe_transfer **pptrans)
{
   struct pvr_context *context = pvr_context(pctx);
   struct pvr_resource *resource = pvr_resource(prsc);
   struct pvr_transfer *transfer;
   unsigned int drisup_flags = 0;
   int stride;
   void *map;

   if (level) {
      debug_printf("%s: level %u not supported\n", __func__, level);

      return NULL;
   }

   if (usage & PIPE_MAP_READ)
      drisup_flags |= PVRDRI_IMAGE_TRANSFER_READ;
   if (usage & PIPE_MAP_WRITE)
      drisup_flags |= PVRDRI_IMAGE_TRANSFER_WRITE;

   transfer = rzalloc(resource, struct pvr_transfer);

   pipe_resource_reference(&transfer->base.resource, prsc);
   transfer->base.level = level;
   transfer->base.usage = usage;
   transfer->base.box   = *box;

   map = DRISUPMapImage(context->drisup_context,
                        resource->drisup_image,
                        box->x, box->y, box->width, box->height,
                        drisup_flags, &stride, &transfer->drisup_data);

   if (!map) {
      debug_printf("%s: DRISUPMapImage failed\n", __func__);

      pipe_resource_reference(&transfer->base.resource, NULL);
      ralloc_free(transfer);

      return NULL;
   }

   transfer->base.stride = stride;
   transfer->base.layer_stride = (uint64_t)stride * box->height;

   *pptrans = &transfer->base;

   return map;
}

static void
pvr_texture_unmap(struct pipe_context *pctx,
                  struct pipe_transfer *ptrans)
{
   struct pvr_transfer *transfer = pvr_transfer(ptrans);
   struct pvr_context *context = pvr_context(pctx);
   struct pvr_resource *resource = pvr_resource(transfer->base.resource);

   DRISUPUnmapImage(context->drisup_context,
                    resource->drisup_image, transfer->drisup_data);

   pipe_resource_reference(&transfer->base.resource, NULL);

   ralloc_free(transfer);
}

void
pvr_resource_screen_init(struct pvr_screen *screen)
{
   screen->base.resource_create_with_modifiers =
      pvr_resource_create_with_modifiers;
   screen->base.resource_create = pvr_resource_create;
   screen->base.resource_destroy = pvr_resource_destroy;
   screen->base.resource_from_handle = pvr_resource_from_handle;
   screen->base.resource_from_fds = pvr_resource_from_fds;
   screen->base.resource_get_handle = pvr_resource_get_handle;
   screen->base.resource_get_param = pvr_resource_get_param;
}

void
pvr_resource_context_init(struct pvr_context *context)
{
   context->base.flush_resource = pvr_flush_resource;
   context->base.invalidate_resource = pvr_invalidate_resource;
   context->base.resource_from_texture = pvr_resource_from_texture;
   context->base.resource_from_renderbuffer = pvr_resource_from_renderbuffer;
   context->base.blit_image = pvr_blit_image;
   context->base.texture_map = pvr_texture_map;
   context->base.texture_unmap = pvr_texture_unmap;
}

void
pvr_resource_screen_destroy(struct pvr_screen *screen)
{
}
