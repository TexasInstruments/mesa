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
#include <xf86drm.h>

#include "pvrdri_mod.h"

#define PVR_IMAGE_LOADER_VER_MIN 1

#define PVRDRI_FLUSH_WAIT_FOR_HW        (1U << 0)
#define PVRDRI_FLUSH_NEW_EXTERNAL_FRAME (1U << 1)
#define PVRDRI_FLUSH_ALL_SURFACES       (1U << 2)


/* We need to know the current screen in order to lookup EGL images. */
static __thread struct DRISUPScreen *gpsPVRScreen;

/*************************************************************************//*!
 Local functions
 *//**************************************************************************/

static inline bool
PVRDRIFlushBuffers(struct DRISUPContext *psPVRContext,
                   struct DRISUPDrawable *psPVRDrawable, uint32_t uiFlags)
{
   PVRDRIDrawableImpl *psDrawableImpl;
   bool bFlushAllSurfaces = (uiFlags & PVRDRI_FLUSH_ALL_SURFACES) != 0;
   bool bSwapBuffers = (uiFlags & PVRDRI_FLUSH_NEW_EXTERNAL_FRAME) != 0;
   bool bWaitForHW = (uiFlags & PVRDRI_FLUSH_WAIT_FOR_HW) != 0;

   psDrawableImpl = psPVRDrawable ? psPVRDrawable->psImpl : NULL;

   assert(!(uiFlags & ~(PVRDRI_FLUSH_WAIT_FOR_HW |
                        PVRDRI_FLUSH_NEW_EXTERNAL_FRAME |
                        PVRDRI_FLUSH_ALL_SURFACES)));

   return PVRDRIEGLFlushBuffers(psPVRContext->eAPI,
                                psPVRContext->psPVRScreen->psImpl,
                                psPVRContext->psImpl, psDrawableImpl,
                                bFlushAllSurfaces, bSwapBuffers, bWaitForHW);
}

void
PVRDRIFlushBuffersForSwap(struct DRISUPContext *psPVRContext,
                          struct DRISUPDrawable *psPVRDrawable)
{
   if (psPVRContext) {
      /*
       * The bFlushInProgress flag is intended to prevent new buffers being
       * fetched whilst a flush is in progress, which may corrupt the state
       * maintained by the Mesa platform code.
       */
      psPVRDrawable->bFlushInProgress = true;

      (void) PVRDRIFlushBuffers(psPVRContext,
                                psPVRDrawable,
                                PVRDRI_FLUSH_NEW_EXTERNAL_FRAME);

      psPVRDrawable->bFlushInProgress = false;
   }
}

static void
PVRDRIFlushBuffersGC(struct DRISUPContext *psPVRContext,
                     struct DRISUPDrawable *psPVRDrawable)
{
   (void) PVRDRIFlushBuffers(psPVRContext, psPVRDrawable,
                             PVRDRI_FLUSH_WAIT_FOR_HW |
                             PVRDRI_FLUSH_ALL_SURFACES);
}

static void
PVRContextUnbind(struct DRISUPContext *psPVRContext, bool bMakeUnCurrent,
                 bool bMarkSurfaceInvalid)
{
   struct DRISUPScreen *psPVRScreen = psPVRContext->psPVRScreen;
   struct DRISUPDrawable *psPVRDrawable = psPVRContext->psPVRDrawable;

   if (bMakeUnCurrent && !psPVRContext->bMakeUncurrentNoFlush) {
      uint32_t uiFlags = PVRDRI_FLUSH_ALL_SURFACES;

      (void) PVRDRIFlushBuffers(psPVRContext, psPVRDrawable, uiFlags);
   } else if (psPVRDrawable) {
      PVRDRIFlushBuffersGC(psPVRContext, psPVRDrawable);
   }

   if (bMakeUnCurrent)
      PVRDRIMakeUnCurrentGC(psPVRContext->eAPI, psPVRScreen->psImpl);

   if (psPVRDrawable != NULL) {
      if (bMarkSurfaceInvalid) {
         PVRDRIEGLMarkRendersurfaceInvalid(psPVRContext->eAPI,
                                           psPVRScreen->psImpl,
                                           psPVRContext->psImpl);
      }

      psPVRContext->psPVRDrawable = NULL;
      psPVRDrawable->psPVRContext = NULL;
   }
}

static inline void
PVRDrawableUnbindContext(struct DRISUPDrawable *psPVRDrawable)
{
   struct DRISUPContext *psPVRContext = psPVRDrawable->psPVRContext;

   if (psPVRContext)
      PVRContextUnbind(psPVRContext, false, true);
}

/*************************************************************************//*!
 Mesa driver API functions
 *//**************************************************************************/
struct DRISUPScreen *
DRIMODCreateScreen(struct __DRIscreenRec *psDRIScreen, int iFD,
                   bool bUseInvalidate, void *pvLoaderPrivate,
                   const struct __DRIconfigRec ***pppsConfigs,
                   int *piMaxGLES1Version, int *piMaxGLES2Version)
{
   struct DRISUPScreen *psPVRScreen;
   const struct __DRIconfigRec **ppsConfigs;

   psPVRScreen = calloc(1, sizeof(*psPVRScreen));
   if (psPVRScreen == NULL) {
      __driUtilMessage("%s: Couldn't allocate PVRDRIScreen", __func__);
      return NULL;
   }

   psPVRScreen->psDRIScreen = psDRIScreen;
   psPVRScreen->iFD = iFD;
   psPVRScreen->bUseInvalidate = bUseInvalidate;
   psPVRScreen->pvLoaderPrivate = pvLoaderPrivate;

   if (!PVRDRIGetMesaFormats(psPVRScreen))
      goto ErrorScreenFree;

   psPVRScreen->psImpl = PVRDRICreateScreenImpl(iFD);
   if (psPVRScreen->psImpl == NULL)
      goto ErrorScreenFree;

   if (!PVRDRIGetSupportedFormats(psPVRScreen))
      goto ErrorScreenImplDeinit;

   ppsConfigs = PVRDRICreateConfigs(psPVRScreen);
   if (ppsConfigs == NULL) {
      __driUtilMessage("%s: No framebuffer configs", __func__);
      goto ErrorDestroyFormatInfo;
   }

   *piMaxGLES1Version = PVRDRIAPIVersion(PVRDRI_API_GLES1,
                                         PVRDRI_API_SUB_NONE,
                                         psPVRScreen->psImpl);

   *piMaxGLES2Version = PVRDRIAPIVersion(PVRDRI_API_GLES2,
                                         PVRDRI_API_SUB_NONE,
                                         psPVRScreen->psImpl);

   *pppsConfigs = ppsConfigs;

   return psPVRScreen;

ErrorDestroyFormatInfo:
   PVRDRIDestroyFormatInfo(psPVRScreen);

ErrorScreenImplDeinit:
   PVRDRIDestroyScreenImpl(psPVRScreen->psImpl);

ErrorScreenFree:
   PVRDRIFreeMesaFormats(psPVRScreen);

   free(psPVRScreen);

   return NULL;
}

void
DRIMODDestroyScreen(struct DRISUPScreen *psPVRScreen)
{
   PVRDRIDestroyFormatInfo(psPVRScreen);
   PVRDRIDestroyScreenImpl(psPVRScreen->psImpl);
   PVRDRIFreeMesaFormats(psPVRScreen);
   free(psPVRScreen);
}

unsigned int
DRIMODCreateContext(PVRDRIAPIType eAPI, PVRDRIConfig *psPVRDRIConfig,
                    struct PVRDRIContextConfig *psCtxConfig,
                    struct __DRIcontextRec *psDRIContext,
                    struct DRISUPContext *psPVRSharedContext,
                    struct DRISUPScreen *psPVRScreen,
                    struct DRISUPContext **ppsPVRContext)
{
   struct DRISUPContext *psPVRContext;
   PVRDRIContextImpl *psSharedImpl;
   bool bNotifyReset;
   unsigned int uError;

   psSharedImpl = psPVRSharedContext ? psPVRSharedContext->psImpl : NULL,
      psPVRContext = calloc(1, sizeof(*psPVRContext));
   if (psPVRContext == NULL) {
      __driUtilMessage("%s: Couldn't allocate PVRDRIContext", __func__);
      return PVRDRI_CONTEXT_ERROR_NO_MEMORY;
   }

   psPVRContext->psDRIContext = psDRIContext;
   psPVRContext->psPVRScreen = psPVRScreen;

   switch (eAPI) {
   case PVRDRI_API_GLES1:
   case PVRDRI_API_GLES2:
      break;
   default:
      __driUtilMessage("%s: Unsupported API: %d", __func__, (int) eAPI);
      uError = PVRDRI_CONTEXT_ERROR_BAD_API;
      goto ErrorContextFree;
   }
   psPVRContext->eAPI = eAPI;

   switch (psCtxConfig->iResetStrategy) {
   case PVRDRI_CONTEXT_RESET_NO_NOTIFICATION:
      bNotifyReset = false;
      break;
   case PVRDRI_CONTEXT_RESET_LOSE_CONTEXT:
      bNotifyReset = true;
      break;
   default:
      __driUtilMessage("%s: Unsupported reset strategy: %d",
                       __func__, psCtxConfig->iResetStrategy);
      uError = PVRDRI_CONTEXT_ERROR_UNKNOWN_ATTRIBUTE;
      goto ErrorContextFree;
   }

   switch (psCtxConfig->iReleaseBehavior) {
   case PVRDRI_CONTEXT_RELEASE_BEHAVIOR_NONE:
      psPVRContext->bMakeUncurrentNoFlush = true;
      break;
   case PVRDRI_CONTEXT_RELEASE_BEHAVIOR_FLUSH:
      psPVRContext->bMakeUncurrentNoFlush = false;
      break;
   default:
      __driUtilMessage("%s: Unsupported release behaviour: %d",
                       __func__, psCtxConfig->iReleaseBehavior);
      uError = PVRDRI_CONTEXT_ERROR_UNKNOWN_ATTRIBUTE;
      goto ErrorContextFree;
   }

   uError = PVRDRICreateContextV1(psPVRScreen->psImpl, psSharedImpl,
                                  psPVRDRIConfig, psPVRContext->eAPI,
                                  PVRDRI_API_SUB_NONE,
                                  psCtxConfig->uMajorVersion,
                                  psCtxConfig->uMinorVersion,
                                  psCtxConfig->uFlags, bNotifyReset,
                                  psCtxConfig->uPriority,
                                  &psPVRContext->psImpl);
   if (uError != PVRDRI_CONTEXT_ERROR_SUCCESS)
      goto ErrorContextFree;

   *ppsPVRContext = psPVRContext;

   return uError;

ErrorContextFree:
   free(psPVRContext);

   return uError;
}

void
DRIMODDestroyContext(struct DRISUPContext *psPVRContext)
{
   struct DRISUPScreen *psPVRScreen = psPVRContext->psPVRScreen;

   PVRContextUnbind(psPVRContext, false, false);
   PVRDRIDestroyContextImpl(psPVRContext->psImpl, psPVRContext->eAPI,
                            psPVRScreen->psImpl);
   free(psPVRContext);
}

static IMG_PIXFMT
PVRDRIGetPixelFormat(PVRDRIConfig *psPVRDRIConfig)
{
   unsigned int uRGBBits;
   unsigned int uRedMask, uGreenMask, uBlueMask, uAlphaMask;
   unsigned int uSRGBCapable;
   bool bRes;

   bRes = MODSUPConfigQuery(psPVRDRIConfig,
                            PVRDRI_CONFIG_ATTRIB_RGB_BITS, &uRGBBits);

   bRes &= MODSUPConfigQuery(psPVRDRIConfig,
                             PVRDRI_CONFIG_ATTRIB_RED_MASK, &uRedMask);

   bRes &= MODSUPConfigQuery(psPVRDRIConfig,
                             PVRDRI_CONFIG_ATTRIB_GREEN_MASK,
                             &uGreenMask);

   bRes &= MODSUPConfigQuery(psPVRDRIConfig,
                             PVRDRI_CONFIG_ATTRIB_BLUE_MASK, &uBlueMask);

   bRes &= MODSUPConfigQuery(psPVRDRIConfig,
                             PVRDRI_CONFIG_ATTRIB_ALPHA_MASK,
                             &uAlphaMask);

   bRes &= MODSUPConfigQuery(psPVRDRIConfig,
                             PVRDRI_CONFIG_ATTRIB_SRGB_CAPABLE,
                             &uSRGBCapable);

   if (!bRes) {
      __driUtilMessage("%s: Config query failed", __func__);
      return IMG_PIXFMT_UNKNOWN;
   }

   switch (uRGBBits) {
   case 32:
   case 24:
      if (uRedMask == 0x00FF0000 &&
          uGreenMask == 0x0000FF00 && uBlueMask == 0x000000FF) {
         if (uAlphaMask == 0xFF000000) {
            if (uSRGBCapable)
               return IMG_PIXFMT_B8G8R8A8_UNORM_SRGB;
            else
               return IMG_PIXFMT_B8G8R8A8_UNORM;
         } else if (uAlphaMask == 0) {
            return IMG_PIXFMT_B8G8R8X8_UNORM;
         }
      }

      if (uRedMask == 0x000000FF &&
          uGreenMask == 0x0000FF00 && uBlueMask == 0x00FF0000) {
         if (uAlphaMask == 0xFF000000) {
            if (uSRGBCapable)
               return IMG_PIXFMT_R8G8B8A8_UNORM_SRGB;
            else
               return IMG_PIXFMT_R8G8B8A8_UNORM;
         } else if (uAlphaMask == 0) {
            return IMG_PIXFMT_R8G8B8X8_UNORM;
         }
      }

      __driUtilMessage("%s: Unsupported format", __func__);

      return IMG_PIXFMT_UNKNOWN;
   case 16:
      if (uRedMask == 0xF800 &&
          uGreenMask == 0x07E0 && uBlueMask == 0x001F) {
         return IMG_PIXFMT_B5G6R5_UNORM;
      }

      __driUtilMessage("%s: Unsupported format", __func__);

      return IMG_PIXFMT_UNKNOWN;
   default:
      __driUtilMessage("%s: Unsupported format", __func__);

      return IMG_PIXFMT_UNKNOWN;
   }
}

struct DRISUPDrawable *
DRIMODCreateDrawable(struct __DRIdrawableRec *psDRIDrawable,
                     struct DRISUPScreen *psPVRScreen, void *pvLoaderPrivate,
                     PVRDRIConfig *psPVRDRIConfig)
{
   struct DRISUPDrawable *psPVRDrawable;
   IMG_PIXFMT ePixelFormat;
   unsigned int uDoubleBufferMode;

   psPVRDrawable = calloc(1, sizeof(*psPVRDrawable));
   if (!psPVRDrawable) {
      __driUtilMessage("%s: Couldn't allocate PVR drawable", __func__);
      goto ErrorDrawableFree;
   }

   psPVRDrawable->psDRIDrawable = psDRIDrawable;
   psPVRDrawable->psPVRScreen = psPVRScreen;
   psPVRDrawable->pvLoaderPrivate = pvLoaderPrivate;
   psPVRDrawable->psConfig = psPVRDRIConfig;

   ePixelFormat = PVRDRIGetPixelFormat(psPVRDRIConfig);
   if (ePixelFormat == IMG_PIXFMT_UNKNOWN) {
      __driUtilMessage("%s: Couldn't determine IMG pixel format", __func__);
      goto ErrorDrawableFree;
   }

   psPVRDrawable->psFormat =
      PVRDRIIMGPixelFormatToImageFormat(psPVRScreen, ePixelFormat);
   if (!psPVRDrawable->psFormat) {
      __driUtilMessage("%s: Unsupported IMG pixel format (format = %u)",
                       __func__, ePixelFormat);
      return false;
   }

   if (!MODSUPConfigQuery(psPVRDRIConfig,
                          PVRDRI_CONFIG_ATTRIB_DOUBLE_BUFFER_MODE,
                          &uDoubleBufferMode)) {
      __driUtilMessage("%s: Couldn't query double buffer mode", __func__);
      goto ErrorDrawableFree;
   }
   psPVRDrawable->bDoubleBufferMode = (uDoubleBufferMode != 0);

   /*
    * We aren't told the type of the drawable so treat double buffered
    * drawables as windows and single buffered drawables as pixmaps (although
    * these could actually be pbuffers).
    */
   if (psPVRDrawable->bDoubleBufferMode)
      psPVRDrawable->eType = PVRDRI_DRAWABLE_WINDOW;
   else
      psPVRDrawable->eType = PVRDRI_DRAWABLE_PIXMAP;

   psPVRDrawable->psImpl =
      PVRDRICreateDrawableWithConfig((PVRDRIDrawable *) psPVRDrawable,
                                     psPVRDRIConfig);
   if (!psPVRDrawable->psImpl) {
      __driUtilMessage("%s: Couldn't create PVR drawable", __func__);
      goto ErrorDrawableFree;
   }

   return psPVRDrawable;

ErrorDrawableFree:
   PVRDRIDestroyDrawableImpl(psPVRDrawable->psImpl);
   free(psPVRDrawable);

   return NULL;
}

void
DRIMODDestroyDrawable(struct DRISUPDrawable *psPVRDrawable)
{
   PVRDrawableUnbindContext(psPVRDrawable);
   PVRDRIDrawableDeinit(psPVRDrawable);
   PVREGLDrawableDestroyConfig(psPVRDrawable->psImpl);
   PVRDRIDestroyDrawableImpl(psPVRDrawable->psImpl);
   free(psPVRDrawable);
}

bool
DRIMODMakeCurrent(struct DRISUPContext *psPVRContext,
                  struct DRISUPDrawable *psPVRWrite,
                  struct DRISUPDrawable *psPVRRead)
{
   struct DRISUPDrawable *psPVRDrawableOld = psPVRContext->psPVRDrawable;

   if (psPVRWrite != NULL) {
      if (!PVRDRIDrawableInit(psPVRWrite)) {
         __driUtilMessage("%s: Couldn't initialise write drawable", __func__);
         return false;
      }
   }

   if (psPVRRead != NULL) {
      if (!PVRDRIDrawableInit(psPVRRead)) {
         __driUtilMessage("%s: Couldn't initialise read drawable", __func__);
         return false;
      }
   }

   if (!PVRDRIMakeCurrentGC(psPVRContext->eAPI,
                            psPVRContext->psPVRScreen->psImpl,
                            psPVRContext->psImpl,
                            psPVRWrite == NULL ? NULL : psPVRWrite->psImpl,
                            psPVRRead == NULL ? NULL : psPVRRead->psImpl))
      return false;

   if (psPVRDrawableOld != NULL)
      psPVRDrawableOld->psPVRContext = NULL;

   if (psPVRWrite != NULL)
      psPVRWrite->psPVRContext = psPVRContext;

   psPVRContext->psPVRDrawable = psPVRWrite;

   DRIMODThreadSetCurrentScreen(psPVRContext->psPVRScreen);

   return true;
}

bool
DRIMODUnbindContext(struct DRISUPContext *psPVRContext)
{
   PVRContextUnbind(psPVRContext, true, false);
   DRIMODThreadSetCurrentScreen(NULL);

   return true;
}

struct DRISUPBuffer *
DRIMODAllocateBuffer(struct DRISUPScreen *psPVRScreen,
                     unsigned int uAttachment, unsigned int uFormat,
                     int iWidth, int iHeight, unsigned int *puName,
                     unsigned int *puPitch, unsigned int *puCPP,
                     unsigned int *puFlags)
{
   PVRDRIBufferImpl *psPVRBufferImpl;
   unsigned int uBPP;
   unsigned int uPitch;

   (void) uAttachment;

   /* GEM names are only supported on primary nodes */
   if (drmGetNodeTypeFromFd(psPVRScreen->iFD) != DRM_NODE_PRIMARY) {
      __driUtilMessage("%s: Cannot allocate buffer", __func__);
      return NULL;
   }

   /* This is based upon PVRDRIGetPixelFormat */
   switch (uFormat) {
   case 32:
   case 16:
      /* Format (depth) and bpp match */
      uBPP = uFormat;
      break;
   case 24:
      uBPP = 32;
      break;
   default:
      __driUtilMessage("%s: Unsupported format '%u'", __func__, uFormat);
      return NULL;
   }

   psPVRBufferImpl = PVRDRIBufferCreate(psPVRScreen->psImpl, iWidth, iHeight,
                                        uBPP, PVDRI_BUFFER_USE_SHARE, &uPitch);
   if (!psPVRBufferImpl) {
      __driUtilMessage("%s: Failed to create backing buffer", __func__);
      return NULL;
   }

   *puName = PVRDRIBufferGetName(psPVRBufferImpl);
   *puPitch = uPitch;
   *puCPP = uBPP / 8;
   *puFlags = 0;

   return (struct DRISUPBuffer *) psPVRBufferImpl;
}

void
DRIMODReleaseBuffer(struct DRISUPScreen *psPVRScreen,
                    struct DRISUPBuffer *psPVRBuffer)
{
   PVRDRIBufferImpl *psPVRBufferImpl = (PVRDRIBufferImpl *) psPVRBuffer;

   (void) psPVRScreen;

   PVRDRIBufferDestroy(psPVRBufferImpl);
}

/*************************************************************************//*!
 Global functions
 *//**************************************************************************/

void
DRIMODThreadSetCurrentScreen(struct DRISUPScreen *psPVRScreen)
{
   gpsPVRScreen = psPVRScreen;
}

struct DRISUPScreen *
DRIMODThreadGetCurrentScreen(void)
{
   return gpsPVRScreen;
}
