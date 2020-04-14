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

#include "utils.h"
#include "pvrdri.h"

int
MODSUPGetBuffers(__DRIdrawable *psDRIDrawable, unsigned int uFourCC,
                 uint32_t *puStamp, void *pvLoaderPrivate,
                 uint32_t uBufferMask, struct PVRDRIImageList *psImageList)
{
   PVRDRIDrawable *psPVRDrawable = psDRIDrawable->driverPrivate;
   __DRIscreen *psDRIScreen = psDRIDrawable->driScreenPriv;
   struct __DRIimageList sDRIList;
   int res;

#if !defined(DRI_IMAGE_HAS_BUFFER_PREV)
   uBufferMask &= ~PVRDRI_IMAGE_BUFFER_PREV;
#endif

   if (psPVRDrawable->uFourCC != uFourCC) {
      psPVRDrawable->uDRIFormat = PVRDRIFourCCToDRIFormat(uFourCC);
      psPVRDrawable->uFourCC = uFourCC;
   }

   res = psDRIScreen->image.loader->getBuffers(psDRIDrawable,
                                               psPVRDrawable->uDRIFormat,
                                               puStamp,
                                               pvLoaderPrivate,
                                               uBufferMask, &sDRIList);

   if (res) {
      psImageList->uImageMask = sDRIList.image_mask;
      psImageList->psBack = sDRIList.back;
      psImageList->psFront = sDRIList.front;
      psImageList->psPrev = sDRIList.prev;
   }

   return res;
}

bool
MODSUPCreateConfigs(__DRIconfig ***pppsConfigs, __DRIscreen *psDRIScreen,
                    int iPVRDRIMesaFormat, const uint8_t *puDepthBits,
                    const uint8_t *puStencilBits,
                    unsigned int uNumDepthStencilBits,
                    const unsigned int *puDBModes, unsigned int uNumDBModes,
                    const uint8_t *puMSAASamples, unsigned int uNumMSAAModes,
                    bool bEnableAccum, bool bColorDepthMatch,
                    bool bMutableRenderBuffer,
                    int iYUVDepthRange, int iYUVCSCStandard,
                    uint32_t uMaxPbufferWidth, uint32_t uMaxPbufferHeight)
{
   __DRIconfig **ppsConfigs;
   mesa_format eFormat = PVRDRIMesaFormatToMesaFormat(iPVRDRIMesaFormat);
   unsigned int i;

   (void) psDRIScreen;

   switch (eFormat) {
   case MESA_FORMAT_NONE:
      __driUtilMessage("%s: Unknown PVR DRI format: %u",
                       __func__, iPVRDRIMesaFormat);
      return false;
   default:
      break;
   }

   /*
    * The double buffered modes array argument for driCreateConfigs has
    * entries of type GLenum.
    */
   static_assert(sizeof(GLenum) == sizeof(unsigned int),
                 "Size mismatch between GLenum and unsigned int");

   ppsConfigs = driCreateConfigs(eFormat, puDepthBits, puStencilBits,
                                 uNumDepthStencilBits, (GLenum *) puDBModes,
                                 uNumDBModes, puMSAASamples, uNumMSAAModes,
                                 bEnableAccum, bColorDepthMatch,
                                 bMutableRenderBuffer,
                                 iYUVDepthRange, iYUVCSCStandard);
   if (!ppsConfigs)
      return false;

   for (i = 0; ppsConfigs[i]; i++) {
      ppsConfigs[i]->modes.maxPbufferWidth = uMaxPbufferWidth;
      ppsConfigs[i]->modes.maxPbufferHeight = uMaxPbufferHeight;
      ppsConfigs[i]->modes.maxPbufferPixels =
         uMaxPbufferWidth * uMaxPbufferHeight;
   }

   *pppsConfigs = ppsConfigs;

   return true;
}

__DRIconfig **
MODSUPConcatConfigs(__DRIscreen *psDRIScreen,
                    __DRIconfig **ppsConfigA, __DRIconfig **ppsConfigB)
{
   (void) psDRIScreen;

   return driConcatConfigs(ppsConfigA, ppsConfigB);
}

struct __DRIimageRec *
MODSUPLookupEGLImage(__DRIscreen *psDRIScreen, void *pvImage,
                     void *pvLoaderPrivate)
{
   return psDRIScreen->dri2.image->lookupEGLImage(psDRIScreen,
                                                  pvImage,
                                                  pvLoaderPrivate);
}


unsigned int
MODSUPGetCapability(__DRIscreen *psDRIScreen, unsigned int uCapability)
{
   if (psDRIScreen->image.loader->base.version >= 2 &&
       psDRIScreen->image.loader->getCapability) {
      enum dri_loader_cap eCapability =
         (enum dri_loader_cap) uCapability;

      return psDRIScreen->image.loader->getCapability(
         psDRIScreen->loaderPrivate,
         eCapability);
   }

   return 0;
}

bool
PVRDRIConfigQuery(const PVRDRIConfig *psConfig,
                  PVRDRIConfigAttrib eConfigAttrib, int *piValueOut)
{
   if (!psConfig || !piValueOut)
      return false;

   switch (eConfigAttrib) {
   case PVRDRI_CONFIG_ATTRIB_RENDERABLE_TYPE:
      *piValueOut = psConfig->iSupportedAPIs;
      return true;
   case PVRDRI_CONFIG_ATTRIB_RGB_MODE:
      *piValueOut = psConfig->sGLMode.rgbMode;
      return true;
   case PVRDRI_CONFIG_ATTRIB_DOUBLE_BUFFER_MODE:
      *piValueOut = psConfig->sGLMode.doubleBufferMode;
      return true;
   case PVRDRI_CONFIG_ATTRIB_RED_BITS:
      *piValueOut = psConfig->sGLMode.redBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_GREEN_BITS:
      *piValueOut = psConfig->sGLMode.greenBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_BLUE_BITS:
      *piValueOut = psConfig->sGLMode.blueBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_ALPHA_BITS:
      *piValueOut = psConfig->sGLMode.alphaBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_RGB_BITS:
      *piValueOut = psConfig->sGLMode.rgbBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_DEPTH_BITS:
      *piValueOut = psConfig->sGLMode.depthBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_STENCIL_BITS:
      *piValueOut = psConfig->sGLMode.stencilBits;
      return true;
   case PVRDRI_CONFIG_ATTRIB_SAMPLE_BUFFERS:
      *piValueOut = psConfig->sGLMode.sampleBuffers;
      return true;
   case PVRDRI_CONFIG_ATTRIB_SAMPLES:
      *piValueOut = psConfig->sGLMode.samples;
      return true;
   case PVRDRI_CONFIG_ATTRIB_BIND_TO_TEXTURE_RGB:
      *piValueOut = psConfig->sGLMode.bindToTextureRgb;
      return true;
   case PVRDRI_CONFIG_ATTRIB_BIND_TO_TEXTURE_RGBA:
      *piValueOut = psConfig->sGLMode.bindToTextureRgba;
      return true;
#if defined(__DRI_ATTRIB_YUV_BIT)
   case PVRDRI_CONFIG_ATTRIB_YUV_ORDER:
      *piValueOut = psConfig->sGLMode.YUVOrder;
      return true;
   case PVRDRI_CONFIG_ATTRIB_YUV_NUM_OF_PLANES:
      *piValueOut = psConfig->sGLMode.YUVNumberOfPlanes;
      return true;
   case PVRDRI_CONFIG_ATTRIB_YUV_SUBSAMPLE:
      *piValueOut = psConfig->sGLMode.YUVSubsample;
      return true;
   case PVRDRI_CONFIG_ATTRIB_YUV_DEPTH_RANGE:
      *piValueOut = psConfig->sGLMode.YUVDepthRange;
      return true;
   case PVRDRI_CONFIG_ATTRIB_YUV_CSC_STANDARD:
      *piValueOut = psConfig->sGLMode.YUVCSCStandard;
      return true;
   case PVRDRI_CONFIG_ATTRIB_YUV_PLANE_BPP:
      *piValueOut = psConfig->sGLMode.YUVPlaneBPP;
      return true;
#endif
#if !defined(__DRI_ATTRIB_YUV_BIT)
   case PVRDRI_CONFIG_ATTRIB_YUV_ORDER:
   case PVRDRI_CONFIG_ATTRIB_YUV_NUM_OF_PLANES:
   case PVRDRI_CONFIG_ATTRIB_YUV_SUBSAMPLE:
   case PVRDRI_CONFIG_ATTRIB_YUV_DEPTH_RANGE:
   case PVRDRI_CONFIG_ATTRIB_YUV_CSC_STANDARD:
   case PVRDRI_CONFIG_ATTRIB_YUV_PLANE_BPP:
      return false;
#endif
   case PVRDRI_CONFIG_ATTRIB_RED_MASK:
      *piValueOut = psConfig->sGLMode.redMask;
      return true;
   case PVRDRI_CONFIG_ATTRIB_GREEN_MASK:
      *piValueOut = psConfig->sGLMode.greenMask;
      return true;
   case PVRDRI_CONFIG_ATTRIB_BLUE_MASK:
      *piValueOut = psConfig->sGLMode.blueMask;
      return true;
   case PVRDRI_CONFIG_ATTRIB_ALPHA_MASK:
      *piValueOut = psConfig->sGLMode.alphaMask;
      return true;
   case PVRDRI_CONFIG_ATTRIB_SRGB_CAPABLE:
      *piValueOut = psConfig->sGLMode.sRGBCapable;
      return true;
   case PVRDRI_CONFIG_ATTRIB_INVALID:
      errorMessage("%s: Invalid attribute", __func__);
      assert(0);
      return false;
   default:
      return false;
   }
}

bool
MODSUPConfigQuery(const PVRDRIConfig *psConfig,
                  PVRDRIConfigAttrib eConfigAttrib, unsigned int *puValueOut)
{
   bool bRes;
   int iValue;

   bRes = PVRDRIConfigQuery(psConfig, eConfigAttrib, &iValue);
   if (bRes)
      *puValueOut = (unsigned int) iValue;

   return bRes;
}

void
MODSUPFlushFrontBuffer(struct __DRIdrawableRec *psDRIDrawable,
                       void *pvLoaderPrivate)
{
   PVRDRIDrawable *psPVRDrawable = psDRIDrawable->driverPrivate;
   __DRIscreen *psDRIScreen = psDRIDrawable->driScreenPriv;

   if (!psPVRDrawable->sConfig.sGLMode.doubleBufferMode)
      psDRIScreen->image.loader->flushFrontBuffer(psDRIDrawable,
                                                  pvLoaderPrivate);
}
