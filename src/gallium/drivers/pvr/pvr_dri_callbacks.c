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

#include <stdlib.h>
#include <string.h>

#include "drm-uapi/drm_fourcc.h"

#include "util/macros.h"
#include "util/ralloc.h"

#include "pvr/ddk/pvr_dri_support.h"
#include "pvr/ddk/pvr_dri_support_ddk.h"

#include "pvr_resource.h"
#include "pvr_screen.h"
#include "pvr_dri_callbacks.h"
#include "pvr_dri_callbacks_util.h"

#include "pvr/common/pvr_common_display.h"

bool
MODSUPCreateConfigs(struct __DRIconfigRec ***pppsConfigs,
                    struct __DRIscreenRec *psDRIScreen,
                    int iPVRDRIMesaFormat,
                    const uint8_t *puDepthBits,
                    const uint8_t *puStencilBits,
                    unsigned int uNumDepthStencilBits,
                    const unsigned int *puDBModes,
                    unsigned int uNumDBModes,
                    const uint8_t *puMSAASamples,
                    unsigned int uNumMSAAModes,
                    bool bEnableAccum,
                    bool bColorDepthMatch,
                    bool bMutableRenderBuffer,
                    int iYUVDepthRange,
                    int iYUVCSCStandard,
                    uint32_t uMaxPbufferWidth,
                    uint32_t uMaxPbufferHeight)
{
   struct pvr_screen *screen = (struct pvr_screen *)psDRIScreen;

   if (!screen->config) {
      screen->config = rzalloc(screen, struct pvr_config);
      if (!screen->config)
         return false;
   }

   if (iPVRDRIMesaFormat == 0 || iPVRDRIMesaFormat > PVRDRI_MESA_FORMAT_MAX)
      goto exit_ok;

   /* Non-YUV data should be the same for each format, so we only store one
    * instance of the data.
    * We will be called multiple times for each YUV format, one for each
    * combination of iYUVDepthRange and iYUVCSCStandard, neither of which
    * we store.
    */
   screen->config->have_mesa_format[iPVRDRIMesaFormat] = true;

   if (!screen->config->depth_bits && uNumDepthStencilBits != 0) {
      screen->config->depth_bits = ralloc_array(screen->config, uint8_t,
                                                uNumDepthStencilBits);
      if (!screen->config->depth_bits)
         return false;

      memcpy(screen->config->depth_bits, puDepthBits,
             uNumDepthStencilBits * sizeof(*screen->config->depth_bits));

      screen->config->num_depth_stencil_bits = uNumDepthStencilBits;
   } else if (screen->config->num_depth_stencil_bits != uNumDepthStencilBits) {
      debug_printf("%s: Number of depth bits mismatch (%u != %u)\n", __func__,
                   screen->config->num_depth_stencil_bits,
                   uNumDepthStencilBits);
      return false;
   }

   if (!screen->config->stencil_bits && uNumDepthStencilBits != 0) {
      if (screen->config->num_depth_stencil_bits != 0 &&
          screen->config->num_depth_stencil_bits != uNumDepthStencilBits) {

         debug_printf("%s: Number of depth stencil bits mismatch (%u != %u)\n",
                      __func__, screen->config->num_depth_stencil_bits,
                      uNumDepthStencilBits);
         return false;
      }

      screen->config->stencil_bits = ralloc_array(screen->config, uint8_t,
                                                  uNumDepthStencilBits);
      if (!screen->config->stencil_bits)
         return false;

      memcpy(screen->config->stencil_bits, puStencilBits,
             uNumDepthStencilBits * sizeof(*screen->config->stencil_bits));

      screen->config->num_depth_stencil_bits = uNumDepthStencilBits;
   }


   if (!screen->config->db_modes && uNumDBModes != 0) {
      screen->config->db_modes = ralloc_array(screen->config, unsigned int,
                                              uNumDBModes);
      if (!screen->config->db_modes)
         return false;

      memcpy(screen->config->db_modes, puDBModes,
             uNumDBModes * sizeof(*screen->config->db_modes));

      screen->config->num_db_modes = uNumDBModes;
   } else if (screen->config->num_db_modes != uNumDBModes) {
      debug_printf("%s: Number of DB modes mismatch (%u != %u)\n", __func__,
                   screen->config->num_db_modes,
                   uNumDBModes);
      return false;
   }

   if (!screen->config->msaa_samples && uNumMSAAModes != 0) {
      screen->config->msaa_samples = ralloc_array(screen->config, uint8_t,
                                                  uNumMSAAModes);
      if (!screen->config->msaa_samples)
         return false;

      memcpy(screen->config->msaa_samples, puMSAASamples,
             uNumMSAAModes * sizeof(*screen->config->msaa_samples));

      screen->config->num_msaa_modes = uNumMSAAModes;
   } else if (screen->config->num_msaa_modes != uNumMSAAModes) {
      debug_printf("%s: Number of MSAA modes mismatch (%u != %u)\n", __func__,
                   screen->config->num_msaa_modes,
                   uNumMSAAModes);
      return false;
   }

   if (screen->config->max_pbuffer_width != 0 ||
       screen->config->max_pbuffer_height != 0) {

      if (screen->config->enable_accum != bEnableAccum) {
         debug_printf("%s: Enable Accumulation mismatch (%d != %d)\n",
                      __func__,
                      (int)screen->config->enable_accum,
                      (int)bEnableAccum);
         return false;
      }

      if (screen->config->color_depth_match != bColorDepthMatch) {
         debug_printf("%s: Color Depth Match mismatch (%d != %d)\n",
                      __func__,
                      (int)screen->config->color_depth_match,
                      (int)bColorDepthMatch);
         return false;
      }

      if (screen->config->mutable_render_buffer != bMutableRenderBuffer) {
         debug_printf("%s: Mutable Render Buffer mismatch (%d != %d)\n",
                      __func__,
                      (int)screen->config->mutable_render_buffer,
                      (int)bMutableRenderBuffer);
         return false;
      }

      if (screen->config->max_pbuffer_width != uMaxPbufferWidth) {
         debug_printf("%s: Max Pbuffer Width mismatch (%u != %u)\n",
                      __func__,
                      (unsigned int)screen->config->max_pbuffer_width,
                      (unsigned int)uMaxPbufferWidth);
         return false;
      }

      if (screen->config->max_pbuffer_height != uMaxPbufferHeight) {
         debug_printf("%s: Max Pbuffer Height mismatch (%u != %u)\n",
                      __func__,
                      (unsigned int)screen->config->max_pbuffer_height,
                      (unsigned int)uMaxPbufferHeight);
         return false;
      }
   }

   screen->config->enable_accum = bEnableAccum;
   screen->config->color_depth_match = bColorDepthMatch;
   screen->config->mutable_render_buffer = bMutableRenderBuffer;
   screen->config->max_pbuffer_width = uMaxPbufferWidth;
   screen->config->max_pbuffer_height = uMaxPbufferHeight;

exit_ok:
   *pppsConfigs = (struct __DRIconfigRec **)&screen->config;
   return true;
}

struct __DRIconfigRec **
MODSUPConcatConfigs(struct __DRIscreenRec *psDRIScreen,
                    struct __DRIconfigRec **ppsConfigA,
                    struct __DRIconfigRec **ppsConfigB)
{
   struct pvr_screen *screen = (struct pvr_screen *)psDRIScreen;

   return (struct __DRIconfigRec **)&screen->config;
}

unsigned int
MODSUPGetCapability(struct __DRIscreenRec *psDRIScreen,
                    unsigned int uCapability)
{
   switch (uCapability) {
   case PVRDRI_LOADER_CAP_RGBA_ORDERING:
   case PVRDRI_LOADER_CAP_YUV_SURFACE_IMG:
      return 1;
   default:
      debug_printf("%s: Capability %u not supported\n", __func__, uCapability);
      return 0;
   }
}

int
MODSUPGetDisplayFD(struct __DRIscreenRec *psDRIScreen,
                   void *pvLoaderPrivate)
{
   struct pvr_screen *screen = (struct pvr_screen *)psDRIScreen;

   if (screen->display_fd != -1) {
      return screen->display_fd;
   } else {
      const char *display_driver_name = DRISUPGetDisplayDriverName();

      if (display_driver_name == NULL) {
         return screen->ro ? screen->ro->kms_fd : -1;
      } else {
         int fd = screen->ro ? screen->ro->kms_fd : screen->gpu_fd;

         screen->display_fd = pvr_common_query_compatible_display_device_fd(fd,
                                 display_driver_name);

         return screen->display_fd;
      }
   }
}

void
MODSUPDestroyLoaderImageState(const struct __DRIscreenRec *psDRIScreen,
                              void *pvLoaderPrivate)
{
}

bool
MODSUPIsExplicitDriverLoad(const struct __DRIscreenRec *psDRIScreen)
{
   const char *loader_override = getenv("MESA_LOADER_DRIVER_OVERRIDE");

   /*
    * No need to check the value; if there is an override, and this
    * driver is loaded, the override must have been used to force this
    * driver to be loaded.
    * Mesa does not treat an empty override string differently to a
    * non-empty one.
    *
    * It was hoped that the driver_name_is_inferred field in the
    * pvr_screen structure could be used here, rather than checking
    * MESA_LOADER_DRIVER_OVERRIDE directly, but the field doesn't seem
    * to have the required meaning.
    */
   return loader_override != NULL;
}

static int
PVRDRIPipeFormatToMesaFormat(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_NONE:
      return PVRDRI_MESA_FORMAT_NONE;
   case PIPE_FORMAT_BGRA8888_UNORM:
      return PVRDRI_MESA_FORMAT_B8G8R8A8_UNORM;
   case PIPE_FORMAT_BGRX8888_UNORM:
      return PVRDRI_MESA_FORMAT_B8G8R8X8_UNORM;
   case PIPE_FORMAT_B5G6R5_UNORM:
      return PVRDRI_MESA_FORMAT_B5G6R5_UNORM;
   case PIPE_FORMAT_RGBA8888_UNORM:
      return PVRDRI_MESA_FORMAT_R8G8B8A8_UNORM;
   case PIPE_FORMAT_RGBX8888_UNORM:
      return PVRDRI_MESA_FORMAT_R8G8B8X8_UNORM;
   case PIPE_FORMAT_UYVY:
      return PVRDRI_MESA_FORMAT_YCBCR;
   case PIPE_FORMAT_NV12:
      return PVRDRI_MESA_FORMAT_YUV420_2PLANE;
   case PIPE_FORMAT_NV21:
      return PVRDRI_MESA_FORMAT_YVU420_2PLANE;
   case PIPE_FORMAT_BGRA8888_SRGB:
      return PVRDRI_MESA_FORMAT_B8G8R8A8_SRGB;
   case PIPE_FORMAT_RGBA8888_SRGB:
      return PVRDRI_MESA_FORMAT_R8G8B8A8_SRGB;
   case PIPE_FORMAT_IYUV:
      return PVRDRI_MESA_FORMAT_YUV420_3PLANE;
   case PIPE_FORMAT_YV12:
      return PVRDRI_MESA_FORMAT_YVU420_3PLANE;
   case PIPE_FORMAT_YUYV:
      return PVRDRI_MESA_FORMAT_YCBCR_REV;
   case PIPE_FORMAT_YVYU:
      return PVRDRI_MESA_FORMAT_YVYU;
   case PIPE_FORMAT_VYUY:
      return PVRDRI_MESA_FORMAT_VYUY;
   case PIPE_FORMAT_Y8_U8_V8_444_UNORM:
      return PVRDRI_MESA_FORMAT_YUV444;
   default:
      return PVRDRI_MESA_FORMAT_NONE;
   }
}

static bool
pvr_is_format_supported_config(struct pvr_screen *screen,
                               enum pipe_format format,
                               unsigned sample_count)
{
   int mesa_format = PVRDRIPipeFormatToMesaFormat(format);

   if (!screen->config->have_mesa_format[mesa_format])
      return false;

   for (unsigned int i = 0 ; i < screen->config->num_msaa_modes; i++)
      if (screen->config->msaa_samples[i] == sample_count)
         return true;

   return false;
}

static bool
pvr_is_depth_stencil_supported(struct pvr_screen *screen,
                               enum pipe_format format)
{
   for (unsigned int j = 0; j < screen->config->num_depth_stencil_bits; j++) {
      enum pipe_format zs_format = PIPE_FORMAT_NONE;

      switch (screen->config->depth_bits[j]) {
      case 0:
         switch(screen->config->stencil_bits[j]) {
         case 0:
            /* PIPE_FORMAT_NONE, set above */
            break;
         case 8:
            zs_format = PIPE_FORMAT_S8_UINT;
            break;
         default:
            break;
         }
         break;
      case 16:
         switch(screen->config->stencil_bits[j]) {
         case 0:
            zs_format = PIPE_FORMAT_Z16_UNORM;
            break;
         case 8:
            zs_format = PIPE_FORMAT_Z16_UNORM_S8_UINT;
            break;
         default:
            break;
         }
         break;
      case 24:
         switch(screen->config->stencil_bits[j]) {
         case 0:
            zs_format = PIPE_FORMAT_Z24X8_UNORM;
            break;
         case 8:
            zs_format = PIPE_FORMAT_Z24_UNORM_S8_UINT;
            break;
         default:
            break;
         }
         break;
      case 32:
         switch(screen->config->stencil_bits[j]) {
         case 0:
            zs_format = PIPE_FORMAT_Z32_UNORM;
            break;
         default:
            break;
         }
      }
      if (format == zs_format)
        return true;
   }
   return false;
}

bool
pvr_is_format_supported(struct pipe_screen *pscreen,
                        enum pipe_format format,
                        enum pipe_texture_target target,
                        unsigned sample_count,
                        unsigned storage_sample_count,
                        unsigned bind)
{
   struct pvr_screen *screen = pvr_screen(pscreen);

   if (MAX2(sample_count, 1) != MAX2(storage_sample_count, 1))
      return false;

   if (sample_count == 1)
      sample_count = 0;

   if (bind == PIPE_BIND_DEPTH_STENCIL && sample_count == 0)
      return pvr_is_depth_stencil_supported(screen, format);

   if ((bind & PIPE_BIND_DISPLAY_TARGET) != 0 || sample_count != 0)
      return pvr_is_format_supported_config(screen, format, sample_count);

   if (pvr_is_format_supported_config(screen, format, 0))
      return true;

   {
      const int fourcc = pvr_pipe_format_to_fourcc(format);

      if (fourcc) {
         for (unsigned int i = 0; i < screen->num_dmabuf_formats; i++) {
            if (screen->dmabuf_formats[i] == fourcc)
               return true;
         }
      }
   }

   return false;
}
