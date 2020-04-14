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
#include "pvrdri_mod.h"

static inline void
PVRDRIMarkRenderSurfaceAsInvalid(struct DRISUPDrawable *psPVRDrawable)
{
   struct DRISUPContext *psPVRContext = psPVRDrawable->psPVRContext;

   if (psPVRContext) {
      struct DRISUPScreen *psPVRScreen = psPVRContext->psPVRScreen;

      PVRDRIEGLMarkRendersurfaceInvalid(psPVRContext->eAPI,
                                        psPVRScreen->psImpl,
                                        psPVRContext->psImpl);
   }
}

/*************************************************************************//*!
 PVR drawable local functions (image driver loader)
 *//**************************************************************************/

static inline void
PVRDrawableImageDestroy(struct DRISUPDrawable *psPVRDrawable)
{
   if (psPVRDrawable->psImage) {
      PVRDRIUnrefImage(psPVRDrawable->psImage);
      psPVRDrawable->psImage = NULL;
   }
}

static inline void
PVRDrawableImageAccumDestroy(struct DRISUPDrawable *psPVRDrawable)
{
   if (psPVRDrawable->psImageAccum) {
      PVRDRIUnrefImage(psPVRDrawable->psImageAccum);
      psPVRDrawable->psImageAccum = NULL;
   }
}

static void
PVRDrawableImageUpdate(struct DRISUPDrawable *psPVRDrawable)
{
   if (psPVRDrawable->psImage != psPVRDrawable->psDRI) {
      assert(PVRDRIImageGetSharedBuffer(psPVRDrawable->psDRI) != NULL);

      PVRDrawableImageDestroy(psPVRDrawable);

      PVRDRIRefImage(psPVRDrawable->psDRI);
      psPVRDrawable->psImage = psPVRDrawable->psDRI;
   }

   if (psPVRDrawable->psImageAccum != psPVRDrawable->psDRIAccum) {
      PVRDrawableImageAccumDestroy(psPVRDrawable);

      if (psPVRDrawable->psDRIAccum) {
         PVRDRIRefImage(psPVRDrawable->psDRIAccum);
         psPVRDrawable->psImageAccum = psPVRDrawable->psDRIAccum;
      }
   }
}

/*************************************************************************//*!
Function Name            : PVRImageDrawableGetNativeInfo
Inputs                   : psPVRDrawable
Returns                  : Boolean
Description              : Update native drawable information.
 *//**************************************************************************/
static bool
PVRImageDrawableGetNativeInfo(struct DRISUPDrawable *psPVRDrawable)
{
   struct PVRDRIImageList sImages;
   uint32_t uBufferMask;

   if (psPVRDrawable->bDoubleBufferMode)
      uBufferMask = PVRDRI_IMAGE_BUFFER_BACK;
   else
      uBufferMask = PVRDRI_IMAGE_BUFFER_FRONT;

   uBufferMask |= PVRDRI_IMAGE_BUFFER_PREV;

   if (!MODSUPGetBuffers(psPVRDrawable->psDRIDrawable,
                         psPVRDrawable->psFormat->iDRIFourCC,
                         NULL, psPVRDrawable->pvLoaderPrivate,
                         uBufferMask, &sImages)) {
      errorMessage("%s: Image get buffers call failed", __func__);
      return false;
   }

   psPVRDrawable->psDRI =
      (sImages.uImageMask & PVRDRI_IMAGE_BUFFER_BACK) ?
      sImages.psBack : sImages.psFront;

   if (sImages.uImageMask & PVRDRI_IMAGE_BUFFER_PREV)
      psPVRDrawable->psDRIAccum = sImages.psPrev;
   else
      psPVRDrawable->psDRIAccum = NULL;

   return true;
}

/*************************************************************************//*!
Function Name            : PVRImageDrawableCreate
Inputs                   : psPVRDrawable
Returns                  : Boolean
Description              : Create drawable
 *//**************************************************************************/
static bool
PVRImageDrawableCreate(struct DRISUPDrawable *psPVRDrawable)
{
   uint32_t uBytesPerPixel;
   PVRDRIBufferAttribs sBufferAttribs;

   if (!PVRImageDrawableGetNativeInfo(psPVRDrawable))
      return false;

   PVRDRIEGLImageGetAttribs(PVRDRIImageGetEGLImage(psPVRDrawable->psDRI),
                            &sBufferAttribs);
   uBytesPerPixel = PVRDRIPixFmtGetBlockSize(sBufferAttribs.ePixFormat);

   psPVRDrawable->uWidth = sBufferAttribs.uiWidth;
   psPVRDrawable->uHeight = sBufferAttribs.uiHeight;
   psPVRDrawable->uStride = sBufferAttribs.uiStrideInBytes;
   psPVRDrawable->uBytesPerPixel = uBytesPerPixel;

   PVRDrawableImageUpdate(psPVRDrawable);

   if (!PVREGLDrawableCreate(psPVRDrawable->psPVRScreen->psImpl,
                             psPVRDrawable->psImpl)) {
      errorMessage("%s: Couldn't create EGL drawable", __func__);
      return false;
   }

   return true;
}

/*************************************************************************//*!
Function Name            : PVRImageDrawableUpdate
Inputs                   : psPVRDrawable
Returns                  : Boolean
Description              : Update drawable
 *//**************************************************************************/
static bool
PVRImageDrawableUpdate(struct DRISUPDrawable *psPVRDrawable,
                       bool bAllowRecreate)
{
   uint32_t uBytesPerPixel;
   PVRDRIBufferAttribs sBufferAttribs;
   bool bRecreate;

   PVRDRIEGLImageGetAttribs(PVRDRIImageGetEGLImage(psPVRDrawable->psDRI),
                            &sBufferAttribs);
   uBytesPerPixel = PVRDRIPixFmtGetBlockSize(sBufferAttribs.ePixFormat);

   bRecreate =
      (!psPVRDrawable->bDoubleBufferMode &&
       psPVRDrawable->psImage != psPVRDrawable->psDRI) ||
      (psPVRDrawable->uWidth != sBufferAttribs.uiWidth) ||
      (psPVRDrawable->uHeight != sBufferAttribs.uiHeight) ||
      (psPVRDrawable->uStride != sBufferAttribs.uiStrideInBytes) ||
      (psPVRDrawable->uBytesPerPixel != uBytesPerPixel);

   if (bRecreate) {
      if (bAllowRecreate) {
         PVRDRIMarkRenderSurfaceAsInvalid(psPVRDrawable);

         psPVRDrawable->uWidth = sBufferAttribs.uiWidth;
         psPVRDrawable->uHeight = sBufferAttribs.uiHeight;
         psPVRDrawable->uStride = sBufferAttribs.uiStrideInBytes;
         psPVRDrawable->uBytesPerPixel = uBytesPerPixel;
      } else {
         return false;
      }
   }

   PVRDrawableImageUpdate(psPVRDrawable);

   if (bRecreate) {
      if (!PVREGLDrawableRecreate(psPVRDrawable->psPVRScreen->psImpl,
                                  psPVRDrawable->psImpl)) {
         errorMessage("%s: Couldn't recreate EGL drawable", __func__);
         return false;
      }
   }

   return true;
}

/*************************************************************************//*!
 PVR drawable local functions
 *//**************************************************************************/

/*************************************************************************//*!
Function Name    : PVRDRIDrawableUpdate
Inputs           : psPVRDrawable
Description      : Update drawable
 *//**************************************************************************/
static bool
PVRDRIDrawableUpdate(struct DRISUPDrawable *psPVRDrawable, bool bAllowRecreate)
{
   bool bRes;
   int iInfoInvalid = 0;

   /*
    * The test for bDrawableUpdating is needed because drawable parameters are
    * fetched (via KEGLGetDrawableParameters) when a drawable is recreated.
    * The test for bFlushInProgress is to prevent the drawable information
    * being updated during a flush, which could result in a call back into the
    * Mesa platform code during the processing for a buffer swap, which could
    * corrupt the platform state.
    */
   if (psPVRDrawable->bDrawableUpdating || psPVRDrawable->bFlushInProgress)
      return false;

   psPVRDrawable->bDrawableUpdating = true;

   if (psPVRDrawable->psPVRScreen->bUseInvalidate) {
      iInfoInvalid = p_atomic_read(&psPVRDrawable->iInfoInvalid);
      bRes = !iInfoInvalid;
      if (bRes)
         goto ExitNotUpdating;
   }

   bRes = PVRImageDrawableGetNativeInfo(psPVRDrawable);
   if (!bRes)
      goto ExitNotUpdating;

   bRes = PVRImageDrawableUpdate(psPVRDrawable, bAllowRecreate);
   if (bRes && iInfoInvalid)
      p_atomic_add(&psPVRDrawable->iInfoInvalid, -iInfoInvalid);

ExitNotUpdating:
   psPVRDrawable->bDrawableUpdating = false;
   return bRes;
}

/*************************************************************************//*!
 PVR drawable interface
 *//**************************************************************************/
bool
PVRDRIDrawableInit(struct DRISUPDrawable *psPVRDrawable)
{
   if (psPVRDrawable->bInitialised)
      return true;

   if (!PVRImageDrawableCreate(psPVRDrawable))
      return false;

   psPVRDrawable->bInitialised = true;

   return true;
}

void
PVRDRIDrawableDeinit(struct DRISUPDrawable *psPVRDrawable)
{
   (void) PVREGLDrawableDestroy(psPVRDrawable->psPVRScreen->psImpl,
                                psPVRDrawable->psImpl);

   PVRDrawableImageDestroy(psPVRDrawable);
   PVRDrawableImageAccumDestroy(psPVRDrawable);

   psPVRDrawable->bInitialised = false;
}

static bool
PVRDRIDrawableGetParameters(struct DRISUPDrawable *psPVRDrawable,
                            PVRDRIBufferImpl **ppsDstBuffer,
                            PVRDRIBufferImpl **ppsAccumBuffer)
{
   if (ppsDstBuffer || ppsAccumBuffer) {
      __DRIimage *psDstImage = psPVRDrawable->psImage;
      __DRIimage *psAccumImage = psPVRDrawable->psImageAccum;
      PVRDRIBufferImpl *psDstBuffer, *psAccumBuffer;

      psDstBuffer = PVRDRIImageGetSharedBuffer(psDstImage);
      if (!psDstBuffer) {
         errorMessage("%s: Couldn't get backing buffer", __func__);
         return false;
      }

      if (psAccumImage) {
         psAccumBuffer = PVRDRIImageGetSharedBuffer(psAccumImage);
         if (!psAccumBuffer)
            psAccumBuffer = psDstBuffer;
      } else {
         psAccumBuffer = psDstBuffer;
      }

      if (ppsDstBuffer)
         *ppsDstBuffer = psDstBuffer;

      if (ppsAccumBuffer)
         *ppsAccumBuffer = psAccumBuffer;
   }

   return true;
}

bool
PVRDRIDrawableQuery(const PVRDRIDrawable *psPVRDRIDrawable,
                    PVRDRIBufferAttrib eBufferAttrib, uint32_t *uiValueOut)
{
   const struct DRISUPDrawable *psPVRDrawable;

   if (!psPVRDRIDrawable || !uiValueOut)
      return false;

   psPVRDrawable = (const struct DRISUPDrawable *) psPVRDRIDrawable;

   switch (eBufferAttrib) {
   case PVRDRI_BUFFER_ATTRIB_TYPE:
      *uiValueOut = (uint32_t) psPVRDrawable->eType;
      return true;
   case PVRDRI_BUFFER_ATTRIB_WIDTH:
      *uiValueOut = (uint32_t) psPVRDrawable->uWidth;
      return true;
   case PVRDRI_BUFFER_ATTRIB_HEIGHT:
      *uiValueOut = (uint32_t) psPVRDrawable->uHeight;
      return true;
   case PVRDRI_BUFFER_ATTRIB_STRIDE:
      *uiValueOut = (uint32_t) psPVRDrawable->uStride;
      return true;
   case PVRDRI_BUFFER_ATTRIB_PIXEL_FORMAT:
      *uiValueOut = (uint32_t) psPVRDrawable->psFormat->eIMGPixelFormat;
      static_assert(sizeof(IMG_PIXFMT) <= sizeof(*uiValueOut),
                    "Type size mismatch");
      return true;
   case PVRDRI_BUFFER_ATTRIB_INVALID:
      errorMessage("%s: Invalid attribute", __func__);
      assert(0);
      return false;
   default:
      return false;
   }
}

bool
PVRDRIDrawableGetParametersV2(PVRDRIDrawable *psPVRDRIDrawable,
                              uint32_t uiFlags,
                              PVRDRIBufferImpl **ppsDstBuffer,
                              PVRDRIBufferImpl **ppsAccumBuffer)
{
   const bool bNoUpdate = uiFlags & PVRDRI_GETPARAMS_FLAG_NO_UPDATE;
   struct DRISUPDrawable *psPVRDrawable;

   psPVRDrawable = (struct DRISUPDrawable *) psPVRDRIDrawable;

   if (!bNoUpdate) {
      const bool bAllowRecreate =
         uiFlags & PVRDRI_GETPARAMS_FLAG_ALLOW_RECREATE;

      if (!PVRDRIDrawableUpdate(psPVRDrawable, bAllowRecreate))
         if (bAllowRecreate)
            return false;
   }

   return PVRDRIDrawableGetParameters(psPVRDrawable,
                                      ppsDstBuffer, ppsAccumBuffer);
}
