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

#if !defined(__PVRDRI_MOD_H__)
#define __PVRDRI_MOD_H__

#include <stdint.h>
#include <stdbool.h>

#include "pvrdri.h"

/* This should match EGL_MAX_PLANES */
#define DRI_PLANES_MAX 3

#define DRI2_BUFFERS_MAX 3

struct PVRDRIModifiers {
   /* Number of modifiers for a given format */
   int iNumModifiers;

   /* Array of modifiers */
   uint64_t *puModifiers;

   /*
    * Array of booleans that indicates which modifiers in the above array can
    * only be used for EGL Image External (and so not for scanout).
    */
   unsigned int *puExternalOnly;
};

/** Our PVR related screen data */
struct DRISUPScreen {
   /* DRI screen structure pointer */
   struct __DRIscreenRec *psDRIScreen;

   /* Use invalidate events */
   bool bUseInvalidate;

   int iFD;

   void *pvLoaderPrivate;

   PVRDRIScreenImpl *psImpl;

   /*
    * Number of supported formats:
    * -1 -> couldn't be queried (pvr_dri_support too old)
    *  0 -> uninitialised or initialisation failed
    */
   int iNumFormats;

   /* Indicates which entries in the image format array are supported */
   bool *pbHasFormat;

   /* Array of modifiers for each image format array entry */
   struct PVRDRIModifiers *psModifiers;

   /* Supported Mesa formats */
   unsigned int *puMesaFormats;
   unsigned int uNumMesaFormats;
};

/** Our PVR related context data */
struct DRISUPContext {
   /* Pointer to DRI context */
   struct __DRIcontextRec *psDRIContext;

   /* Pointer to PVRDRIScreen structure */
   struct DRISUPScreen *psPVRScreen;

   /* Pointer to currently bound drawable */
   struct DRISUPDrawable *psPVRDrawable;

   /* API */
   PVRDRIAPIType eAPI;

   PVRDRIContextImpl *psImpl;

   /* Don't flush when the context is made uncurrent */
   bool bMakeUncurrentNoFlush;
};

typedef struct PVRDRIImageFormat_TAG {
   /*
    * IMG pixel format for the entire/overall image, e.g.
    * IMG_PIXFMT_B8G8R8A8_UNORM or IMG_PIXFMT_YUV420_2PLANE.
    */
   IMG_PIXFMT eIMGPixelFormat;

   /*
    * DRI fourcc for the entire/overall image (defined by dri_interface.h),
    * e.g. __DRI_IMAGE_FOURCC_ARGB8888 or __DRI_IMAGE_FOURCC_NV12.
    */
   int iDRIFourCC;

   /*
    * DRI components for the entire/overall image (defined by
    * dri_interface.h), e.g. __DRI_IMAGE_COMPONENTS_RGBA or
    * __DRI_IMAGE_COMPONENTS_Y_UV.
    *
    * This specifies the image components and their groupings, in terms of
    * sub-images/planes, but not the order in which they appear.
    *
    * For example:
    * - any combination of BGRA channels would correspond to
    *   __DRI_IMAGE_COMPONENTS_RGBA
    * - any combination of BGR or BGRX would correspond to
    *   __DRI_IMAGE_COMPONENTS_RGB
    * - any combination of YUV with 2 planes would correspond to
    *   __DRI_IMAGE_COMPONENTS_Y_UV
    */
   int iDRIComponents;

   /*
    * True if the format represents an sRGB colour space and false if it
    * represents a linear one.
    */
   bool bIsSRGB;

   /* The number of sub-images/planes that make up the overall image */
   unsigned int uiNumPlanes;

   /* Per-plane information */
   struct {
      /* IMG pixel format for the plane */
      IMG_PIXFMT eIMGPixelFormat;

      /*
       * This is the amount that the image width should be bit-shifted in
       * order to give the plane width. This value can be determined from the
       * YUV sub-sampling ratios and should either be 0 (full * width), 1
       * (half width) or 2 (quarter width).
       */
      unsigned int uiWidthShift;

      /*
       * This is the amount that the image height should be bit-shifted in
       * order to give the plane height. This value can be determined from the
       * YUV sub-sampling ratios and should either be 0 (full * height) or 1
       * (half height).
       */
      unsigned int uiHeightShift;
   } sPlanes[DRI_PLANES_MAX];
} PVRDRIImageFormat;

/** Our PVR related drawable data */
struct DRISUPDrawable {
   /** Ptr to PVR screen, that spawned this drawable */
   struct DRISUPScreen *psPVRScreen;

   /** DRI drawable data */
   struct __DRIdrawableRec *psDRIDrawable;

   void *pvLoaderPrivate;

   bool bDoubleBufferMode;
   PVRDRIDrawableType eType;

   PVRDRIConfig *psConfig;

   /** Are surface/buffers created? */
   bool bInitialised;

   /* Width and height */
   uint32_t uWidth;
   uint32_t uHeight;

   /** Buffer stride */
   unsigned int uStride;

   /* Number of bytes per pixel */
   unsigned int uBytesPerPixel;

   /* Context bound to this drawable */
   struct DRISUPContext *psPVRContext;

   /* Format of this drawable */
   const PVRDRIImageFormat *psFormat;

   /* Indicates the drawable info is invalid */
   int iInfoInvalid;

   /* Indicates the drawable is currently being updated */
   bool bDrawableUpdating;

   /* Indicates a flush is in progress */
   bool bFlushInProgress;

   __DRIimage *psDRI;
   __DRIimage *psImage;

   __DRIimage *psDRIAccum;
   __DRIimage *psImageAccum;

   PVRDRIDrawableImpl *psImpl;
};

/*************************************************************************//*!
 pvrdri_mod.c
 *//**************************************************************************/

struct DRISUPScreen *DRIMODThreadGetCurrentScreen(void);
void DRIMODThreadSetCurrentScreen(struct DRISUPScreen *psPVRScreen);

void PVRDRIFlushBuffersForSwap(struct DRISUPContext *psPVRContext,
                               struct DRISUPDrawable *psPVRDrawable);

/*************************************************************************//*!
 pvrutil_mod.c
 *//**************************************************************************/

const struct __DRIconfigRec **PVRDRICreateConfigs(const struct DRISUPScreen
                                                  *psPVRScreen);
const PVRDRIImageFormat *PVRDRIFourCCToImageFormat(struct DRISUPScreen
                                                   *psPVRScreen,
                                                   int iDRIFourCC);
const PVRDRIImageFormat *PVRDRIIMGPixelFormatToImageFormat(struct DRISUPScreen *psPVRScreen,
                                                           IMG_PIXFMT eIMGPixelFormat);
IMG_YUV_COLORSPACE PVRDRIToIMGColourSpace(const PVRDRIImageFormat *psFormat,
                                          unsigned int uDRIColourSpace,
                                          unsigned int uDRISampleRange);
IMG_YUV_CHROMA_INTERP PVRDRIChromaSittingToIMGInterp(const PVRDRIImageFormat *psFormat,
                                                     unsigned int uChromaSitting);
bool PVRDRIGetSupportedFormats(struct DRISUPScreen *psPVRScreen);
void PVRDRIDestroyFormatInfo(struct DRISUPScreen *psPVRScreen);
bool PVRDRIValidateImageModifier(struct DRISUPScreen *psPVRScreen,
                                 const int iFourCC, const uint64_t uiModifier);
bool PVRDRIGetMesaFormats(struct DRISUPScreen *psPVRScreen);
void PVRDRIFreeMesaFormats(struct DRISUPScreen *psPVRScreen);

/*************************************************************************//*!
 pvrdrawable_mod.c
 *//**************************************************************************/

bool PVRDRIDrawableInit(struct DRISUPDrawable *psPVRDrawable);
void PVRDRIDrawableDeinit(struct DRISUPDrawable *psPVRDrawable);
bool PVRDRIDrawableQuery(const PVRDRIDrawable *psPVRDRIDrawable,
                         PVRDRIBufferAttrib eBufferAttrib,
                         uint32_t *uiValueOut);
bool PVRDRIDrawableGetParametersV2(PVRDRIDrawable *psPVRDRIDrawable,
                                   uint32_t uiFlags,
                                   PVRDRIBufferImpl **ppsDstBuffer,
                                   PVRDRIBufferImpl **ppsAccumBuffer);

/*************************************************************************//*!
pvrimage_mod.c
 *//**************************************************************************/

__DRIimage *PVRDRIScreenGetDRIImage(void *hEGLImage);
void PVRDRIRefImage(__DRIimage *psImage);
void PVRDRIUnrefImage(__DRIimage *psImage);
PVRDRIImageType PVRDRIImageGetSharedType(__DRIimage *psImage);
PVRDRIBufferImpl *PVRDRIImageGetSharedBuffer(__DRIimage *psImage);
IMGEGLImage *PVRDRIImageGetSharedEGLImage(__DRIimage *psImage);
IMGEGLImage *PVRDRIImageGetEGLImage(__DRIimage *psImage);

/*************************************************************************//*!
 Functions to implement PVRDRISupportInterfaceV2, for backwards compatibility
 With DRI Support libraries that only provide PVRDRISupportInterface.
 *//**************************************************************************/

struct DRISUPScreen *DRIMODCreateScreen(struct __DRIscreenRec *psDRIScreen,
                                        int iFD, bool bUseInvalidate,
                                        void *pvLoaderPrivate,
                                        const struct __DRIconfigRec ***pppsConfigs,
                                        int *piMaxGLES1Version,
                                        int *piMaxGLES2Version);
void DRIMODDestroyScreen(struct DRISUPScreen *psDRISUPScreen);

unsigned int DRIMODCreateContext(PVRDRIAPIType eAPI,
                                 PVRDRIConfig *psPVRDRIConfig,
                                 struct PVRDRIContextConfig *psCtxConfig,
                                 struct __DRIcontextRec *psDRIContext,
                                 struct DRISUPContext *psDRISUPSharedContext,
                                 struct DRISUPScreen *psDRISUPScreen,
                                 struct DRISUPContext **ppsDRISUPContext);
void DRIMODDestroyContext(struct DRISUPContext *psDRISUPContext);

struct DRISUPDrawable *DRIMODCreateDrawable(struct __DRIdrawableRec *psDRIDrawable,
                                            struct DRISUPScreen *psDRISUPScreen,
                                            void *pvLoaderPrivate,
                                            PVRDRIConfig *psPVRDRIConfig);
void DRIMODDestroyDrawable(struct DRISUPDrawable *psDRISUPDrawable);

bool DRIMODMakeCurrent(struct DRISUPContext *psDRISUPContext,
                       struct DRISUPDrawable *psDRISUPWrite,
                       struct DRISUPDrawable *psDRISUPRead);
bool DRIMODUnbindContext(struct DRISUPContext *psDRISUPContext);

struct DRISUPBuffer *DRIMODAllocateBuffer(struct DRISUPScreen *psDRISUPScreen,
                                          unsigned int uAttchment,
                                          unsigned int uFormat,
                                          int iWidth, int iHeight,
                                          unsigned int *puName,
                                          unsigned int *puPitch,
                                          unsigned int *puCPP,
                                          unsigned int *puFlags);
void DRIMODReleaseBuffer(struct DRISUPScreen *psDRISUPScreen,
                         struct DRISUPBuffer *psDRISUPBuffer);
void DRIMODSetTexBuffer2(struct DRISUPContext *psDRISUPContext, int iTarget,
                         int iFormat, struct DRISUPDrawable *psDRISUPDrawable);
void DRIMODReleaseTexBuffer(struct DRISUPContext *psDRISUPContext,
                            int iTarget,
                            struct DRISUPDrawable *psDRISUPDrawable);

void DRIMODFlush(struct DRISUPDrawable *psDRISUPDrawable);
void DRIMODInvalidate(struct DRISUPDrawable *psDRISUPDrawable);
void DRIMODFlushWithFlags(struct DRISUPContext *psDRISUPContext,
                          struct DRISUPDrawable *psDRISUPDrawable,
                          unsigned int uFlags, unsigned int uThrottleReason);

__DRIimage *DRIMODCreateImageFromName(struct DRISUPScreen *psDRISUPScreen,
                                      int iWidth, int iHeight, int iFourCC,
                                      int iName, int iPitch,
                                      void *pvLoaderPrivate);
__DRIimage *DRIMODCreateImageFromRenderbuffer(struct DRISUPContext *psDRISUPContext,
                                              int iRenderBuffer,
                                              void *pvLoaderPrivate);
void DRIMODDestroyImage(__DRIimage *psImage);
__DRIimage *DRIMODCreateImage(struct DRISUPScreen *psDRISUPScreen,
                              int iWidth, int iHeight, int iFourCC,
                              unsigned int uUse, void *pvLoaderPrivate);
bool DRIMODQueryImage(__DRIimage *psImage, int iAttrib, int *iValue);
__DRIimage *DRIMODDupImage(__DRIimage *psImage, void *pvLoaderPrivate);
bool DRIMODValidateImageUsage(__DRIimage *psImage, unsigned int uUse);
__DRIimage *DRIMODCreateImageFromNames(struct DRISUPScreen *psDRISUPScreen,
                                       int iWidth, int iHeight, int iFourCC,
                                       int *piNames, int iNumNames,
                                       int *piStrides, int *piOffsets,
                                       void *pvLoaderPrivate);
__DRIimage *DRIMODFromPlanar(__DRIimage *psImage, int iPlane,
                             void *pvLoaderPrivate);
__DRIimage *DRIMODCreateImageFromTexture(struct DRISUPContext *psDRISUPContext,
                                         int iTarget, unsigned int uTexture,
                                         int iDepth, int iLevel,
                                         unsigned int *puError,
                                         void *pvLoaderPrivate);
__DRIimage *DRIMODCreateImageFromFDs(struct DRISUPScreen *psDRISUPcreen,
                                     int iWidth, int iHeight, int iFourCC,
                                     int *piFDs, int iNumFDs,
                                     int *piStrides, int *piOffsets,
                                     void *pvLoaderPrivate);
__DRIimage *DRIMODCreateImageFromDMABufs(struct DRISUPScreen *psDRISUPScreen,
                                         int iWidth, int iHeight, int iFourCC,
                                         int *piFDs, int iNumFDs,
                                         int *piStrides, int *piOffsets,
                                         unsigned int uColorSpace,
                                         unsigned int uSampleRange,
                                         unsigned int uHorizSiting,
                                         unsigned int uVertSiting,
                                         unsigned int *puError,
                                         void *pvLoaderPrivate);
int DRIMODGetImageCapabilities(struct DRISUPScreen *psDRISUPScreen);
void DRIMODBlitImage(struct DRISUPContext *psDRISUPContext,
                     __DRIimage *psDst, __DRIimage *psSrc,
                     int iDstX0, int iDstY0, int iDstWidth, int iDstHeight,
                     int iSrcX0, int iSrcY0, int iSrcWidth, int iSrcHeight,
                     int iFlushFlag);
void *DRIMODMapImage(struct DRISUPContext *psDRISUPContext,
                     __DRIimage *psImage,
                     int iX0, int iY0, int iWidth, int iHeight,
                     unsigned int iFlags, int *iStride, void **ppvData);
void DRIMODUnmapImage(struct DRISUPContext *psDRISUPContext,
                      __DRIimage *psImage, void *pvData);
__DRIimage *DRIMODCreateImageWithModifiers(struct DRISUPScreen *psDRISUPScreen,
                                           int iWidth, int iHeight,
                                           int iFourCC,
                                           const uint64_t *puModifiers,
                                           const unsigned int uModifierCount,
                                           void *pvLoaderPrivate);
__DRIimage *DRIMODCreateImageFromDMABufs2(struct DRISUPScreen *psDRISUPScreen,
                                          int iWidth, int iHeight,
                                          int iFourCC, uint64_t uModifier,
                                          int *piFDs, int iNumFDs,
                                          int *piStrides, int *piOffsets,
                                          unsigned int uColorSpace,
                                          unsigned int uSampleRange,
                                          unsigned int uHorizSiting,
                                          unsigned int uVertSiting,
                                          unsigned int *puError,
                                          void *pvLoaderPrivate);

bool DRIMODQueryDMABufFormats(struct DRISUPScreen *psDRISUPScreen, int iMax,
                              int *piFormats, int *piCount);
bool DRIMODQueryDMABufModifiers(struct DRISUPScreen *psDRISUPScreen,
                                int iFourCC, int iMax, uint64_t *puModifiers,
                                unsigned int *piExternalOnly, int *piCount);
bool DRIMODQueryDMABufFormatModifierAttribs(struct DRISUPScreen *psDRISUPScreen,
                                            uint32_t uFourCC,
                                            uint64_t uModifier,
                                            int iAttribute,
                                            uint64_t *puValue);

__DRIimage *DRIMODCreateImageFromRenderBuffer2(struct DRISUPContext *psDRISUPContext,
                                               int iRenderBuffer,
                                               void *pvLoaderPrivate,
                                               unsigned int *puError);
__DRIimage *DRIMODCreateImageFromBuffer(struct DRISUPContext *psDRISUPContext,
                                        int iTarget, void *pvBuffer,
                                        unsigned int *puError,
                                        void *pvLoaderPrivate);

int DRIMODQueryRendererInteger(struct DRISUPScreen *psDRISUPScreen,
                               int iAttribute, unsigned int *puValue);
int DRIMODQueryRendererString(struct DRISUPScreen *psDRISUPScreen,
                              int iAttribute, const char **ppszValue);

void *DRIMODCreateFence(struct DRISUPContext *psDRISUPContext);
void DRIMODDestroyFence(struct DRISUPScreen *psDRISUPScreen, void *pvFence);
bool DRIMODClientWaitSync(struct DRISUPContext *psDRISUPContext,
                          void *pvFence, unsigned int uFlags,
                          uint64_t uTimeout);
void DRIMODServerWaitSync(struct DRISUPContext *psDRISUPContext,
                          void *pvFence, unsigned int uFlags);
unsigned int DRIMODGetFenceCapabilities(struct DRISUPScreen *psDRISUPScreen);
void *DRIMODCreateFenceFD(struct DRISUPContext *psDRISUPContext, int iFD);
int DRIMODGetFenceFD(struct DRISUPScreen *psDRISUPScreen, void *pvFence);

unsigned int DRIMODGetNumAPIProcs(struct DRISUPScreen *psDRISUPScreen,
                                  PVRDRIAPIType eAPI);
const char *DRIMODGetAPIProcName(struct DRISUPScreen *psDRISUPScreen,
                                 PVRDRIAPIType eAPI, unsigned int uIndex);
void *DRIMODGetAPIProcAddress(struct DRISUPScreen *psDRISUPScreen,
                              PVRDRIAPIType eAPI, unsigned int uIndex);

void DRIMODSetDamageRegion(struct DRISUPDrawable *psDRISUPDrawable,
                           unsigned int uNRects, int *piRects);

#endif /* defined(__PVRDRI_MOD_H__) */
