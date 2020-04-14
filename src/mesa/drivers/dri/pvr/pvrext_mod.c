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

#include <dlfcn.h>
#include <assert.h>

#include "util/u_atomic.h"
#include "pvrdri_mod.h"

static void
PVRDRIFlushDrawableContext(struct DRISUPDrawable *psPVRDrawable,
                           struct DRISUPContext *psPVRContext)
{
   struct DRISUPContext *psPVRDrawContext = psPVRDrawable->psPVRContext;

   if (psPVRDrawContext) {
      PVRDRIEGLFlushBuffers(psPVRDrawContext->eAPI,
                            psPVRDrawContext->psPVRScreen->psImpl,
                            psPVRDrawContext->psImpl,
                            psPVRDrawable->psImpl,
                            false,
                            false, (psPVRDrawContext != psPVRContext));
   }
}

void
DRIMODSetTexBuffer2(struct DRISUPContext *psPVRContext, int iTarget,
                    int iFormat, struct DRISUPDrawable *psPVRDrawable)
{
   (void) iTarget;
   (void) iFormat;

   if (!psPVRDrawable->bInitialised) {
      if (!PVRDRIDrawableInit(psPVRDrawable)) {
         __driUtilMessage("%s: Couldn't initialise pixmap", __func__);
         return;
      }
   }

   PVRDRIFlushDrawableContext(psPVRDrawable, psPVRContext);
   PVRDRI2BindTexImage(psPVRContext->eAPI, psPVRContext->psPVRScreen->psImpl,
                       psPVRContext->psImpl, psPVRDrawable->psImpl);
}

void
DRIMODReleaseTexBuffer(struct DRISUPContext *psPVRContext, int iTarget,
                       struct DRISUPDrawable *psPVRDrawable)
{
   (void) iTarget;

   PVRDRI2ReleaseTexImage(psPVRContext->eAPI,
                          psPVRContext->psPVRScreen->psImpl,
                          psPVRContext->psImpl, psPVRDrawable->psImpl);
}

void
DRIMODFlush(struct DRISUPDrawable *psPVRDrawable)
{
   struct DRISUPContext *psPVRContext = psPVRDrawable->psPVRContext;

   PVRDRIFlushBuffersForSwap(psPVRContext, psPVRDrawable);
}

void
DRIMODInvalidate(struct DRISUPDrawable *psPVRDrawable)
{
   if (psPVRDrawable->psPVRScreen->bUseInvalidate)
      p_atomic_inc(&psPVRDrawable->iInfoInvalid);
}

void
DRIMODFlushWithFlags(struct DRISUPContext *psPVRContext,
                     struct DRISUPDrawable *psPVRDrawable,
                     unsigned int uFlags, unsigned int uThrottleReason)
{
   (void) uThrottleReason;

   if ((uFlags & PVRDRI_FLUSH_DRAWABLE) != 0) {
      PVRDRIFlushBuffersForSwap(psPVRContext, psPVRDrawable);
   } else if ((uFlags & PVRDRI_FLUSH_CONTEXT) != 0) {
      /*
       * PVRDRI_FLUSH__CONTEXT means "glFlush". Most callers also specify
       * PVRDRI_FLUSH_DRAWABLE. An exception is GBM, which flushes after an
       * unmap, when there doesn't appear to be a need to flush outstanding
       * GPU operations.
       */
   }
}

int
DRIMODQueryRendererInteger(struct DRISUPScreen *psPVRScreen, int iAttribute,
                           unsigned int *puValue)
{
   (void) psPVRScreen;

   switch (iAttribute) {
   case PVRDRI_RENDERER_HAS_CONTEXT_PRIORITY:
      puValue[0] = 0;
      puValue[0] |= PVRDRI_RENDERER_HAS_CONTEXT_PRIORITY_HIGH;
      puValue[0] |= PVRDRI_RENDERER_HAS_CONTEXT_PRIORITY_MEDIUM;
      puValue[0] |= PVRDRI_RENDERER_HAS_CONTEXT_PRIORITY_LOW;
      return 0;
   case PVRDRI_RENDERER_HAS_FRAMEBUFFER_SRGB:
      puValue[0] = 1;
      return 0;
   default:
      return -1;
   }
}

int
DRIMODQueryRendererString(struct DRISUPScreen *psPVRScreen, int iAttribute,
                          const char **ppszValue)
{
   (void) psPVRScreen;
   (void) iAttribute;
   (void) ppszValue;

   return -1;
}

void *
DRIMODCreateFence(struct DRISUPContext *psPVRContext)
{
   return PVRDRICreateFenceImpl(psPVRContext->eAPI,
                                psPVRContext->psPVRScreen->psImpl,
                                psPVRContext->psImpl);
}

void
DRIMODDestroyFence(struct DRISUPScreen *psPVRScreen, void *pvFence)
{
   (void) psPVRScreen;

   PVRDRIDestroyFenceImpl(pvFence);
}

bool
DRIMODClientWaitSync(struct DRISUPContext *psPVRContext, void *pvFence,
                     unsigned int uFlags, uint64_t uTimeout)
{
   bool bFlushCommands = (uFlags & PVRDRI_FENCE_FLAG_FLUSH_COMMANDS);
   bool bTimeout = (uTimeout != PVRDRI_FENCE_TIMEOUT_INFINITE);

   if (psPVRContext && bFlushCommands) {
      struct DRISUPDrawable *psPVRDrawable;
      PVRDRIDrawableImpl *psDrawableImpl;

      psPVRDrawable = psPVRContext->psPVRDrawable;
      psDrawableImpl = psPVRDrawable ? psPVRDrawable->psImpl : NULL;

      (void) PVRDRIEGLFlushBuffers(psPVRContext->eAPI,
                                   psPVRContext->psPVRScreen->psImpl,
                                   psPVRContext->psImpl,
                                   psDrawableImpl, true, false, false);
   }

   return PVRDRIClientWaitSyncImpl(PVRDRI_API_NONE, NULL, pvFence, false,
                                   bTimeout, uTimeout);
}

void
DRIMODServerWaitSync(struct DRISUPContext *psPVRContext, void *pvFence,
                     unsigned int uFlags)
{
   assert(uFlags == 0);
   (void) uFlags;

   if (pvFence) {
      if (!PVRDRIServerWaitSyncImpl(psPVRContext->eAPI,
                                    psPVRContext->psImpl, pvFence)) {
         __driUtilMessage("%s: Server wait sync failed", __func__);
      }
   }
}

unsigned int
DRIMODGetFenceCapabilities(struct DRISUPScreen *psPVRScreen)
{
   return PVRDRIGetFenceCapabilities(psPVRScreen->psImpl);
}

void *
DRIMODCreateFenceFD(struct DRISUPContext *psPVRContext, int iFD)
{
   return PVRDRICreateFenceFd(psPVRContext->eAPI,
                              psPVRContext->psPVRScreen->psImpl,
                              psPVRContext->psImpl, iFD);
}

int
DRIMODGetFenceFD(struct DRISUPScreen *psPVRScreen, void *pvFence)
{
   (void) psPVRScreen;

   return PVRDRIGetFenceFd(pvFence);
}

unsigned int
DRIMODGetNumAPIProcs(struct DRISUPScreen *psPVRScreen, PVRDRIAPIType eAPI)
{
   (void) psPVRScreen;

   return PVRDRIGetNumAPIFuncs(eAPI);
}

const char *
DRIMODGetAPIProcName(struct DRISUPScreen *psPVRScreen, PVRDRIAPIType eAPI,
                     unsigned int uIndex)
{
   (void) psPVRScreen;

   return PVRDRIGetAPIFunc(eAPI, uIndex);
}

void *
DRIMODGetAPIProcAddress(struct DRISUPScreen *psPVRScreen, PVRDRIAPIType eAPI,
                        unsigned int uIndex)
{
   const char *pszFunc;
   void *pvHandle;
   void *pvFunc;
   const char *pszError;

   pszFunc = PVRDRIGetAPIFunc(eAPI, uIndex);
   if (!pszFunc) {
      __driUtilMessage("%s: No Proc for API %u at index %u",
                       __func__, (unsigned int) eAPI, uIndex);
      return NULL;
   }

   pvHandle = PVRDRIEGLGetLibHandle(eAPI, psPVRScreen->psImpl);
   if (!pvHandle) {
      __driUtilMessage("%s: No library handle for API %u",
                       __func__, (unsigned int) eAPI);
      return NULL;
   }

   (void) dlerror();
   pvFunc = dlsym(pvHandle, pszFunc);
   pszError = dlerror();
   if (pszError)
      pvFunc = PVRDRIEGLGetProcAddress(eAPI, psPVRScreen->psImpl, pszFunc);

   return pvFunc;
}

void
DRIMODSetDamageRegion(struct DRISUPDrawable *psDRISUPDrawable,
                      unsigned int uNRects, int *piRects)
{
   (void) psDRISUPDrawable;
   (void) uNRects;
   (void) piRects;
}
