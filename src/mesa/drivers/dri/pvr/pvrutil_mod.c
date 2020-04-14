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
#include <string.h>

#include "img_drm_fourcc.h"
#include "pvrdri_mod.h"

/*
 * The following sRGB formats defined may not be defined in drm_fourcc.h, but
 * match the corresponding __DRI_IMAGE_FOURCC formats in Mesa.
 */
#if !defined(DRM_FORMAT_SARGB8888)
#define DRM_FORMAT_SARGB8888 0x83324258
#endif

#if !defined(DRM_FORMAT_SABGR8888)
#define DRM_FORMAT_SABGR8888 0x84324258
#endif

#if !defined(DRM_FORMAT_SBGR888)
#define DRM_FORMAT_SBGR888   0xff324742
#endif

static const PVRDRIImageFormat g_asFormats[] = {
   {
      .eIMGPixelFormat = IMG_PIXFMT_R10G10B10A2_UNORM,
      .iDRIFourCC = DRM_FORMAT_ABGR2101010,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RGBA,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R10G10B10A2_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_B8G8R8A8_UNORM,
      .iDRIFourCC = DRM_FORMAT_ARGB8888,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RGBA,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_B8G8R8A8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_B8G8R8A8_UNORM_SRGB,
      .iDRIFourCC = DRM_FORMAT_SARGB8888,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RGBA,
      .bIsSRGB = true,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_B8G8R8A8_UNORM_SRGB,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_R8G8B8A8_UNORM,
      .iDRIFourCC = DRM_FORMAT_ABGR8888,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RGBA,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8G8B8A8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_R8G8B8A8_UNORM_SRGB,
      .iDRIFourCC = DRM_FORMAT_SABGR8888,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RGBA,
      .bIsSRGB = true,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8G8B8A8_UNORM_SRGB,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_B8G8R8X8_UNORM,
      .iDRIFourCC = DRM_FORMAT_XRGB8888,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RGB,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_B8G8R8X8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_R8G8B8X8_UNORM,
      .iDRIFourCC = DRM_FORMAT_XBGR8888,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RGB,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8G8B8X8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_R8G8B8_UNORM,
      .iDRIFourCC = DRM_FORMAT_BGR888,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RGB,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8G8B8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
#if defined(DRM_FORMAT_SBGR888)
   {
      .eIMGPixelFormat = IMG_PIXFMT_R8G8B8_UNORM_SRGB,
      .iDRIFourCC = DRM_FORMAT_SBGR888,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RGB,
      .bIsSRGB = true,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8G8B8_UNORM_SRGB,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
#endif
   {
      .eIMGPixelFormat = IMG_PIXFMT_B5G6R5_UNORM,
      .iDRIFourCC = DRM_FORMAT_RGB565,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RGB,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_B5G6R5_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_R8G8_UNORM,
      .iDRIFourCC = DRM_FORMAT_GR88,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RG,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8G8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
      .iDRIFourCC = DRM_FORMAT_R8,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_R,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_L8A8_UNORM,
      .iDRIFourCC = 0,
      .iDRIComponents = 0,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_L8A8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_L8_UNORM,
      .iDRIFourCC = 0,
      .iDRIComponents = 0,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_L8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_D32_FLOAT,
      .iDRIFourCC = 0,
      .iDRIComponents = 0,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_D32_FLOAT,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_S8_UINT,
      .iDRIFourCC = 0,
      .iDRIComponents = 0,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_S8_UINT,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
#if defined(DRM_FORMAT_ARGB4444)
   {
      .eIMGPixelFormat = IMG_PIXFMT_B4G4R4A4_UNORM,
      .iDRIFourCC = DRM_FORMAT_ARGB4444,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RGBA,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_B4G4R4A4_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
#endif
#if defined(DRM_FORMAT_ARGB1555)
   {
      .eIMGPixelFormat = IMG_PIXFMT_B5G5R5A1_UNORM,
      .iDRIFourCC = DRM_FORMAT_ARGB1555,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_RGBA,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_B5G5R5A1_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
#endif
   {
      .eIMGPixelFormat = IMG_PIXFMT_YUYV,
      .iDRIFourCC = DRM_FORMAT_YUYV,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_EXTERNAL,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_YUYV,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
#if defined(DRM_FORMAT_YVU444_PACK10_IMG)
   {
      .eIMGPixelFormat = IMG_PIXFMT_YVU10_444_1PLANE_PACK10,
      .iDRIFourCC = DRM_FORMAT_YVU444_PACK10_IMG,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_EXTERNAL,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_YVU10_444_1PLANE_PACK10,
         .uiWidthShift = 0,
         .uiHeightShift = 0,
      },
   },
#endif
   {
      .eIMGPixelFormat = IMG_PIXFMT_YUV420_2PLANE,
      .iDRIFourCC = DRM_FORMAT_NV12,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_Y_UV,
      .uiNumPlanes = 2,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
      .sPlanes[1] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8G8_UNORM,
         .uiWidthShift = 1,
         .uiHeightShift = 1},
   },
#if defined(DRM_FORMAT_NV21)
   {
      .eIMGPixelFormat = IMG_PIXFMT_YVU420_2PLANE,
      .iDRIFourCC = DRM_FORMAT_NV21,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_Y_UV,
      .uiNumPlanes = 2,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
      .sPlanes[1] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8G8_UNORM,
         .uiWidthShift = 1,
         .uiHeightShift = 1},
   },
#endif
   {
      .eIMGPixelFormat = IMG_PIXFMT_YUV420_3PLANE,
      .iDRIFourCC = DRM_FORMAT_YUV420,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_Y_U_V,
      .uiNumPlanes = 3,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
      .sPlanes[1] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
         .uiWidthShift = 1,
         .uiHeightShift = 1},
      .sPlanes[2] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
         .uiWidthShift = 1,
         .uiHeightShift = 1},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_YVU420_3PLANE,
      .iDRIFourCC = DRM_FORMAT_YVU420,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_Y_U_V,
      .uiNumPlanes = 3,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
      .sPlanes[1] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
         .uiWidthShift = 1,
         .uiHeightShift = 1},
      .sPlanes[2] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
         .uiWidthShift = 1,
         .uiHeightShift = 1},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_YUV8_444_3PLANE_PACK8,
      .iDRIFourCC = DRM_FORMAT_YUV444,
      .iDRIComponents = PVRDRI_IMAGE_COMPONENTS_Y_U_V,
      .uiNumPlanes = 3,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
      .sPlanes[1] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
      .sPlanes[2] = {
         .eIMGPixelFormat = IMG_PIXFMT_R8_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_D16_UNORM,
      .iDRIFourCC = 0,
      .iDRIComponents = 0,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_D16_UNORM,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
   {
      .eIMGPixelFormat = IMG_PIXFMT_D24_UNORM_X8_TYPELESS,
      .iDRIFourCC = 0,
      .iDRIComponents = 0,
      .uiNumPlanes = 1,
      .sPlanes[0] = {
         .eIMGPixelFormat = IMG_PIXFMT_D24_UNORM_X8_TYPELESS,
         .uiWidthShift = 0,
         .uiHeightShift = 0},
   },
};

/*
 * Check if a PVR Screen has support for a particular format based upon its
 * position in g_asFormats. If querying of this information isn't supported by
 * pvr_dri_support then assume the format is supported.
 */
static inline bool
PVRDRIScreenHasFormatFromIdx(const struct DRISUPScreen *const psPVRScreen,
                             const unsigned int uiFormatIdx)
{
   if (psPVRScreen->iNumFormats > 0) {
      if (uiFormatIdx < ARRAY_SIZE(g_asFormats))
         return psPVRScreen->pbHasFormat[uiFormatIdx];
      return false;
   }

   assert(psPVRScreen->iNumFormats == -1);
   return true;
}

const struct __DRIconfigRec **
PVRDRICreateConfigs(const struct DRISUPScreen *psPVRScreen)
{
   static const unsigned int auBackBufferModes[] = {
      PVRDRI_ATTRIB_SWAP_NONE,
      PVRDRI_ATTRIB_SWAP_UNDEFINED,
   };
   const uint8_t *puDepthBits = PVRDRIDepthBitsArray();
   const uint8_t *puStencilBits = PVRDRIStencilBitsArray();
   const uint8_t *puMSAASamples = PVRDRIMSAABitsArray();
   const unsigned int uNumBackBufferModes = ARRAY_SIZE(auBackBufferModes);
   const unsigned int uNumDepthStencilBits =
      PVRDRIDepthStencilBitArraySize();
   const unsigned int uNumMSAASamples = PVRDRIMSAABitArraySize();
   struct __DRIconfigRec **ppsConfigs = NULL;
   struct __DRIconfigRec **ppsNewConfigs;
   unsigned int i;

   for (i = 0; i < psPVRScreen->uNumMesaFormats; i++) {
      if (!MODSUPCreateConfigs(&ppsNewConfigs, psPVRScreen->psDRIScreen,
                               psPVRScreen->puMesaFormats[i],
                               puDepthBits, puStencilBits,
                               uNumDepthStencilBits,
                               auBackBufferModes, uNumBackBufferModes,
                               puMSAASamples, uNumMSAASamples,
                               false, false, false,
                               PVRDRI_YUV_DEPTH_RANGE_NONE,
                               PVRDRI_YUV_CSC_STANDARD_NONE,
                               PVRDRIMaxPBufferWidth(),
                               PVRDRIMaxPBufferHeight())) {
         __driUtilMessage("%s: Couldn't create DRI configs", __func__);
         return NULL;
      }

      ppsConfigs = MODSUPConcatConfigs(psPVRScreen->psDRIScreen,
                                       ppsConfigs, ppsNewConfigs);
   }

   return (const struct __DRIconfigRec **) ppsConfigs;
}

const PVRDRIImageFormat *
PVRDRIFourCCToImageFormat(struct DRISUPScreen *psPVRScreen, int iDRIFourCC)
{
   unsigned int i;

   if (!iDRIFourCC)
      return NULL;

   for (i = 0; i < ARRAY_SIZE(g_asFormats); i++) {
      if (g_asFormats[i].iDRIFourCC != iDRIFourCC)
         continue;

      if (!PVRDRIScreenHasFormatFromIdx(psPVRScreen, i))
         break;

      return &g_asFormats[i];
   }

   return NULL;
}

const PVRDRIImageFormat *
PVRDRIIMGPixelFormatToImageFormat(struct DRISUPScreen *psPVRScreen,
                                  IMG_PIXFMT eIMGPixelFormat)
{
   unsigned int i;

   assert(eIMGPixelFormat != IMG_PIXFMT_UNKNOWN);

   for (i = 0; i < ARRAY_SIZE(g_asFormats); i++) {
      if (g_asFormats[i].eIMGPixelFormat != eIMGPixelFormat)
         continue;

      /*
       * Assume that the screen has the format, i.e. it's supported by the
       * HW+SW, since we can only have an IMG_PIXFMT from having called one of
       * the other PVRDRI*ToImageFormat functions or one of the
       * pvr_dri_support functions.
       */
      assert(PVRDRIScreenHasFormatFromIdx(psPVRScreen, i));
      return &g_asFormats[i];
   }

   return NULL;
}

/*
 * The EGL_EXT_image_dma_buf_import says that if a hint is unspecified then
 * the implementation may guess based on the pixel format or may fallback to
 * some default value. Furthermore, if a hint is unsupported then the
 * implementation may use whichever settings it wants to achieve the closest
 * match.
 */
IMG_YUV_COLORSPACE
PVRDRIToIMGColourSpace(const PVRDRIImageFormat *psFormat,
                       unsigned int uDRIColourSpace,
                       unsigned int uDRISampleRange)
{
   switch (psFormat->iDRIComponents) {
   case PVRDRI_IMAGE_COMPONENTS_R:
   case PVRDRI_IMAGE_COMPONENTS_RG:
   case PVRDRI_IMAGE_COMPONENTS_RGB:
   case PVRDRI_IMAGE_COMPONENTS_RGBA:
      return IMG_COLORSPACE_UNDEFINED;
   case PVRDRI_IMAGE_COMPONENTS_Y_U_V:
   case PVRDRI_IMAGE_COMPONENTS_Y_UV:
   case PVRDRI_IMAGE_COMPONENTS_Y_XUXV:
   case PVRDRI_IMAGE_COMPONENTS_EXTERNAL:
      break;
   default:
      errorMessage("%s: Unrecognised DRI components (components = 0x%X)",
                   __func__, psFormat->iDRIComponents);
      unreachable("unhandled DRI component");
      return IMG_COLORSPACE_UNDEFINED;
   }

   switch (uDRIColourSpace) {
   case PVRDRI_YUV_COLOR_SPACE_UNDEFINED:
   case PVRDRI_YUV_COLOR_SPACE_ITU_REC601:
      switch (uDRISampleRange) {
      case PVRDRI_YUV_RANGE_UNDEFINED:
      case PVRDRI_YUV_NARROW_RANGE:
         return IMG_COLORSPACE_BT601_CONFORMANT_RANGE;
      case PVRDRI_YUV_FULL_RANGE:
         return IMG_COLORSPACE_BT601_FULL_RANGE;
      default:
         errorMessage ("%s: Unrecognised DRI sample range (sample range = 0x%X)",
                       __func__, uDRISampleRange);
         unreachable("unhandled sample range");
         return IMG_COLORSPACE_UNDEFINED;
      }
   case PVRDRI_YUV_COLOR_SPACE_ITU_REC709:
      switch (uDRISampleRange) {
      case PVRDRI_YUV_RANGE_UNDEFINED:
      case PVRDRI_YUV_NARROW_RANGE:
         return IMG_COLORSPACE_BT709_CONFORMANT_RANGE;
      case PVRDRI_YUV_FULL_RANGE:
         return IMG_COLORSPACE_BT709_FULL_RANGE;
      default:
         errorMessage ("%s: Unrecognised DRI sample range (sample range = 0x%X)",
                       __func__, uDRISampleRange);
         unreachable("unhandled sample range");
         return IMG_COLORSPACE_UNDEFINED;
      }
   case PVRDRI_YUV_COLOR_SPACE_ITU_REC2020:
      switch (uDRISampleRange) {
      case PVRDRI_YUV_RANGE_UNDEFINED:
      case PVRDRI_YUV_NARROW_RANGE:
         return IMG_COLORSPACE_BT2020_CONFORMANT_RANGE;
      case PVRDRI_YUV_FULL_RANGE:
         return IMG_COLORSPACE_BT2020_FULL_RANGE;
      default:
         errorMessage ("%s: Unrecognised DRI sample range (sample range = 0x%X)",
                       __func__, uDRISampleRange);
         assert(0);
         return IMG_COLORSPACE_UNDEFINED;
      }
   default:
      errorMessage ("%s: Unrecognised DRI color space (color space = 0x%X)",
                    __func__, uDRIColourSpace);
      unreachable("unhandled color space");
      return IMG_COLORSPACE_UNDEFINED;
   }
}

IMG_YUV_CHROMA_INTERP
PVRDRIChromaSittingToIMGInterp(const PVRDRIImageFormat *psFormat,
                               unsigned int uChromaSitting)
{
   switch (psFormat->iDRIComponents) {
   case PVRDRI_IMAGE_COMPONENTS_R:
   case PVRDRI_IMAGE_COMPONENTS_RG:
   case PVRDRI_IMAGE_COMPONENTS_RGB:
   case PVRDRI_IMAGE_COMPONENTS_RGBA:
      return IMG_CHROMA_INTERP_UNDEFINED;
   case PVRDRI_IMAGE_COMPONENTS_Y_U_V:
   case PVRDRI_IMAGE_COMPONENTS_Y_UV:
   case PVRDRI_IMAGE_COMPONENTS_Y_XUXV:
   case PVRDRI_IMAGE_COMPONENTS_EXTERNAL:
      break;
   default:
      errorMessage("%s: Unrecognised DRI components (components = 0x%X)",
                   __func__, psFormat->iDRIComponents);
      unreachable("unhandled dri component");
      return IMG_CHROMA_INTERP_UNDEFINED;
   }

   switch (uChromaSitting) {
   case PVRDRI_YUV_CHROMA_SITING_UNDEFINED:
   case PVRDRI_YUV_CHROMA_SITING_0:
      return IMG_CHROMA_INTERP_ZERO;
   case PVRDRI_YUV_CHROMA_SITING_0_5:
      return IMG_CHROMA_INTERP_HALF;
   default:
      errorMessage ("%s: Unrecognised DRI chroma sitting (chroma sitting = 0x%X)",
          __func__, uChromaSitting);
      unreachable("unhandled chroma sitting");
      return IMG_CHROMA_INTERP_UNDEFINED;
   }
}

bool
PVRDRIGetSupportedFormats(struct DRISUPScreen *psPVRScreen)
{
   int *piFormats;
   IMG_PIXFMT *peImgFormats;
   bool bRet = false;
   unsigned int i;

   piFormats = malloc(ARRAY_SIZE(g_asFormats) * sizeof(*piFormats));
   peImgFormats = malloc(ARRAY_SIZE(g_asFormats) * sizeof(*peImgFormats));

   psPVRScreen->pbHasFormat = malloc(ARRAY_SIZE(g_asFormats) *
                                     sizeof(*psPVRScreen->pbHasFormat));

   psPVRScreen->psModifiers = calloc(ARRAY_SIZE(g_asFormats),
                                     sizeof(*psPVRScreen->psModifiers));

   if (!piFormats || !peImgFormats ||
       !psPVRScreen->pbHasFormat || !psPVRScreen->psModifiers) {
      errorMessage("%s: Out of memory", __func__);
      goto err_free;
   }

   for (i = 0; i < ARRAY_SIZE(g_asFormats); i++) {
      piFormats[i] = g_asFormats[i].iDRIFourCC;
      peImgFormats[i] = g_asFormats[i].eIMGPixelFormat;

      psPVRScreen->psModifiers[i].iNumModifiers = -1;
   }

   psPVRScreen->iNumFormats =
      PVRDRIQuerySupportedFormats(psPVRScreen->psImpl, ARRAY_SIZE(g_asFormats),
                                  piFormats, peImgFormats,
                                  psPVRScreen->pbHasFormat);
   if (psPVRScreen->iNumFormats == 0) {
      __driUtilMessage("%s: Couldn't query supported pixel formats",
                       __func__);
      goto err_free;
   }

   bRet = true;
   goto cleanup;

err_free:
   free(psPVRScreen->psModifiers);
   psPVRScreen->psModifiers = NULL;

   free(psPVRScreen->pbHasFormat);
   psPVRScreen->pbHasFormat = NULL;
cleanup:
   free(peImgFormats);
   free(piFormats);
   return bRet;
}

bool
DRIMODQueryDMABufFormats(struct DRISUPScreen *psPVRScreen, int iMax,
                         int *piFormats, int *piCount)
{
   int iLim;
   unsigned int i;
   int j;

   assert(psPVRScreen->iNumFormats != 0);

   if (psPVRScreen->iNumFormats < 0)
      return false;

   iLim = (iMax) ? iMax : psPVRScreen->iNumFormats;

   for (i = 0, j = 0; i < ARRAY_SIZE(g_asFormats) && j < iLim; i++) {
      /*
       * SRGB formats don't map to DRM formats, as defined by drm_fourcc.h, so
       * should not be returned.
       */
      if (g_asFormats[i].bIsSRGB)
         continue;

      if (psPVRScreen->pbHasFormat[i] && g_asFormats[i].iDRIFourCC) {
         if (iMax)
            piFormats[j] = g_asFormats[i].iDRIFourCC;
         j++;
      }
   }

   *piCount = j;

   return true;
}

static bool
PVRDRIGetSupportedModifiers(struct DRISUPScreen *psPVRScreen,
                            struct PVRDRIModifiers *psModifiers,
                            const PVRDRIImageFormat *psFormat)
{
   int iNumModifiers;

   iNumModifiers = PVRDRIQueryModifiers(psPVRScreen->psImpl,
                                        psFormat->iDRIFourCC,
                                        psFormat->eIMGPixelFormat,
                                        NULL, NULL);
   if (iNumModifiers < 0) {
      errorMessage("%s: Couldn't query modifiers for format 0x%x",
                   __func__, psFormat->iDRIFourCC);
      return false;
   }

   psModifiers->puModifiers = malloc(iNumModifiers *
                                     sizeof(*psModifiers->puModifiers));
   psModifiers->puExternalOnly = malloc(iNumModifiers *
                                        sizeof(*psModifiers->
                                               puExternalOnly));
   if (!psModifiers->puModifiers || !psModifiers->puExternalOnly) {
      free(psModifiers->puModifiers);
      psModifiers->puModifiers = NULL;

      free(psModifiers->puExternalOnly);
      psModifiers->puExternalOnly = NULL;

      errorMessage("%s: Out of memory", __func__);

      return false;
   }
   psModifiers->iNumModifiers = iNumModifiers;

   iNumModifiers = PVRDRIQueryModifiers(psPVRScreen->psImpl,
                                        psFormat->iDRIFourCC,
                                        psFormat->eIMGPixelFormat,
                                        psModifiers->puModifiers,
                                        psModifiers->puExternalOnly);

   assert(iNumModifiers == psModifiers->iNumModifiers);

   return true;
}

static bool
PVRDRIGetModifiersForFormat(struct DRISUPScreen *psPVRScreen, int iFourCC,
                            const PVRDRIImageFormat **ppsFormat,
                            const struct PVRDRIModifiers **ppsModifiers)
{
   const PVRDRIImageFormat *psFormat;
   struct PVRDRIModifiers *psModifiers;
   unsigned int uIdx;

   assert(psPVRScreen->iNumFormats != 0);

   if (psPVRScreen->iNumFormats < 0)
      return false;

   psFormat = PVRDRIFourCCToImageFormat(psPVRScreen, iFourCC);
   if (!psFormat)
      return false;

   uIdx = psFormat - g_asFormats;
   psModifiers = &psPVRScreen->psModifiers[uIdx];

   if (psModifiers->iNumModifiers < 0)
      if (!PVRDRIGetSupportedModifiers(psPVRScreen, psModifiers, psFormat))
         return false;

   *ppsFormat = psFormat;
   *ppsModifiers = psModifiers;

   return true;
}

bool
PVRDRIValidateImageModifier(struct DRISUPScreen *psPVRScreen,
                            const int iFourCC, const uint64_t uiModifier)
{
   const PVRDRIImageFormat *psFormat;
   const struct PVRDRIModifiers *psModifiers;

   if (!PVRDRIGetModifiersForFormat(psPVRScreen, iFourCC, &psFormat,
                                    &psModifiers))
      return false;

   for (int i = 0; i < psModifiers->iNumModifiers; i++)
      if (psModifiers->puModifiers[i] == uiModifier)
         return true;

   return false;
}

bool
DRIMODQueryDMABufModifiers(struct DRISUPScreen *psPVRScreen, int iFourCC,
                           int iMax, uint64_t *puModifiers,
                           unsigned int *puExternalOnly, int *piCount)
{
   const PVRDRIImageFormat *psFormat;
   const struct PVRDRIModifiers *psModifiers;
   int iNumCopy;

   if (!PVRDRIGetModifiersForFormat(psPVRScreen, iFourCC, &psFormat,
                                    &psModifiers))
      return false;

   if (!iMax) {
      *piCount = psModifiers->iNumModifiers;
      return true;
   }

   iNumCopy = (iMax < psModifiers->iNumModifiers) ?
      iMax : psModifiers->iNumModifiers;

   if (puModifiers)
      (void) memcpy(puModifiers, psModifiers->puModifiers,
                    sizeof(*puModifiers) * iNumCopy);

   if (puExternalOnly)
      (void) memcpy(puExternalOnly, psModifiers->puExternalOnly,
                    sizeof(*puExternalOnly) * iNumCopy);

   *piCount = iNumCopy;

   return true;
}

bool
DRIMODQueryDMABufFormatModifierAttribs(struct DRISUPScreen *psPVRScreen,
                                       uint32_t uFourCC, uint64_t uModifier,
                                       int iAttribute, uint64_t *puValue)
{
   const PVRDRIImageFormat *psFormat;
   const struct PVRDRIModifiers *psModifiers;
   int i;

   if (!PVRDRIGetModifiersForFormat(psPVRScreen, uFourCC, &psFormat,
                                    &psModifiers))
      return false;

   for (i = 0; i < psModifiers->iNumModifiers; i++)
      if (psModifiers->puModifiers[i] == uModifier)
         break;

   if (i == psModifiers->iNumModifiers)
      return false;

   switch (iAttribute) {
   case PVRDRI_IMAGE_FORMAT_MODIFIER_ATTRIB_PLANE_COUNT:
      *puValue = psFormat->uiNumPlanes;
      break;
   default:
      return false;
   }

   return true;
}

void
PVRDRIDestroyFormatInfo(struct DRISUPScreen *psPVRScreen)
{
   unsigned int i;

   if (psPVRScreen->psModifiers) {
      for (i = 0; i < ARRAY_SIZE(g_asFormats); i++) {
         free(psPVRScreen->psModifiers[i].puModifiers);
         free(psPVRScreen->psModifiers[i].puExternalOnly);
      }
      free(psPVRScreen->psModifiers);
   }

   free(psPVRScreen->pbHasFormat);
}

bool
PVRDRIGetMesaFormats(struct DRISUPScreen *psPVRScreen)
{
   const unsigned int auMesaFormatsBase[] = {
      PVRDRI_MESA_FORMAT_B8G8R8A8_UNORM,
      PVRDRI_MESA_FORMAT_B8G8R8A8_SRGB,
      PVRDRI_MESA_FORMAT_B8G8R8X8_UNORM,
      PVRDRI_MESA_FORMAT_B5G6R5_UNORM,
   };
   const unsigned int auMesaFormatsRGB[] = {
      PVRDRI_MESA_FORMAT_R8G8B8A8_UNORM,
      PVRDRI_MESA_FORMAT_R8G8B8A8_SRGB,
      PVRDRI_MESA_FORMAT_R8G8B8X8_UNORM,
   };
   unsigned int uSizeMesaFormats;
   bool bCapRGB;
   unsigned int i, j;

   bCapRGB = MODSUPGetCapability(psPVRScreen->psDRIScreen,
                                 PVRDRI_LOADER_CAP_RGBA_ORDERING) != 0;

   uSizeMesaFormats = sizeof(auMesaFormatsBase);
   if (bCapRGB)
      uSizeMesaFormats += sizeof(auMesaFormatsRGB);

   /*
    * We haven't checked if any of the Mesa formats are supported by the DDK
    * at this point, so we may allocate more memory than we need.
    */
   psPVRScreen->puMesaFormats = malloc(uSizeMesaFormats);
   if (psPVRScreen->puMesaFormats == NULL) {
      __driUtilMessage("%s: Out of memory", __func__);
      return false;
   }

   for (i = 0, j = 0; i < ARRAY_SIZE(auMesaFormatsBase); i++) {
      unsigned int uMesaFormat = auMesaFormatsBase[i];

      if (!PVRDRIMesaFormatSupported(uMesaFormat))
         continue;

      psPVRScreen->puMesaFormats[j++] = uMesaFormat;
   }

   if (bCapRGB) {
      for (i = 0; i < ARRAY_SIZE(auMesaFormatsRGB); i++) {
         unsigned int uMesaFormat = auMesaFormatsRGB[i];

         if (!PVRDRIMesaFormatSupported(uMesaFormat))
            continue;

         psPVRScreen->puMesaFormats[j++] = uMesaFormat;
      }
   }

   assert((j * sizeof(psPVRScreen->puMesaFormats[0])) <=
          uSizeMesaFormats);

   psPVRScreen->uNumMesaFormats = j;

   return true;
}

void
PVRDRIFreeMesaFormats(struct DRISUPScreen *psPVRScreen)
{
   free(psPVRScreen->puMesaFormats);
}
