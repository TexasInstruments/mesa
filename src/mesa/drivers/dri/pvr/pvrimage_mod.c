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
#include <assert.h>

#include "util/u_atomic.h"
#include <xf86drm.h>

#include "img_drm_fourcc.h"
#include "pvrdri_mod.h"

struct PVRDRIImageShared {
   int iRefCount;

   struct DRISUPScreen *psPVRScreen;

   PVRDRIImageType eType;
   const PVRDRIImageFormat *psFormat;
   IMG_YUV_COLORSPACE eColourSpace;
   IMG_YUV_CHROMA_INTERP eChromaUInterp;
   IMG_YUV_CHROMA_INTERP eChromaVInterp;

   PVRDRIBufferImpl *psBuffer;
   IMGEGLImage *psEGLImage;
   PVRDRIEGLImageType eglImageType;
   struct PVRDRIImageShared *psAncestor;
};

struct __DRIimageRec {
   int iRefCount;

   void *pvLoaderPrivate;

   struct PVRDRIImageShared *psShared;

   IMGEGLImage *psEGLImage;
};


static struct PVRDRIImageShared *
CommonImageSharedSetup(struct DRISUPScreen *psPVRScreen,
                       PVRDRIImageType eType)
{
   struct PVRDRIImageShared *psShared;

   psShared = calloc(1, sizeof(*psShared));
   if (!psShared)
      return NULL;

   psShared->psPVRScreen = psPVRScreen;
   psShared->eType = eType;
   psShared->iRefCount = 1;

   assert(psShared->eColourSpace == IMG_COLORSPACE_UNDEFINED &&
          psShared->eChromaUInterp == IMG_CHROMA_INTERP_UNDEFINED &&
          psShared->eChromaVInterp == IMG_CHROMA_INTERP_UNDEFINED);

   return psShared;
}

static void
DestroyImageShared(struct PVRDRIImageShared *psShared)
{
   int iRefCount = p_atomic_dec_return(&psShared->iRefCount);

   assert(iRefCount >= 0);

   if (iRefCount > 0)
      return;

   switch (psShared->eType) {
   case PVRDRI_IMAGE_FROM_NAMES:
   case PVRDRI_IMAGE_FROM_DMABUFS:
   case PVRDRI_IMAGE:
      if (psShared->psBuffer)
         PVRDRIBufferDestroy(psShared->psBuffer);

      assert(!psShared->psAncestor);
      break;
   case PVRDRI_IMAGE_FROM_EGLIMAGE: {
      PVRDRIScreenImpl *psScreenImpl;

      psScreenImpl = psShared->psPVRScreen->psImpl;

      PVRDRIEGLImageDestroyExternal(psScreenImpl,
                                    psShared->psEGLImage,
                                    psShared->eglImageType);
      break;
   }
   case PVRDRI_IMAGE_SUBIMAGE:
      if (psShared->psBuffer)
         PVRDRIBufferDestroy(psShared->psBuffer);

      if (psShared->psAncestor)
         DestroyImageShared(psShared->psAncestor);
      break;
   default:
      errorMessage("%s: Unknown image type: %d",
                   __func__, (int) psShared->eType);
      break;
   }

   free(psShared);
}

static struct PVRDRIImageShared *
CreateImageSharedFromEGLImage(struct DRISUPScreen *psPVRScreen,
                              IMGEGLImage *psEGLImage,
                              PVRDRIEGLImageType eglImageType)
{
   struct PVRDRIImageShared *psShared;
   PVRDRIBufferAttribs sAttribs;
   const PVRDRIImageFormat *psFormat;

   PVRDRIEGLImageGetAttribs(psEGLImage, &sAttribs);

   psFormat = PVRDRIIMGPixelFormatToImageFormat(psPVRScreen,
                                                sAttribs.ePixFormat);
   if (!psFormat)
      return NULL;

   psShared = CommonImageSharedSetup(psPVRScreen, PVRDRI_IMAGE_FROM_EGLIMAGE);
   if (!psShared)
      return NULL;

   psShared->psEGLImage = psEGLImage;
   psShared->psFormat = psFormat;
   psShared->eglImageType = eglImageType;

   return psShared;
}

static struct PVRDRIImageShared *
CreateImageSharedFromNames(struct DRISUPScreen *psPVRScreen,
                           int iWidth, int iHeight, int iFourCC,
                           int *piNames, int iNumNames,
                           int *piStrides, int *piOffsets)
{
   struct PVRDRIImageShared *psShared;
   const PVRDRIImageFormat *psFormat;
   int aiPlaneNames[DRI_PLANES_MAX];
   unsigned int auiWidthShift[DRI_PLANES_MAX];
   unsigned int auiHeightShift[DRI_PLANES_MAX];
   unsigned int i;

   psFormat = PVRDRIFourCCToImageFormat(psPVRScreen, iFourCC);
   if (!psFormat) {
      errorMessage("%s: Unsupported DRI FourCC (fourcc = 0x%X)",
                   __func__, iFourCC);
      return NULL;
   }

   if (iNumNames != 1 && iNumNames != (int) psFormat->uiNumPlanes) {
      errorMessage("%s: Unexpected number of names (%d) for fourcc "
                   "(#%x) - expected 1 or %u",
                   __func__, iNumNames, iFourCC, psFormat->uiNumPlanes);
      return NULL;
   }

   for (i = 0; i < psFormat->uiNumPlanes; i++) {
      if (piOffsets[i] < 0) {
         errorMessage("%s: Offset %d unsupported (value = %d)",
                      __func__, i, piOffsets[i]);
         return NULL;
      }

      aiPlaneNames[i] = iNumNames == 1 ? piNames[0] : piNames[i];
      auiWidthShift[i] = psFormat->sPlanes[i].uiWidthShift;
      auiHeightShift[i] = psFormat->sPlanes[i].uiHeightShift;
   }

   psShared =
      CommonImageSharedSetup(psPVRScreen, PVRDRI_IMAGE_FROM_NAMES);
   if (!psShared)
      return NULL;

   psShared->psBuffer = PVRDRIBufferCreateFromNames(psPVRScreen->psImpl,
                                                    iWidth, iHeight,
                                                    psFormat->uiNumPlanes,
                                                    aiPlaneNames,
                                                    piStrides, piOffsets,
                                                    auiWidthShift,
                                                    auiHeightShift);
   if (!psShared->psBuffer) {
      errorMessage("%s: Failed to create buffer for shared image",
                   __func__);
      goto ErrorDestroyImage;
   }

   psShared->psFormat = psFormat;
   psShared->eColourSpace =
      PVRDRIToIMGColourSpace(psFormat,
                             PVRDRI_YUV_COLOR_SPACE_UNDEFINED,
                             PVRDRI_YUV_RANGE_UNDEFINED);
   psShared->eChromaUInterp =
      PVRDRIChromaSittingToIMGInterp(psFormat,
                                     PVRDRI_YUV_CHROMA_SITING_UNDEFINED);
   psShared->eChromaVInterp =
      PVRDRIChromaSittingToIMGInterp(psFormat,
                                     PVRDRI_YUV_CHROMA_SITING_UNDEFINED);

   return psShared;

ErrorDestroyImage:
   DestroyImageShared(psShared);

   return NULL;
}

static struct PVRDRIImageShared *
CreateImageSharedFromDMABufs(struct DRISUPScreen *psPVRScreen,
                             int iWidth, int iHeight,
                             int iFourCC, uint64_t uModifier,
                             int *piFDs, int iNumFDs,
                             int *piStrides, int *piOffsets,
                             unsigned int uColorSpace,
                             unsigned int uSampleRange,
                             unsigned int uHorizSiting,
                             unsigned int uVertSiting,
                             unsigned int *puError)
{
   struct PVRDRIImageShared *psShared;
   const PVRDRIImageFormat *psFormat;
   int aiPlaneFDs[DRI_PLANES_MAX];
   unsigned int auiWidthShift[DRI_PLANES_MAX];
   unsigned int auiHeightShift[DRI_PLANES_MAX];
   unsigned int i;

   psFormat = PVRDRIFourCCToImageFormat(psPVRScreen, iFourCC);
   if (!psFormat) {
      errorMessage("%s: Unsupported DRI FourCC (fourcc = 0x%X)",
                   __func__, iFourCC);
      *puError = PVRDRI_IMAGE_ERROR_BAD_MATCH;
      return NULL;
   }

   /* When a modifier isn't specified, skip the validation */
   if (uModifier != DRM_FORMAT_MOD_INVALID) {
      /*
       * The modifier validation has to be done in this "higher" level
       * function instead of pvr_dri_support. The support for modifiers is
       * done on per format basis, but there is no way to pass the format
       * information down to the plane creation API in pvr_dri_support.
       */
      if (!PVRDRIValidateImageModifier(psPVRScreen, iFourCC, uModifier)) {
         errorMessage("%s: Unsupported mod (fmt = %#x, mod = %#llx",
                      __func__, iFourCC,
                      (long long unsigned int) uModifier);
         *puError = PVRDRI_IMAGE_ERROR_BAD_MATCH;
         return NULL;
      }
   }

   if (iNumFDs != 1 && iNumFDs != (int) psFormat->uiNumPlanes) {
      errorMessage("%s: Unexpected number of FDs (%d) for fourcc "
                   "(#%x) - expected 1 or %u",
                   __func__, iNumFDs, iFourCC, psFormat->uiNumPlanes);
      *puError = PVRDRI_IMAGE_ERROR_BAD_MATCH;
      return NULL;
   }

   for (i = 0; i < psFormat->uiNumPlanes; i++) {
      if (piOffsets[i] < 0) {
         errorMessage("%s: Offset %d unsupported (value = %d)",
                      __func__, i, piOffsets[i]);
         *puError = PVRDRI_IMAGE_ERROR_BAD_ACCESS;
         return NULL;
      }

      aiPlaneFDs[i] = iNumFDs == 1 ? piFDs[0] : piFDs[i];
      auiWidthShift[i] = psFormat->sPlanes[i].uiWidthShift;
      auiHeightShift[i] = psFormat->sPlanes[i].uiHeightShift;
   }

   psShared = CommonImageSharedSetup(psPVRScreen,
                                     PVRDRI_IMAGE_FROM_DMABUFS);
   if (!psShared) {
      *puError = PVRDRI_IMAGE_ERROR_BAD_ALLOC;
      return NULL;
   }

   psShared->psBuffer =
      PVRDRIBufferCreateFromFdsWithModifier(psPVRScreen->psImpl,
                                            iWidth, iHeight, uModifier,
                                            psFormat->uiNumPlanes, aiPlaneFDs,
                                            piStrides, piOffsets,
                                            auiWidthShift, auiHeightShift);
   if (!psShared->psBuffer) {
      errorMessage("%s: Failed to create buffer for shared image",
                   __func__);
      *puError = PVRDRI_IMAGE_ERROR_BAD_ALLOC;
      goto ErrorDestroyImage;
   }

   psShared->psFormat = psFormat;
   psShared->eColourSpace = PVRDRIToIMGColourSpace(psFormat, uColorSpace,
                                                   uSampleRange);
   psShared->eChromaUInterp = PVRDRIChromaSittingToIMGInterp(psFormat,
                                                             uHorizSiting);
   psShared->eChromaVInterp = PVRDRIChromaSittingToIMGInterp(psFormat,
                                                             uVertSiting);

   *puError = PVRDRI_IMAGE_ERROR_SUCCESS;

   return psShared;

ErrorDestroyImage:
   DestroyImageShared(psShared);

   return NULL;
}

static struct PVRDRIImageShared *
CreateImageShared(struct DRISUPScreen *psPVRScreen, int iWidth, int iHeight,
                  int iFourCC, unsigned int use, int *piStride)
{
   struct PVRDRIImageShared *psShared;
   const PVRDRIImageFormat *psFormat;
   unsigned int uStride;
   unsigned int uBPP;

   if ((use & PVDRI_BUFFER_USE_CURSOR)
       && (use & PVDRI_BUFFER_USE_SCANOUT)) {
      return NULL;
   }

   psFormat = PVRDRIFourCCToImageFormat(psPVRScreen, iFourCC);
   if (!psFormat) {
      errorMessage("%s: Unsupported DRI image format (format = 0x%X)",
                   __func__, iFourCC);
      return NULL;
   }

   if (psFormat->uiNumPlanes != 1) {
      errorMessage("%s: Only single plane formats are supported (format 0x%X has %u planes)",
          __func__, iFourCC, psFormat->uiNumPlanes);
      return NULL;
   }

   uBPP = PVRDRIPixFmtGetBPP(psFormat->eIMGPixelFormat);

   psShared = CommonImageSharedSetup(psPVRScreen, PVRDRI_IMAGE);
   if (!psShared)
      return NULL;

   psShared->psBuffer =
      PVRDRIBufferCreate(psPVRScreen->psImpl, iWidth, iHeight, uBPP, use,
                         &uStride);
   if (!psShared->psBuffer) {
      errorMessage("%s: Failed to create buffer", __func__);
      goto ErrorDestroyImage;
   }

   psShared->psFormat = psFormat;

   *piStride = uStride;

   return psShared;

ErrorDestroyImage:
   DestroyImageShared(psShared);

   return NULL;
}

static struct PVRDRIImageShared *
CreateImageSharedWithModifiers(struct DRISUPScreen *psPVRScreen,
                               int iWidth, int iHeight, int iFourCC,
                               const uint64_t *puModifiers,
                               unsigned int uModifierCount, int *piStride)
{
   struct PVRDRIImageShared *psShared;
   const PVRDRIImageFormat *psFormat;
   unsigned int uStride;

   psFormat = PVRDRIFourCCToImageFormat(psPVRScreen, iFourCC);
   if (!psFormat) {
      errorMessage("%s: Unsupported DRI image format (format = 0x%X)",
                   __func__, iFourCC);
      return NULL;
   }

   psShared = CommonImageSharedSetup(psPVRScreen, PVRDRI_IMAGE);
   if (!psShared)
      return NULL;

   psShared->psBuffer =
      PVRDRIBufferCreateWithModifiers(psPVRScreen->psImpl, iWidth, iHeight,
                                      psFormat->iDRIFourCC,
                                      psFormat->eIMGPixelFormat,
                                      puModifiers, uModifierCount, &uStride);
   if (!psShared->psBuffer) {
      errorMessage("%s: Failed to create buffer", __func__);
      goto ErrorDestroyImage;
   }

   psShared->psFormat = psFormat;

   *piStride = uStride;

   return psShared;

ErrorDestroyImage:
   DestroyImageShared(psShared);

   return NULL;
}

static struct PVRDRIImageShared *
RefImageShared(struct PVRDRIImageShared *psShared)
{
   int iRefCount = p_atomic_inc_return(&psShared->iRefCount);

   (void) iRefCount;
   assert(iRefCount > 1);

   return psShared;
}

static struct PVRDRIImageShared *
CreateImageSharedForSubImage(struct PVRDRIImageShared *psParent, int iPlane)
{
   struct PVRDRIImageShared *psShared;
   struct PVRDRIImageShared *psAncestor;
   PVRDRIBufferImpl *psBuffer = NULL;
   IMG_PIXFMT eIMGPixelFormat;

   /* Sub-images represent a single plane in the parent image */
   if (!psParent->psBuffer)
      return NULL;

   /*
    * The ancestor image is the owner of the original buffer that will back
    * the new image. The parent image may be a child of that image itself. The
    * ancestor image must not be destroyed until all the child images that
    * refer to it have been destroyed. A reference will be taken on the
    * ancestor to ensure that is the case.  We must distinguish between the
    * parent's buffer and the ancestor's buffer. For example, plane 0 in the
    * parent is not necessarily plane 0 in the ancestor.
    */
   psAncestor = psParent;
   if (psAncestor->psAncestor) {
      psAncestor = psAncestor->psAncestor;

      assert(!psAncestor->psAncestor);
   }

   psBuffer = PVRDRISubBufferCreate(psParent->psPVRScreen->psImpl,
                                    psParent->psBuffer, iPlane);
   if (!psBuffer)
      return NULL;

   psShared = CommonImageSharedSetup(NULL, PVRDRI_IMAGE_SUBIMAGE);
   if (!psShared)
      goto ErrorDestroyBuffer;

   psShared->psAncestor = RefImageShared(psAncestor);
   psShared->psBuffer = psBuffer;
   psShared->psPVRScreen = psParent->psPVRScreen;

   eIMGPixelFormat = psParent->psFormat->sPlanes[iPlane].eIMGPixelFormat;

   psShared->psFormat =
      PVRDRIIMGPixelFormatToImageFormat(psParent->psPVRScreen,
                                        eIMGPixelFormat);

   assert(psShared->psFormat);

   return psShared;

ErrorDestroyBuffer:
   PVRDRIBufferDestroy(psBuffer);
   return NULL;
}

static __DRIimage *
CommonImageSetup(void *pvLoaderPrivate)
{
   __DRIimage *psImage;

   psImage = calloc(1, sizeof(*psImage));
   if (!psImage)
      return NULL;

   psImage->pvLoaderPrivate = pvLoaderPrivate;
   psImage->iRefCount = 1;

   return psImage;
}

void
DRIMODDestroyImage(__DRIimage *psImage)
{
   int iRefCount = p_atomic_dec_return(&psImage->iRefCount);

   assert(iRefCount >= 0);

   if (iRefCount > 0)
      return;

   if (psImage->psShared)
      DestroyImageShared(psImage->psShared);

   PVRDRIEGLImageFree(psImage->psEGLImage);
   free(psImage);
}

__DRIimage *
DRIMODCreateImageFromName(struct DRISUPScreen *psPVRScreen,
                          int iWidth, int iHeight, int iFourCC, int iName,
                          int iPitch, void *pvLoaderPrivate)
{
   const PVRDRIImageFormat *psFormat;
   int iStride;
   int iOffset;

   psFormat = PVRDRIFourCCToImageFormat(psPVRScreen, iFourCC);
   if (!psFormat) {
      errorMessage("%s: Unsupported DRI image FourCC (format = 0x%X)",
                   __func__, iFourCC);
      return NULL;
   }

   iStride = iPitch * PVRDRIPixFmtGetBlockSize(psFormat->eIMGPixelFormat);
   iOffset = 0;

   return DRIMODCreateImageFromNames(psPVRScreen, iWidth, iHeight, iFourCC,
                                     &iName, 1, &iStride, &iOffset,
                                     pvLoaderPrivate);
}

__DRIimage *
DRIMODCreateImageFromRenderBuffer2(struct DRISUPContext *psPVRContext,
                                   int iRenderBuffer, void *pvLoaderPrivate,
                                   unsigned int *puError)
{
   struct DRISUPScreen *psPVRScreen = psPVRContext->psPVRScreen;
   unsigned int uError;
   IMGEGLImage *psEGLImage;
   __DRIimage *psImage;

   psImage = CommonImageSetup(pvLoaderPrivate);
   if (!psImage) {
      *puError = PVRDRI_IMAGE_ERROR_BAD_ALLOC;
      return NULL;
   }

   psEGLImage = PVRDRIEGLImageCreate();
   if (!psEGLImage) {
      DRIMODDestroyImage(psImage);

      *puError = PVRDRI_IMAGE_ERROR_BAD_ALLOC;
      return NULL;
   }

   uError = PVRDRIGetImageSource(psPVRContext->eAPI, psPVRScreen->psImpl,
                                 psPVRContext->psImpl, PVRDRI_GL_RENDERBUFFER,
                                 (uintptr_t) iRenderBuffer, 0, psEGLImage);
   if (uError != PVRDRI_IMAGE_ERROR_SUCCESS) {
      PVRDRIEGLImageFree(psEGLImage);
      DRIMODDestroyImage(psImage);

      *puError = uError;
      return NULL;
   }

   PVRDRIEGLImageSetCallbackData(psEGLImage, psImage);

   /*
    * We can't destroy the image after this point, as the
    * renderbuffer now has a reference to it.
    */
   psImage->psShared =
      CreateImageSharedFromEGLImage(psPVRScreen, psEGLImage,
                                    PVRDRI_EGLIMAGE_IMGEGL);
   if (!psImage->psShared) {
      *puError = PVRDRI_IMAGE_ERROR_BAD_ALLOC;
      return NULL;
   }

   psImage->psEGLImage = PVRDRIEGLImageDup(psImage->psShared->psEGLImage);
   if (!psImage->psEGLImage) {
      *puError = PVRDRI_IMAGE_ERROR_BAD_ALLOC;
      return NULL;
   }

   psImage->iRefCount++;

   *puError = PVRDRI_IMAGE_ERROR_SUCCESS;
   return psImage;
}


__DRIimage *
DRIMODCreateImageFromRenderbuffer(struct DRISUPContext *psPVRContext,
                                  int iRenderBuffer, void *pvLoaderPrivate)
{
   unsigned int uError;

   return DRIMODCreateImageFromRenderBuffer2(psPVRContext, iRenderBuffer,
                                             pvLoaderPrivate, &uError);
}

__DRIimage *
DRIMODCreateImage(struct DRISUPScreen *psPVRScreen, int iWidth, int iHeight,
                  int iFourCC, unsigned int uUse, void *pvLoaderPrivate)
{
   __DRIimage *psImage;
   struct PVRDRIImageShared *psShared;
   IMG_PIXFMT eIMGPixelFormat;
   int iStride;

   psImage = CommonImageSetup(pvLoaderPrivate);
   if (!psImage)
      return NULL;

   psShared = CreateImageShared(psPVRScreen, iWidth, iHeight, iFourCC, uUse,
                                &iStride);
   if (!psShared) {
      DRIMODDestroyImage(psImage);
      return NULL;
   }

   psImage->psShared = psShared;
   eIMGPixelFormat = psShared->psFormat->eIMGPixelFormat;

   psImage->psEGLImage =
      PVRDRIEGLImageCreateFromBuffer(iWidth, iHeight, iStride,
                                     eIMGPixelFormat, psShared->eColourSpace,
                                     psShared->eChromaUInterp,
                                     psShared->eChromaVInterp,
                                     psShared->psBuffer);
   if (!psImage->psEGLImage) {
      DRIMODDestroyImage(psImage);
      return NULL;
   }

   PVRDRIEGLImageSetCallbackData(psImage->psEGLImage, psImage);

   return psImage;
}

__DRIimage *
DRIMODCreateImageWithModifiers(struct DRISUPScreen *psPVRScreen,
                               int iWidth, int iHeight, int iFourCC,
                               const uint64_t *puModifiers,
                               const unsigned int uModifierCount,
                               void *pvLoaderPrivate)
{
   __DRIimage *psImage;
   struct PVRDRIImageShared *psShared;
   IMG_PIXFMT eIMGPixelFormat;
   int iStride;

   psImage = CommonImageSetup(pvLoaderPrivate);
   if (!psImage)
      return NULL;

   psShared = CreateImageSharedWithModifiers(psPVRScreen,
                                             iWidth, iHeight, iFourCC,
                                             puModifiers, uModifierCount,
                                             &iStride);
   if (!psShared) {
      DRIMODDestroyImage(psImage);
      return NULL;
   }

   psImage->psShared = psShared;
   eIMGPixelFormat = psShared->psFormat->eIMGPixelFormat;

   psImage->psEGLImage =
      PVRDRIEGLImageCreateFromBuffer(iWidth, iHeight, iStride,
                                     eIMGPixelFormat, psShared->eColourSpace,
                                     psShared->eChromaUInterp,
                                     psShared->eChromaVInterp,
                                     psShared->psBuffer);
   if (!psImage->psEGLImage) {
      DRIMODDestroyImage(psImage);
      return NULL;
   }

   PVRDRIEGLImageSetCallbackData(psImage->psEGLImage, psImage);

   return psImage;
}

bool
DRIMODQueryImage(__DRIimage *psImage, int attrib, int *value_ptr)
{
   struct PVRDRIImageShared *psShared = psImage->psShared;
   PVRDRIBufferAttribs sAttribs;
   int value;
   uint64_t ulValue;

   PVRDRIEGLImageGetAttribs(psImage->psEGLImage, &sAttribs);

   if (attrib == PVRDRI_IMAGE_ATTRIB_HANDLE ||
       attrib == PVRDRI_IMAGE_ATTRIB_NAME ||
       attrib == PVRDRI_IMAGE_ATTRIB_FD ||
       attrib == PVRDRI_IMAGE_ATTRIB_OFFSET) {
      if (!psShared->psFormat)
         return false;

      switch (psShared->psFormat->iDRIComponents) {
      case PVRDRI_IMAGE_COMPONENTS_R:
      case PVRDRI_IMAGE_COMPONENTS_RG:
      case PVRDRI_IMAGE_COMPONENTS_RGB:
      case PVRDRI_IMAGE_COMPONENTS_RGBA:
      case PVRDRI_IMAGE_COMPONENTS_EXTERNAL:
         break;
      default:
         return false;
      }
   }

   switch (attrib) {
   case PVRDRI_IMAGE_ATTRIB_STRIDE:
      *value_ptr = sAttribs.uiStrideInBytes;
      break;
   case PVRDRI_IMAGE_ATTRIB_HANDLE:
      value = PVRDRIBufferGetHandle(psShared->psBuffer);
      if (value == -1)
         return false;

      *value_ptr = value;
      break;
   case PVRDRI_IMAGE_ATTRIB_NAME:
      value = PVRDRIBufferGetName(psShared->psBuffer);
      if (value == -1)
         return false;

      *value_ptr = value;
      break;
   case PVRDRI_IMAGE_ATTRIB_FORMAT:
      /* The caller should use PVRDRI_IMAGE_ATTRIB_FOURCC, and convert. */
      return false;
   case PVRDRI_IMAGE_ATTRIB_WIDTH:
      *value_ptr = sAttribs.uiWidth;
      break;
   case PVRDRI_IMAGE_ATTRIB_HEIGHT:
      *value_ptr = sAttribs.uiHeight;
      break;
   case PVRDRI_IMAGE_ATTRIB_COMPONENTS:
      if (!psShared->psFormat || !psShared->psFormat->iDRIComponents)
         return false;

      *value_ptr = psShared->psFormat->iDRIComponents;
      break;
   case PVRDRI_IMAGE_ATTRIB_FD:
      value = PVRDRIBufferGetFd(psShared->psBuffer);
      if (value == -1)
         return false;

      *value_ptr = value;
      break;
   case PVRDRI_IMAGE_ATTRIB_FOURCC:
      *value_ptr = psShared->psFormat->iDRIFourCC;
      break;
   case PVRDRI_IMAGE_ATTRIB_NUM_PLANES:
      *value_ptr = (int) psShared->psFormat->uiNumPlanes;
      break;
   case PVRDRI_IMAGE_ATTRIB_OFFSET:
      *value_ptr = PVRDRIBufferGetOffset(psShared->psBuffer);
      break;
   case PVRDRI_IMAGE_ATTRIB_MODIFIER_LOWER:
      ulValue = PVRDRIBufferGetModifier(psShared->psBuffer);
      *value_ptr = (int) (ulValue & 0xffffffff);
      break;
   case PVRDRI_IMAGE_ATTRIB_MODIFIER_UPPER:
      ulValue = PVRDRIBufferGetModifier(psShared->psBuffer);
      *value_ptr = (int) ((ulValue >> 32) & 0xffffffff);
      break;
   default:
      return false;
   }

   return true;
}

__DRIimage *
DRIMODDupImage(__DRIimage *psSrc, void *pvLoaderPrivate)
{
   __DRIimage *psDst;

   psDst = CommonImageSetup(pvLoaderPrivate);
   if (!psDst)
      return NULL;

   psDst->psShared = RefImageShared(psSrc->psShared);

   psDst->psEGLImage = PVRDRIEGLImageDup(psSrc->psEGLImage);
   if (!psDst->psEGLImage) {
      DRIMODDestroyImage(psDst);
      return NULL;
   }

   PVRDRIEGLImageSetCallbackData(psDst->psEGLImage, psDst);

   return psDst;
}

bool
DRIMODValidateImageUsage(__DRIimage *psImage, unsigned int uUse)
{
   struct PVRDRIImageShared *psShared = psImage->psShared;

   if (uUse & (PVDRI_BUFFER_USE_SCANOUT | PVDRI_BUFFER_USE_CURSOR)) {
      uint64_t uModifier;

      /*
       * We are extra strict in this case as an application may ask for a
       * handle so that the memory can be wrapped as a framebuffer/used as a
       * cursor and this can only be done on a card node.
       */
      if (drmGetNodeTypeFromFd(psShared->psPVRScreen->iFD) != DRM_NODE_PRIMARY)
         return false;

      uModifier = PVRDRIBufferGetModifier(psShared->psBuffer);
      if (uModifier != DRM_FORMAT_MOD_INVALID &&
          uModifier != DRM_FORMAT_MOD_LINEAR)
         return false;
   } else if (uUse & (PVDRI_BUFFER_USE_SHARE)) {
      /*
       * We are less strict in this case as it's possible to share buffers
       * using prime (but not flink) on a render node so we only need to know
       * whether or not the FD belongs to the display.
       */
      if (PVRDRIGetDeviceTypeFromFd(psShared->psPVRScreen->iFD) !=
          PVRDRI_DEVICE_TYPE_DISPLAY)
         return false;
   }

   return true;
}

__DRIimage *
DRIMODCreateImageFromNames(struct DRISUPScreen *psPVRScreen,
                           int iWidth, int iHeight, int iFourCC,
                           int *piNames, int iNumNames,
                           int *piStrides, int *piOffsets,
                           void *pvLoaderPrivate)
{
   __DRIimage *psImage;
   struct PVRDRIImageShared *psShared;
   IMG_PIXFMT eIMGPixelFormat;
   int iStride;

   psImage = CommonImageSetup(pvLoaderPrivate);
   if (!psImage)
      return NULL;

   psShared = CreateImageSharedFromNames(psPVRScreen,
                                         iWidth, iHeight, iFourCC,
                                         piNames, iNumNames,
                                         piStrides, piOffsets);
   if (!psShared) {
      DRIMODDestroyImage(psImage);
      return NULL;
   }

   psImage->psShared = psShared;
   eIMGPixelFormat = psShared->psFormat->eIMGPixelFormat;

   if (psShared->psFormat->uiNumPlanes == 1)
      iStride = piStrides[0];
   else
      iStride = iWidth * PVRDRIPixFmtGetBlockSize(eIMGPixelFormat);

   psImage->psEGLImage =
      PVRDRIEGLImageCreateFromBuffer(iWidth, iHeight, iStride,
                                     eIMGPixelFormat, psShared->eColourSpace,
                                     psShared->eChromaUInterp,
                                     psShared->eChromaVInterp,
                                     psShared->psBuffer);
   if (!psImage->psEGLImage) {
      DRIMODDestroyImage(psImage);
      return NULL;
   }

   PVRDRIEGLImageSetCallbackData(psImage->psEGLImage, psImage);

   return psImage;
}

__DRIimage *
DRIMODFromPlanar(__DRIimage *psSrc, int iPlane, void *pvLoaderPrivate)
{
   __DRIimage *psDst;
   struct PVRDRIImageShared *psShared;

   psDst = CommonImageSetup(pvLoaderPrivate);
   if (!psDst)
      return NULL;

   psShared = CreateImageSharedForSubImage(psSrc->psShared, iPlane);
   if (!psShared) {
      if (iPlane != 0) {
         errorMessage("%s: plane %d not supported", __func__, iPlane);
      } else {
         psDst->psShared = RefImageShared(psSrc->psShared);
         psDst->psEGLImage = PVRDRIEGLImageDup(psSrc->psEGLImage);
      }
   } else {
      IMG_PIXFMT eIMGPixelFormat;

      psDst->psShared = psShared;
      eIMGPixelFormat = psShared->psFormat->eIMGPixelFormat;

      psDst->psEGLImage =
         PVRDRIEGLImageCreateFromSubBuffer(eIMGPixelFormat, psShared->psBuffer);
   }

   if (!psDst->psEGLImage) {
      DRIMODDestroyImage(psDst);
      return NULL;
   }

   PVRDRIEGLImageSetCallbackData(psDst->psEGLImage, psDst);

   return psDst;
}

__DRIimage *
DRIMODCreateImageFromTexture(struct DRISUPContext *psPVRContext, int iTarget,
                             unsigned int uTexture, int iDepth, int iLevel,
                             unsigned int *puError, void *pvLoaderPrivate)
{
   IMGEGLImage *psEGLImage;
   __DRIimage *psImage;
   uint32_t iEGLTarget;
   unsigned int uError;

   switch (iTarget) {
   case PVRDRI_GL_TEXTURE_2D:
      iEGLTarget = iTarget;
      break;
   case PVRDRI_GL_TEXTURE_CUBE_MAP_POSITIVE_X:
      iEGLTarget = iTarget + iDepth;
      break;
   default:
      errorMessage("%s: EGL GL texture %d is not supported",
                   __func__, iTarget);
      *puError = PVRDRI_IMAGE_ERROR_BAD_PARAMETER;
      return NULL;
   }

   psImage = CommonImageSetup(pvLoaderPrivate);
   if (!psImage)
      return NULL;

   psEGLImage = PVRDRIEGLImageCreate();
   if (!psEGLImage) {
      DRIMODDestroyImage(psImage);
      return NULL;
   }

   uError = PVRDRIGetImageSource(psPVRContext->eAPI,
                                 psPVRContext->psPVRScreen->psImpl,
                                 psPVRContext->psImpl, iEGLTarget, uTexture,
                                 iLevel, psEGLImage);
   *puError = uError;

   if (uError != PVRDRI_IMAGE_ERROR_SUCCESS) {
      PVRDRIEGLImageFree(psEGLImage);
      DRIMODDestroyImage(psImage);
      return NULL;
   }

   PVRDRIEGLImageSetCallbackData(psEGLImage, psImage);

   /*
    * We can't destroy the image after this point, as the texture now has a
    * reference to it.
    */
   psImage->psShared =
      CreateImageSharedFromEGLImage(psPVRContext->psPVRScreen, psEGLImage,
                                    PVRDRI_EGLIMAGE_IMGEGL);
   if (!psImage->psShared)
      return NULL;

   psImage->psEGLImage = PVRDRIEGLImageDup(psImage->psShared->psEGLImage);
   if (!psImage->psEGLImage)
      return NULL;

   psImage->iRefCount++;

   return psImage;
}

__DRIimage *
DRIMODCreateImageFromFDs(struct DRISUPScreen *psPVRScreen,
                         int iWidth, int iHeight, int iFourCC,
                         int *piFDs, int iNumFDs,
                         int *piStrides, int *piOffsets,
                         void *pvLoaderPrivate)
{
   unsigned int uError;

   return DRIMODCreateImageFromDMABufs(psPVRScreen, iWidth, iHeight, iFourCC,
                                       piFDs, iNumFDs, piStrides, piOffsets,
                                       PVRDRI_YUV_COLOR_SPACE_UNDEFINED,
                                       PVRDRI_YUV_RANGE_UNDEFINED,
                                       PVRDRI_YUV_CHROMA_SITING_UNDEFINED,
                                       PVRDRI_YUV_CHROMA_SITING_UNDEFINED,
                                       &uError, pvLoaderPrivate);
}

__DRIimage *
DRIMODCreateImageFromBuffer(struct DRISUPContext *psPVRContext, int iTarget,
                            void *pvBuffer, unsigned int *puError,
                            void *pvLoaderPrivate)
{
   IMGEGLImage *psEGLImage;
   __DRIimage *psImage;

   switch (iTarget) {
   case PVRDRI_CL_IMAGE_IMG:
      break;
   default:
      errorMessage("%s: Target %d is not supported", __func__, iTarget);
      *puError = PVRDRI_IMAGE_ERROR_BAD_PARAMETER;
      return NULL;
   }

   psImage = CommonImageSetup(pvLoaderPrivate);
   if (!psImage) {
      *puError = PVRDRI_IMAGE_ERROR_BAD_ALLOC;
      return NULL;
   }

   psEGLImage = PVRDRIEGLImageCreate();
   if (!psEGLImage) {
      DRIMODDestroyImage(psImage);

      *puError = PVRDRI_IMAGE_ERROR_BAD_ALLOC;
      return NULL;
   }

   *puError = PVRDRIGetImageSource(PVRDRI_API_CL,
                                   psPVRContext->psPVRScreen->psImpl,
                                   psPVRContext->psImpl, iTarget,
                                   (uintptr_t) pvBuffer, 0, psEGLImage);
   if (*puError != PVRDRI_IMAGE_ERROR_SUCCESS) {
      PVRDRIEGLImageFree(psEGLImage);
      DRIMODDestroyImage(psImage);
      return NULL;
   }

   PVRDRIEGLImageSetCallbackData(psEGLImage, psImage);

   /*
    * We can't destroy the image after this point, as the OCL image now has a
    * reference to it.
    */
   psImage->psShared =
      CreateImageSharedFromEGLImage(psPVRContext->psPVRScreen, psEGLImage,
                                    PVRDRI_EGLIMAGE_IMGOCL);
   if (!psImage->psShared) {
      *puError = PVRDRI_IMAGE_ERROR_BAD_ALLOC;
      return NULL;
   }

   psImage->psEGLImage = PVRDRIEGLImageDup(psImage->psShared->psEGLImage);
   if (!psImage->psEGLImage) {
      *puError = PVRDRI_IMAGE_ERROR_BAD_ALLOC;
      return NULL;
   }

   psImage->iRefCount++;

   *puError = PVRDRI_IMAGE_ERROR_SUCCESS;

   return psImage;
}

__DRIimage *
DRIMODCreateImageFromDMABufs2(struct DRISUPScreen *psPVRScreen,
                              int iWidth, int iHeight,
                              int iFourCC, uint64_t uModifier,
                              int *piFDs, int iNumFDs,
                              int *piStrides, int *piOffsets,
                              unsigned int uColorSpace,
                              unsigned int uSampleRange,
                              unsigned int uHorizSiting,
                              unsigned int uVertSiting,
                              unsigned int *puError,
                              void *pvLoaderPrivate)
{
   __DRIimage *psImage;
   struct PVRDRIImageShared *psShared;
   IMG_PIXFMT eIMGPixelFormat;

   psImage = CommonImageSetup(pvLoaderPrivate);
   if (!psImage) {
      *puError = PVRDRI_IMAGE_ERROR_BAD_ALLOC;
      return NULL;
   }

   psShared = CreateImageSharedFromDMABufs(psPVRScreen, iWidth, iHeight,
                                           iFourCC, uModifier, piFDs, iNumFDs,
                                           piStrides, piOffsets,
                                           uColorSpace, uSampleRange,
                                           uHorizSiting, uVertSiting,
                                           puError);
   if (!psShared) {
      DRIMODDestroyImage(psImage);
      return NULL;
   }

   psImage->psShared = psShared;
   eIMGPixelFormat = psShared->psFormat->eIMGPixelFormat;

   psImage->psEGLImage =
      PVRDRIEGLImageCreateFromBuffer(iWidth, iHeight, piStrides[0],
                                     eIMGPixelFormat, psShared->eColourSpace,
                                     psShared->eChromaUInterp,
                                     psShared->eChromaVInterp,
                                     psShared->psBuffer);
   if (!psImage->psEGLImage) {
      DRIMODDestroyImage(psImage);
      *puError = PVRDRI_IMAGE_ERROR_BAD_ALLOC;
      return NULL;
   }

   PVRDRIEGLImageSetCallbackData(psImage->psEGLImage, psImage);

   *puError = PVRDRI_IMAGE_ERROR_SUCCESS;

   return psImage;
}

__DRIimage *
DRIMODCreateImageFromDMABufs(struct DRISUPScreen *psPVRScreen,
                             int iWidth, int iHeight, int iFourCC,
                             int *piFDs, int iNumFDs,
                             int *piStrides, int *piOffsets,
                             unsigned int uColorSpace,
                             unsigned int uSampleRange,
                             unsigned int uHorizSiting,
                             unsigned int uVertSiting,
                             unsigned int *puError,
                             void *pvLoaderPrivate)
{
   return DRIMODCreateImageFromDMABufs2(psPVRScreen, iWidth, iHeight,
                                        iFourCC, DRM_FORMAT_MOD_INVALID,
                                        piFDs, iNumFDs,
                                        piStrides, piOffsets,
                                        uColorSpace, uSampleRange,
                                        uHorizSiting, uVertSiting,
                                        puError, pvLoaderPrivate);
}

void
PVRDRIRefImage(__DRIimage *psImage)
{
   int iRefCount = p_atomic_inc_return(&psImage->iRefCount);

   (void) iRefCount;
   assert(iRefCount > 1);
}

void
PVRDRIUnrefImage(__DRIimage *psImage)
{
   DRIMODDestroyImage(psImage);
}

PVRDRIImageType
PVRDRIImageGetSharedType(__DRIimage *psImage)
{
   return psImage->psShared->eType;
}

PVRDRIBufferImpl *
PVRDRIImageGetSharedBuffer(__DRIimage *psImage)
{
   assert(psImage->psShared->eType != PVRDRI_IMAGE_FROM_EGLIMAGE);

   return psImage->psShared->psBuffer;
}

IMGEGLImage *
PVRDRIImageGetSharedEGLImage(__DRIimage *psImage)
{
   assert(psImage->psShared->eType == PVRDRI_IMAGE_FROM_EGLIMAGE);

   return psImage->psShared->psEGLImage;
}

IMGEGLImage *
PVRDRIImageGetEGLImage(__DRIimage *psImage)
{
   return psImage->psEGLImage;
}

__DRIimage *
PVRDRIScreenGetDRIImage(void *hEGLImage)
{
   struct DRISUPScreen *psPVRScreen;

   psPVRScreen = DRIMODThreadGetCurrentScreen();
   if (!psPVRScreen)
      return NULL;

   return MODSUPLookupEGLImage(psPVRScreen->psDRIScreen, hEGLImage,
                               psPVRScreen->pvLoaderPrivate);
}

void
DRIMODBlitImage(struct DRISUPContext *psPVRContext,
                __DRIimage *psDst, __DRIimage *psSrc,
                int iDstX0, int iDstY0, int iDstWidth, int iDstHeight,
                int iSrcX0, int iSrcY0, int iSrcWidth, int iSrcHeight,
                int iFlushFlag)
{
   bool bRes;

   bRes = PVRDRIBlitEGLImage(psPVRContext->psPVRScreen->psImpl,
                             psPVRContext->psImpl,
                             psDst->psEGLImage, psDst->psShared->psBuffer,
                             psSrc->psEGLImage, psSrc->psShared->psBuffer,
                             iDstX0, iDstY0, iDstWidth, iDstHeight,
                             iSrcX0, iSrcY0, iSrcWidth, iSrcHeight,
                             iFlushFlag);
   if (!bRes)
      __driUtilMessage("%s: PVRDRIBlitEGLImage failed", __func__);
}

int
DRIMODGetImageCapabilities(struct DRISUPScreen *psPVRScreen)
{
   (void) psPVRScreen;

   return PVRDRI_IMAGE_CAP_GLOBAL_NAMES;
}

void *
DRIMODMapImage(struct DRISUPContext *psPVRContext, __DRIimage *psImage,
               int iX0, int iY0, int iWidth, int iHeight,
               unsigned int iFlags, int *iStride, void **ppvData)
{
   return PVRDRIMapEGLImage(psPVRContext->psPVRScreen->psImpl,
                            psPVRContext->psImpl,
                            psImage->psEGLImage, psImage->psShared->psBuffer,
                            iX0, iY0, iWidth, iHeight, iFlags, iStride,
                            ppvData);
}

void
DRIMODUnmapImage(struct DRISUPContext *psPVRContext, __DRIimage *psImage,
                 void *pvData)
{
   bool bRes;

   bRes = PVRDRIUnmapEGLImage(psPVRContext->psPVRScreen->psImpl,
                              psPVRContext->psImpl, psImage->psEGLImage,
                              psImage->psShared->psBuffer, pvData);
   if (!bRes)
      __driUtilMessage("%s: PVRDRIUnmapEGLImage failed", __func__);
}
