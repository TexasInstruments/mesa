/* -*- mode: c; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* vi: set ts=8 sw=8 sts=8: */
/*************************************************************************/ /*!
@File
@Title          PVR DRI interface definition
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        MIT

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/ /**************************************************************************/

#if !defined(__PVRDRIIFCE_H__)
#define __PVRDRIIFCE_H__

#include <stdint.h>
#include <stdbool.h>

#include "imgpixfmts.h"
#include "imgyuv.h"

typedef enum
{
	PVRDRI_DEVICE_TYPE_INVALID = 0,
	PVRDRI_DEVICE_TYPE_UNKNOWN,
	PVRDRI_DEVICE_TYPE_DISPLAY,
	PVRDRI_DEVICE_TYPE_RENDER,
	PVRDRI_DEVICE_TYPE_CARD,
} PVRDRIDeviceType;

/* API type. */
typedef enum
{
	PVRDRI_API_NONE = 0,
	PVRDRI_API_GLES1 = 2,
	PVRDRI_API_GLES2 = 3,
	PVRDRI_API_CL = 4,
	PVRDRI_API_GL_COMPAT = 5,
	PVRDRI_API_GL_CORE = 6,
} PVRDRIAPIType;

/* API sub type. */
typedef enum
{
	PVRDRI_API_SUB_NONE,
} PVRDRIAPISubType;

typedef enum
{
	PVRDRI_DRAWABLE_NONE = 0,
	PVRDRI_DRAWABLE_WINDOW = 1,
	PVRDRI_DRAWABLE_PIXMAP = 2,
	PVRDRI_DRAWABLE_PBUFFER = 3,
} PVRDRIDrawableType;

typedef enum
{
	PVRDRI_IMAGE = 1,
	PVRDRI_IMAGE_FROM_NAMES,
	PVRDRI_IMAGE_FROM_EGLIMAGE,
	PVRDRI_IMAGE_FROM_DMABUFS,
	PVRDRI_IMAGE_SUBIMAGE,
} PVRDRIImageType;

typedef enum
{
	PVRDRI_EGLIMAGE_NONE = 0,
	PVRDRI_EGLIMAGE_IMGEGL,
	PVRDRI_EGLIMAGE_IMGOCL,
} PVRDRIEGLImageType;

typedef enum
{
	/* Since PVRDRICallbacks version 2 */
	PVRDRI_CONFIG_ATTRIB_INVALID = 0,
	PVRDRI_CONFIG_ATTRIB_RENDERABLE_TYPE = 1,
	PVRDRI_CONFIG_ATTRIB_RGB_MODE = 2,
	PVRDRI_CONFIG_ATTRIB_DOUBLE_BUFFER_MODE = 3,
	PVRDRI_CONFIG_ATTRIB_RED_BITS = 4,
	PVRDRI_CONFIG_ATTRIB_GREEN_BITS = 5,
	PVRDRI_CONFIG_ATTRIB_BLUE_BITS = 6,
	PVRDRI_CONFIG_ATTRIB_ALPHA_BITS = 7,
	PVRDRI_CONFIG_ATTRIB_RGB_BITS = 8,
	PVRDRI_CONFIG_ATTRIB_DEPTH_BITS = 9,
	PVRDRI_CONFIG_ATTRIB_STENCIL_BITS = 10,
	PVRDRI_CONFIG_ATTRIB_SAMPLE_BUFFERS = 11,
	PVRDRI_CONFIG_ATTRIB_SAMPLES = 12,
	PVRDRI_CONFIG_ATTRIB_BIND_TO_TEXTURE_RGB = 13,
	PVRDRI_CONFIG_ATTRIB_BIND_TO_TEXTURE_RGBA = 14,
	PVRDRI_CONFIG_ATTRIB_YUV_ORDER = 15,
	PVRDRI_CONFIG_ATTRIB_YUV_NUM_OF_PLANES = 16,
	PVRDRI_CONFIG_ATTRIB_YUV_SUBSAMPLE = 17,
	PVRDRI_CONFIG_ATTRIB_YUV_DEPTH_RANGE = 18,
	PVRDRI_CONFIG_ATTRIB_YUV_CSC_STANDARD = 19,
	PVRDRI_CONFIG_ATTRIB_YUV_PLANE_BPP = 20,

	/* Valid for use with with PVRDRICallbacksV2 */
	PVRDRI_CONFIG_ATTRIB_RED_MASK = 21,
	PVRDRI_CONFIG_ATTRIB_GREEN_MASK = 22,
	PVRDRI_CONFIG_ATTRIB_BLUE_MASK = 23,
	PVRDRI_CONFIG_ATTRIB_ALPHA_MASK = 24,
	PVRDRI_CONFIG_ATTRIB_SRGB_CAPABLE = 25
} PVRDRIConfigAttrib;

typedef enum
{
	/* Since PVRDRICallbacks version 2 */
	PVRDRI_BUFFER_ATTRIB_INVALID = 0,
	PVRDRI_BUFFER_ATTRIB_TYPE = 1,
	PVRDRI_BUFFER_ATTRIB_WIDTH = 2,
	PVRDRI_BUFFER_ATTRIB_HEIGHT = 3,
	PVRDRI_BUFFER_ATTRIB_STRIDE = 4,
	PVRDRI_BUFFER_ATTRIB_PIXEL_FORMAT = 5,
} PVRDRIBufferAttrib;

/* The context flags match their __DRI_CTX_FLAG and EGL_CONTEXT counterparts */
#define PVRDRI_CONTEXT_FLAG_DEBUG			0x00000001
#define PVRDRI_CONTEXT_FLAG_FORWARD_COMPATIBLE		0x00000002
#define PVRDRI_CONTEXT_FLAG_ROBUST_BUFFER_ACCESS	0x00000004

/* The context error codes match their __DRI_CTX_ERROR counterparts */
#define PVRDRI_CONTEXT_ERROR_SUCCESS			0
/* Out of memory */
#define PVRDRI_CONTEXT_ERROR_NO_MEMORY			1
/* Unsupported API */
#define PVRDRI_CONTEXT_ERROR_BAD_API			2
/* Unsupported version of API */
#define PVRDRI_CONTEXT_ERROR_BAD_VERSION		3
/* Unsupported context flag or combination of flags */
#define PVRDRI_CONTEXT_ERROR_BAD_FLAG			4
/* Unrecognised context attribute */
#define PVRDRI_CONTEXT_ERROR_UNKNOWN_ATTRIBUTE		5
/* Unrecognised context flag */
#define PVRDRI_CONTEXT_ERROR_UNKNOWN_FLAG		6

/*
 * The context priority defines match their __DRI_CTX counterparts, and
 * the context priority values used by the DDK.
 */
#define PVRDRI_CONTEXT_PRIORITY_LOW	0
#define PVRDRI_CONTEXT_PRIORITY_MEDIUM	1
#define PVRDRI_CONTEXT_PRIORITY_HIGH	2

/* The image error flags match their __DRI_IMAGE_ERROR counterparts */
#define PVRDRI_IMAGE_ERROR_SUCCESS		0
#define PVRDRI_IMAGE_ERROR_BAD_ALLOC		1
#define PVRDRI_IMAGE_ERROR_BAD_MATCH		2
#define PVRDRI_IMAGE_ERROR_BAD_PARAMETER	3
#define PVRDRI_IMAGE_ERROR_BAD_ACCESS		4

/* The buffer flags match their __DRI_IMAGE_USE counterparts */
#define PVDRI_BUFFER_USE_SHARE		0x0001
#define PVDRI_BUFFER_USE_SCANOUT	0x0002
#define PVDRI_BUFFER_USE_CURSOR		0x0004
#define PVDRI_BUFFER_USE_LINEAR		0x0008

/* EGL_RENDERABLE_TYPE mask bits */
#define PVRDRI_API_BIT_GLES		0x0001
#define PVRDRI_API_BIT_GLES2		0x0004
#define PVRDRI_API_BIT_GL		0x0008
#define PVRDRI_API_BIT_GLES3		0x0040

/* Mesa config formats. These need not match their MESA_FORMAT counterparts */
#define	PVRDRI_MESA_FORMAT_NONE			0
#define PVRDRI_MESA_FORMAT_B8G8R8A8_UNORM	1
#define PVRDRI_MESA_FORMAT_B8G8R8X8_UNORM	2
#define PVRDRI_MESA_FORMAT_B5G6R5_UNORM		3
#define PVRDRI_MESA_FORMAT_R8G8B8A8_UNORM	4
#define PVRDRI_MESA_FORMAT_R8G8B8X8_UNORM	5
#define PVRDRI_MESA_FORMAT_YCBCR		6
#define PVRDRI_MESA_FORMAT_YUV420_2PLANE	7
#define PVRDRI_MESA_FORMAT_YVU420_2PLANE	8
#define PVRDRI_MESA_FORMAT_B8G8R8A8_SRGB	9
#define PVRDRI_MESA_FORMAT_R8G8B8A8_SRGB	10

/* The blit flags match their DRI counterparts */
#define PVRDRI_BLIT_FLAG_FLUSH		0x0001
#define PVRDRI_BLIT_FLAG_FINISH		0x0002

/* The image mapping flags match their DRI counterparts */
#define	PVRDRI_IMAGE_TRANSFER_READ		0x1
#define	PVRDRI_IMAGE_TRANSFER_WRITE		0x2
#define	PVRDRI_IMAGE_TRANSFER_READ_WRITE	\
		(PVRDRI_IMAGE_TRANSFER_READ | PVRDRI_IMAGE_TRANSFER_WRITE)

/* The PVRDRI_YUV defines match their __DRI_ATTRIB_YUV counterparts */
#define PVRDRI_YUV_ORDER_NONE 0x0
#define PVRDRI_YUV_ORDER_YUV  0x1
#define PVRDRI_YUV_ORDER_YVU  0x2
#define PVRDRI_YUV_ORDER_YUYV 0x4
#define PVRDRI_YUV_ORDER_UYVY 0x8
#define PVRDRI_YUV_ORDER_YVYU 0x10
#define PVRDRI_YUV_ORDER_VYUY 0x20
#define PVRDRI_YUV_ORDER_AYUV 0x40

#define PVRDRI_YUV_SUBSAMPLE_NONE  0x0
#define PVRDRI_YUV_SUBSAMPLE_4_2_0 0x1
#define PVRDRI_YUV_SUBSAMPLE_4_2_2 0x2
#define PVRDRI_YUV_SUBSAMPLE_4_4_4 0x4

#define PVRDRI_YUV_DEPTH_RANGE_NONE    0x0
#define PVRDRI_YUV_DEPTH_RANGE_LIMITED 0x1
#define PVRDRI_YUV_DEPTH_RANGE_FULL    0x2

#define PVRDRI_YUV_CSC_STANDARD_NONE 0x0
#define PVRDRI_YUV_CSC_STANDARD_601  0x1
#define PVRDRI_YUV_CSC_STANDARD_709  0x2
#define PVRDRI_YUV_CSC_STANDARD_2020 0x4

#define PVRDRI_YUV_PLANE_BPP_NONE 0x0
#define PVRDRI_YUV_PLANE_BPP_0    0x1
#define PVRDRI_YUV_PLANE_BPP_8    0x2
#define PVRDRI_YUV_PLANE_BPP_10   0x4

/* Flags for PVRDRICallbacks.DrawableGetParametersV2 */
/* Since PVRDRICallbacks version 2 */
#define PVRDRI_GETPARAMS_FLAG_ALLOW_RECREATE	0x1
/* Since PVRDRICallbacks version 3 */
#define PVRDRI_GETPARAMS_FLAG_NO_UPDATE		0x2

/*
 * Capabilities that might be returned by PVRDRIInterface.GetFenceCapabilities.
 * These match their _DRI_FENCE_CAP counterparts.
 *
 * Since PVRDRIInterface version 2.
 */
#define	PVRDRI_FENCE_CAP_NATIVE_FD 0x1

typedef struct
{
	IMG_PIXFMT          ePixFormat;
	uint32_t            uiWidth;
	uint32_t            uiHeight;
	uint32_t            uiStrideInBytes;
} PVRDRIBufferAttribs;

typedef struct
{
	int sampleBuffers;
	int samples;

	int redBits;
	int greenBits;
	int blueBits;
	int alphaBits;

	int rgbBits;
	int depthBits;
	int stencilBits;

	bool doubleBufferMode;

	int bindToTextureRgb;
	int bindToTextureRgba;
} PVRDRIConfigInfo;

typedef struct IMGEGLImageRec IMGEGLImage;
typedef struct __DRIimageRec __DRIimage;

/* PVRDRI interface opaque types */
typedef struct PVRDRIScreenImplRec PVRDRIScreenImpl;
typedef struct PVRDRIContextImplRec PVRDRIContextImpl;
typedef struct PVRDRIDrawableImplRec PVRDRIDrawableImpl;
typedef struct PVRDRIBufferImplRec PVRDRIBufferImpl;

typedef struct PVRDRIDrawable_TAG PVRDRIDrawable;

typedef void (*PVRDRIGLAPIProc)(void);

/* Since PVRDRICallbacks version 2 */
typedef struct PVRDRIConfigRec PVRDRIConfig;

/* PVRDRISupportInterface (deprecated) */
typedef struct {
	/**********************************************************************
	 * Version 0 interface
	 **********************************************************************/

	PVRDRIDeviceType     (*GetDeviceTypeFromFd)(int iFd);

	bool                 (*IsFirstScreen)(PVRDRIScreenImpl *psScreenImpl);

	uint32_t             (*PixFmtGetDepth)(IMG_PIXFMT eFmt);
	uint32_t             (*PixFmtGetBPP)(IMG_PIXFMT eFmt);
	uint32_t             (*PixFmtGetBlockSize)(IMG_PIXFMT eFmt);

	/* ScreenImpl functions */
	PVRDRIScreenImpl    *(*CreateScreen)(int iFd);
	void                 (*DestroyScreen)(PVRDRIScreenImpl *psScreenImpl);

	int                  (*APIVersion)(PVRDRIAPIType eAPI,
	                                   PVRDRIAPISubType eAPISub,
	                                   PVRDRIScreenImpl *psScreenImpl);

	void                *(*EGLGetLibHandle)(PVRDRIAPIType eAPI,
	                                        PVRDRIScreenImpl *psScreenImpl);

	PVRDRIGLAPIProc      (*EGLGetProcAddress)(PVRDRIAPIType eAPI,
	                                          PVRDRIScreenImpl *psScreenImpl,
	                                          const char *psProcName);

	bool                 (*EGLFlushBuffers)(PVRDRIAPIType eAPI,
	                                        PVRDRIScreenImpl *psScreenImpl,
	                                        PVRDRIContextImpl *psContextImpl,
	                                        PVRDRIDrawableImpl *psDrawableImpl,
	                                        bool bFlushAllSurfaces,
	                                        bool bSwapBuffers,
	                                        bool bWaitForHW);
	/* Obsolete (no longer supported) */
	bool                 (*EGLFreeResources)(PVRDRIScreenImpl *psPVRScreenImpl);
	void                 (*EGLMarkRendersurfaceInvalid)(PVRDRIAPIType eAPI,
	                                                    PVRDRIScreenImpl *psScreenImpl,
	                                                    PVRDRIContextImpl *psContextImpl);
	/* Obsolete (no longer supported) */
	void                 (*EGLSetFrontBufferCallback)(PVRDRIAPIType eAPI,
	                                                  PVRDRIScreenImpl *psScreenImpl,
	                                                  PVRDRIDrawableImpl *psDrawableImpl,
	                                                  void (*pfnCallback)(PVRDRIDrawable *));

	/* Obsolete (no longer supported) */
	unsigned             (*CreateContext)(PVRDRIContextImpl **ppsContextImpl,
	                                          PVRDRIAPIType eAPI,
	                                          PVRDRIAPISubType eAPISub,
	                                          PVRDRIScreenImpl *psScreenImpl,
	                                          const PVRDRIConfigInfo *psConfigInfo,
	                                          unsigned uMajorVersion,
	                                          unsigned uMinorVersion,
	                                          uint32_t uFlags,
	                                          bool bNotifyReset,
	                                          unsigned uPriority,
	                                          PVRDRIContextImpl *psSharedContextImpl);

	void                 (*DestroyContext)(PVRDRIContextImpl *psContextImpl,
	                                           PVRDRIAPIType eAPI,
	                                           PVRDRIScreenImpl *psScreenImpl);

	bool                 (*MakeCurrentGC)(PVRDRIAPIType eAPI,
	                                      PVRDRIScreenImpl *psScreenImpl,
	                                      PVRDRIContextImpl *psContextImpl,
	                                      PVRDRIDrawableImpl *psWriteImpl,
	                                      PVRDRIDrawableImpl *psReadImpl);

	void                 (*MakeUnCurrentGC)(PVRDRIAPIType eAPI,
	                                        PVRDRIScreenImpl *psScreenImpl);

	unsigned             (*GetImageSource)(PVRDRIAPIType eAPI,
	                                       PVRDRIScreenImpl *psScreenImpl,
	                                       PVRDRIContextImpl *psContextImpl,
	                                       uint32_t  uiTarget,
	                                       uintptr_t uiBuffer,
	                                       uint32_t  uiLevel,
	                                       IMGEGLImage *psEGLImage);

	bool                 (*BindTexImage)(PVRDRIAPIType eAPI,
	                                     PVRDRIScreenImpl *psScreenImpl,
	                                     PVRDRIContextImpl *psContextImpl,
	                                     PVRDRIDrawableImpl *psDrawableImpl);

	void                 (*ReleaseTexImage)(PVRDRIAPIType eAPI,
	                                        PVRDRIScreenImpl *psScreenImpl,
	                                        PVRDRIContextImpl *psContextImpl,
	                                        PVRDRIDrawableImpl *psDrawableImpl);

	/* Obsolete (no longer supported) */
	PVRDRIDrawableImpl  *(*CreateDrawable)(PVRDRIDrawable *psPVRDrawable);

	void                 (*DestroyDrawable)(PVRDRIDrawableImpl *psScreenImpl);
	bool                 (*EGLDrawableCreate)(PVRDRIScreenImpl *psScreenImpl,
	                                          PVRDRIDrawableImpl *psDrawableImpl);
	bool                 (*EGLDrawableRecreate)(PVRDRIScreenImpl *psScreenImpl,
	                                            PVRDRIDrawableImpl *psDrawableImpl);
	bool                 (*EGLDrawableDestroy)(PVRDRIScreenImpl *psScreenImpl,
	                                           PVRDRIDrawableImpl *psDrawableImpl);
	void                 (*EGLDrawableDestroyConfig)(PVRDRIDrawableImpl *psDrawableImpl);

	/* Buffer functions */
	PVRDRIBufferImpl    *(*BufferCreate)(PVRDRIScreenImpl *psScreenImpl,
	                                     int iWidth,
	                                     int iHeight,
	                                     unsigned int uiBpp,
	                                     unsigned int uiUseFlags,
	                                     unsigned int *puiStride);

	PVRDRIBufferImpl    *(*BufferCreateWithModifiers)(PVRDRIScreenImpl *psScreenImpl,
	                                                  int iWidth,
	                                                  int iHeight,
	                                                  int iFormat,
	                                                  IMG_PIXFMT eIMGPixelFormat,
	                                                  const uint64_t *puiModifiers,
	                                                  unsigned int uiModifierCount,
	                                                  unsigned int *puiStride);

	PVRDRIBufferImpl    *(*BufferCreateFromNames)(PVRDRIScreenImpl *psScreenImpl,
	                                              int iWidth,
	                                              int iHeight,
	                                              unsigned uiNumPlanes,
	                                              const int *piName,
	                                              const int *piStride,
	                                              const int *piOffset,
	                                              const unsigned int *puiWidthShift,
	                                              const unsigned int *puiHeightShift);

	/* Obsolete (no longer supported) */
	PVRDRIBufferImpl    *(*BufferCreateFromName)(PVRDRIScreenImpl *psScreenImpl,
	                                             int iName,
	                                             int iWidth,
	                                             int iHeight,
	                                             int iStride,
	                                             int iOffset);

	/* Obsolete (no longer supported) */
	PVRDRIBufferImpl    *(*BufferCreateFromFds)(PVRDRIScreenImpl *psScreenImpl,
	                                            int iWidth,
	                                            int iHeight,
	                                            unsigned uiNumPlanes,
	                                            const int *piFd,
	                                            const int *piStride,
	                                            const int *piOffset,
	                                            const unsigned int *puiWidthShift,
	                                            const unsigned int *puiHeightShift);

	PVRDRIBufferImpl    *(*BufferCreateFromFdsWithModifier)(PVRDRIScreenImpl *psScreenImpl,
	                                                        int iWidth,
	                                                        int iHeight,
	                                                        uint64_t uiModifier,
	                                                        unsigned uiNumPlanes,
	                                                        const int *piFd,
	                                                        const int *piStride,
	                                                        const int *piOffset,
	                                                        const unsigned int *puiWidthShift,
	                                                        const unsigned int *puiHeightShift);

	PVRDRIBufferImpl    *(*SubBufferCreate)(PVRDRIScreenImpl *psScreen,
	                                        PVRDRIBufferImpl *psParent,
	                                        int plane);

	void                 (*BufferDestroy)(PVRDRIBufferImpl *psBuffer);

	int                  (*BufferGetFd)(PVRDRIBufferImpl *psBuffer);

	int                  (*BufferGetHandle)(PVRDRIBufferImpl *psBuffer);

	uint64_t             (*BufferGetModifier)(PVRDRIBufferImpl *psBuffer);

	int                  (*BufferGetName)(PVRDRIBufferImpl *psBuffer);

	int                  (*BufferGetOffset)(PVRDRIBufferImpl *psBuffer);

	/* Image functions */
	IMGEGLImage         *(*EGLImageCreate)(void);
	IMGEGLImage         *(*EGLImageCreateFromBuffer)(int iWidth,
	                                                 int iHeight,
	                                                 int iStride,
	                                                 IMG_PIXFMT ePixelFormat,
	                                                 IMG_YUV_COLORSPACE eColourSpace,
	                                                 IMG_YUV_CHROMA_INTERP eChromaUInterp,
	                                                 IMG_YUV_CHROMA_INTERP eChromaVInterp,
	                                                 PVRDRIBufferImpl *psBuffer);

	IMGEGLImage         *(*EGLImageCreateFromSubBuffer)(IMG_PIXFMT ePixelFormat,
	                                                    PVRDRIBufferImpl *psSubBuffer);

	IMGEGLImage         *(*EGLImageDup)(IMGEGLImage *psIn);

	void                 (*EGLImageSetCallbackData)(IMGEGLImage *psEGLImage, __DRIimage *image);

	void                 (*EGLImageDestroyExternal)(PVRDRIScreenImpl *psScreenImpl,
	                                                IMGEGLImage *psEGLImage,
	                                                PVRDRIEGLImageType eglImageType);
	void                 (*EGLImageFree)(IMGEGLImage *psEGLImage);

	void                 (*EGLImageGetAttribs)(IMGEGLImage *psEGLImage,
	                                           PVRDRIBufferAttribs *psAttribs);

	/* Sync functions */
	void                *(*CreateFence)(PVRDRIAPIType eAPI,
	                                    PVRDRIScreenImpl *psScreenImpl,
	                                    PVRDRIContextImpl *psContextImpl);

	void                 (*DestroyFence)(void *psDRIFence);

	/*
	 * When calling ClientWaitSync, the eAPI, psContextImpl and
	 * bFlushCommands parameters must be set to PVRDRI_API_NONE,
	 * NULL and false, respectively.
	 */
	bool                 (*ClientWaitSync)(PVRDRIAPIType eAPI,
	                                       PVRDRIContextImpl *psContextImpl,
	                                       void *psDRIFence,
	                                       bool bFlushCommands,
	                                       bool bTimeout,
	                                       uint64_t uiTimeout);

	bool                 (*ServerWaitSync)(PVRDRIAPIType eAPI,
	                                       PVRDRIContextImpl *psContextImpl,
	                                       void *psDRIFence);

	/* Obsolete (no longer supported) */
	void                 (*DestroyFences)(PVRDRIScreenImpl *psScreenImpl);

	/* EGL interface functions */

	/* Obsolete (no longer supported) */
	bool                 (*EGLDrawableConfigFromGLMode)(PVRDRIDrawableImpl *psPVRDrawable,
	                                                    PVRDRIConfigInfo *psConfigInfo,
	                                                    int supportedAPIs,
	                                                    IMG_PIXFMT ePixFmt);

	/* Blit functions */
	bool                 (*BlitEGLImage)(PVRDRIScreenImpl *psScreenImpl,
	                                     PVRDRIContextImpl *psContextImpl,
	                                     IMGEGLImage *psDstImage,
	                                     PVRDRIBufferImpl *psDstBuffer,
	                                     IMGEGLImage *psSrcImage,
	                                     PVRDRIBufferImpl *psSrcBuffer,
	                                     int iDstX, int iDstY,
	                                     int iDstWidth, int iDstHeight,
	                                     int iSrcX, int iSrcY,
	                                     int iSrcWidth, int iSrcHeight,
	                                     int iFlushFlag);

	/* Mapping functions */
	void                 *(*MapEGLImage)(PVRDRIScreenImpl *psScreenImpl,
	                                     PVRDRIContextImpl *psContextImpl,
	                                     IMGEGLImage *psImage,
	                                     PVRDRIBufferImpl *psBuffer,
	                                     int iX, int iY,
	                                     int iWidth, int iHeight,
	                                     unsigned iFlags,
	                                     int *piStride,
	                                     void **ppvData);

	bool                 (*UnmapEGLImage)(PVRDRIScreenImpl *psScreenImpl,
	                                      PVRDRIContextImpl *psContextImpl,
	                                      IMGEGLImage *psImage, PVRDRIBufferImpl *psBuffer,
	                                      void *pvData);

	/* PVR utility support functions */
	bool                 (*MesaFormatSupported)(unsigned fmt);
	unsigned             (*DepthStencilBitArraySize)(void);
	const uint8_t       *(*DepthBitsArray)(void);
	const uint8_t       *(*StencilBitsArray)(void);
	unsigned             (*MSAABitArraySize)(void);
	const uint8_t       *(*MSAABitsArray)(void);
	uint32_t             (*MaxPBufferWidth)(void);
	uint32_t             (*MaxPBufferHeight)(void);

	unsigned             (*GetNumAPIFuncs)(PVRDRIAPIType eAPI);
	const char          *(*GetAPIFunc)(PVRDRIAPIType eAPI, unsigned index);

	int                  (*QuerySupportedFormats)(PVRDRIScreenImpl *psScreenImpl,
	                                              unsigned uNumFormats,
	                                              const int *piFormats,
	                                              const IMG_PIXFMT *peImgFormats,
	                                              bool *pbSupported);

	int                  (*QueryModifiers)(PVRDRIScreenImpl *psScreenImpl,
	                                       int iFormat,
	                                       IMG_PIXFMT eImgFormat,
	                                       uint64_t *puModifiers,
	                                       unsigned *puExternalOnly);

	/**********************************************************************
	 * Version 1 functions
	 **********************************************************************/

	unsigned             (*CreateContextV1)(PVRDRIScreenImpl *psScreenImpl,
						PVRDRIContextImpl *psSharedContextImpl,
						PVRDRIConfig *psConfig,
						PVRDRIAPIType eAPI,
						PVRDRIAPISubType eAPISub,
						unsigned uMajorVersion,
						unsigned uMinorVersion,
						uint32_t uFlags,
						bool bNotifyReset,
						unsigned uPriority,
						PVRDRIContextImpl **ppsContextImpl);

	PVRDRIDrawableImpl  *(*CreateDrawableWithConfig)(PVRDRIDrawable *psPVRDrawable,
							 PVRDRIConfig *psConfig);

	/**********************************************************************
	 * Version 2 functions
	 **********************************************************************/

	unsigned             (*GetFenceCapabilities)(PVRDRIScreenImpl *psScreenImpl);

	void                *(*CreateFenceFd)(PVRDRIAPIType eAPI,
	                                      PVRDRIScreenImpl *psScreenImpl,
	                                      PVRDRIContextImpl *psContextImpl,
                                              int iFd);

	int                  (*GetFenceFd)(void *psDRIFence);
} PVRDRISupportInterface;

/* Callbacks into non-impl layer (deprecated) */
typedef struct
{
	/**********************************************************************
	 * Version 0 callbacks
	 **********************************************************************/

	/* Obsolete (no longer supported) */
	bool                 (*DrawableRecreate)(PVRDRIDrawable *psPVRDrawable);
	/* Obsolete (no longer supported) */
	bool                 (*DrawableGetParameters)(PVRDRIDrawable *psPVRDrawable,
	                                              PVRDRIBufferImpl **ppsDstBuffer,
	                                              PVRDRIBufferImpl **ppsAccumBuffer,
	                                              PVRDRIBufferAttribs *psAttribs,
	                                              bool *pbDoubleBuffered);

	PVRDRIImageType      (*ImageGetSharedType)(__DRIimage *image);
	PVRDRIBufferImpl    *(*ImageGetSharedBuffer)(__DRIimage *image);
	IMGEGLImage           *(*ImageGetSharedEGLImage)(__DRIimage *image);
	IMGEGLImage           *(*ImageGetEGLImage)(__DRIimage *image);
	__DRIimage          *(*ScreenGetDRIImage)(void *hEGLImage);
	void                 (*RefImage)(__DRIimage *image);
	void                 (*UnrefImage)(__DRIimage *image);

	/*
	 * If the DRI module calls PVRDRIRegisterCallbacks, or
	 * PVRDRIRegisterVersionedCallbacks with any version number,
	 * the DRI support library can use the callbacks above.
	 * The callbacks below can only be called if
	 * PVRDRIRegisterVersionedCallbacks is called with a suitable
	 * version number.
	 */

	/**********************************************************************
	 * Version 1 callbacks
	 **********************************************************************/

	/* Obsolete (no longer supported) */
	bool                 (*DrawableGetParametersV1)(PVRDRIDrawable *psPVRDrawable,
                                                        bool bAllowRecreate,
	                                                PVRDRIBufferImpl **ppsDstBuffer,
	                                                PVRDRIBufferImpl **ppsAccumBuffer,
	                                                PVRDRIBufferAttribs *psAttribs,
	                                                bool *pbDoubleBuffered);

	/*
	 * Register the DRI Support interface with the DRI module.
	 * The caller is not required to preserve the PVRDRICallbacks structure
	 * after the call, so the callee must make a copy.
	 */
	bool                 (*RegisterSupportInterfaceV1)(const PVRDRISupportInterface *psInterface,
							   unsigned uVersion);

	/**********************************************************************
	 * Version 2 callbacks
	 **********************************************************************/

	bool                 (*ConfigQuery)(const PVRDRIConfig *psConfig,
	                                    PVRDRIConfigAttrib eConfigAttrib,
	                                    int *piValueOut);
	/*
	 * DrawableGetParametersV2 is a replacement for DrawableGetParametersV1.
	 * Unlike earlier versions, the caller is expected to query drawable
	 * information (via DrawableQuery) instead of this information being
	 * returned by the callback.
	 */
	bool                 (*DrawableGetParametersV2)(PVRDRIDrawable *psPVRDrawable,
	                                                uint32_t uiFlags,
	                                                PVRDRIBufferImpl **ppsDstBuffer,
	                                                PVRDRIBufferImpl **ppsAccumBuffer);
	bool                 (*DrawableQuery)(const PVRDRIDrawable *psPVRDrawable,
	                                      PVRDRIBufferAttrib eBufferAttrib,
	                                      uint32_t *uiValueOut);

	/**********************************************************************
	 * Version 3 callbacks
	 **********************************************************************/
} PVRDRICallbacks;

/*
 * The caller of PVRDRIRegisterVersionedCallbacks is not required to preserve
 * the PVRDRICallbacks structure after the call, so the callee may need to
 * make a copy of the structure.
 */
bool PVRDRIRegisterVersionedCallbacks(const PVRDRICallbacks *psCallbacks,
				      unsigned uVersion);

/* The context flags match their __DRI_CTX_RESET counterparts */
#define PVRDRI_CONTEXT_RESET_NO_NOTIFICATION		0
#define PVRDRI_CONTEXT_RESET_LOSE_CONTEXT		1

/* The context flags match their __DRI_CTX_RELEASE counterparts */
#define PVRDRI_CONTEXT_RELEASE_BEHAVIOR_NONE		0
#define PVRDRI_CONTEXT_RELEASE_BEHAVIOR_FLUSH		1

/* The flush flags match their __DRI2_FLUSH counterparts */
#define	PVRDRI_FLUSH_DRAWABLE             (1 << 0)
#define PVRDRI_FLUSH_CONTEXT              (1 << 1)
#define PVRDRI_FLUSH_INVALIDATE_ANCILLARY (1 << 2)

/* The throttle reason defines match their __DRI2_THROTTLE counterparts */
#define PVRDRI_THROTTLE_SWAPBUFFER	0
#define PVRDRI_THROTTLE_COPYSUBBUFFER	1
#define PVRDRI_THROTTLE_FLUSHFRONT	2

/* The render query defines match their __DRI2_RENDERER counterparts */
#define PVRDRI_RENDERER_VENDOR_ID			0x0000
#define PVRDRI_RENDERER_DEVICE_ID			0x0001

#define PVRDRI_RENDERER_HAS_TEXTURE_3D			0x000b
#define PVRDRI_RENDERER_HAS_FRAMEBUFFER_SRGB		0x000c

#define PVRDRI_RENDERER_HAS_CONTEXT_PRIORITY		0x000d
#define PVRDRI_RENDERER_HAS_CONTEXT_PRIORITY_LOW	(1 << 0)
#define PVRDRI_RENDERER_HAS_CONTEXT_PRIORITY_MEDIUM	(1 << 1)
#define PVRDRI_RENDERER_HAS_CONTEXT_PRIORITY_HIGH	(1 << 2)

#define PVRDRI_RENDERER_OPENGL_ES2_CONTEXT_CLIENT_VERSION_IMG 0x7001

/* The fence extension defines match their __DRI2_FENCE counterparts */
#define PVRDRI_FENCE_TIMEOUT_INFINITE		0xffffffffffffffffull
#define PVRDRI_FENCE_FLAG_FLUSH_COMMANDS	(1 << 0)

/* The YUV defines match their __DRI_YUV counterparts */
#define PVRDRI_YUV_COLOR_SPACE_UNDEFINED	0
#define PVRDRI_YUV_COLOR_SPACE_ITU_REC601	0x327F
#define PVRDRI_YUV_COLOR_SPACE_ITU_REC709	0x3280
#define PVRDRI_YUV_COLOR_SPACE_ITU_REC2020	0x3281

#define PVRDRI_YUV_RANGE_UNDEFINED		0
#define PVRDRI_YUV_FULL_RANGE			0x3282
#define PVRDRI_YUV_NARROW_RANGE			0x3283

#define PVRDRI_YUV_CHROMA_SITING_UNDEFINED	0
#define PVRDRI_YUV_CHROMA_SITING_0		0x3284
#define PVRDRI_YUV_CHROMA_SITING_0_5		0x3285

/*
 * The image component defines match their __DRI2_IMAGE_COMPONENTS
 * counterparts.
 */
#define PVRDRI_IMAGE_COMPONENTS_RGB		0x3001
#define PVRDRI_IMAGE_COMPONENTS_RGBA		0x3002
#define PVRDRI_IMAGE_COMPONENTS_Y_U_V		0x3003
#define PVRDRI_IMAGE_COMPONENTS_Y_UV		0x3004
#define PVRDRI_IMAGE_COMPONENTS_Y_XUXV		0x3005
#define PVRDRI_IMAGE_COMPONENTS_R		0x3006
#define PVRDRI_IMAGE_COMPONENTS_RG		0x3007
#define PVRDRI_IMAGE_COMPONENTS_Y_UXVX		0x3008
#define PVRDRI_IMAGE_COMPONENTS_AYUV		0x3009
#define PVRDRI_IMAGE_COMPONENTS_XYUV		0x300A
#define PVRDRI_IMAGE_COMPONENTS_EXTERNAL	0x300B

/*
 * The image format modifier attribute defines match their
 * __DRI_IMAGE_FORMAT_MODIFIER_ATTRIB counterparts.
 */
#define PVRDRI_IMAGE_FORMAT_MODIFIER_ATTRIB_PLANE_COUNT	0x0001

/* The image attribute defines match their __DRI_IMAGE_ATTRIB counterparts */
#define PVRDRI_IMAGE_ATTRIB_STRIDE		0x2000
#define PVRDRI_IMAGE_ATTRIB_HANDLE		0x2001
#define PVRDRI_IMAGE_ATTRIB_NAME		0x2002
#define PVRDRI_IMAGE_ATTRIB_FORMAT		0x2003
#define PVRDRI_IMAGE_ATTRIB_WIDTH		0x2004
#define PVRDRI_IMAGE_ATTRIB_HEIGHT		0x2005
#define PVRDRI_IMAGE_ATTRIB_COMPONENTS		0x2006
#define PVRDRI_IMAGE_ATTRIB_FD			0x2007
#define PVRDRI_IMAGE_ATTRIB_FOURCC		0x2008
#define PVRDRI_IMAGE_ATTRIB_NUM_PLANES		0x2009
#define PVRDRI_IMAGE_ATTRIB_OFFSET		0x200A
#define PVRDRI_IMAGE_ATTRIB_MODIFIER_LOWER	0x200B
#define PVRDRI_IMAGE_ATTRIB_MODIFIER_UPPER	0x200C

/* The GL defines match their EGL_GL counterparts */
#define PVRDRI_GL_RENDERBUFFER			0x30B9
#define PVRDRI_GL_TEXTURE_2D			0x30B1
#define PVRDRI_GL_TEXTURE_3D			0x30B2
#define PVRDRI_GL_TEXTURE_CUBE_MAP_POSITIVE_X	0x30B3
#define PVRDRI_GL_TEXTURE_CUBE_MAP_NEGATIVE_X	0x30B4
#define PVRDRI_GL_TEXTURE_CUBE_MAP_POSITIVE_Y	0x30B5
#define PVRDRI_GL_TEXTURE_CUBE_MAP_NEGATIVE_Y	0x30B6
#define PVRDRI_GL_TEXTURE_CUBE_MAP_POSITIVE_Z	0x30B7
#define PVRDRI_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z	0x30B8

/* The CL defines match their EGL_CL counterparts */
#define PVRDRI_CL_IMAGE_IMG			0x6010

/* The swap attribute defines match their __DRI_ATTRIB_SWAP counterparts */
#define PVRDRI_ATTRIB_SWAP_NONE			0x0000
#define PVRDRI_ATTRIB_SWAP_EXCHANGE		0x8061
#define PVRDRI_ATTRIB_SWAP_COPY			0x8062
#define PVRDRI_ATTRIB_SWAP_UNDEFINED		0x8063

struct __DRIscreenRec;
struct __DRIcontextRec;
struct __DRIdrawableRec;
struct __DRIbufferRec;
struct __DRIconfigRec;

struct DRISUPScreen;
struct DRISUPContext;
struct DRISUPDrawable;
struct DRISUPBuffer;

struct PVRDRIContextConfig
{
	unsigned int uMajorVersion;
	unsigned int uMinorVersion;
	uint32_t uFlags;

	int iResetStrategy;
	unsigned int uPriority;
	int iReleaseBehavior;
};

/* The image capability defines match their __DRI_IMAGE_CAP counterparts */
#define	PVRDRI_IMAGE_CAP_GLOBAL_NAMES	1

/*
 * PVR DRI Support interface V2.
 * This structure may change over time, as older interfaces become obsolete.
 * For example, the v0 interface may be removed if superseded by newer
 * interfaces.
 */
struct  PVRDRISupportInterfaceV2
{
	struct
	{
		struct DRISUPScreen *(*CreateScreen)
			(struct __DRIscreenRec *psDRIScreen,
			 int iFD,
			 bool bUseInvalidate,
			 void *pvLoaderPrivate,
			 const struct __DRIconfigRec ***pppsConfigs,
			 int *piMaxGLES1Version,
			 int *piMaxGLES2Version);

		void (*DestroyScreen)
			(struct DRISUPScreen *psDRISUPScreen);

		unsigned int (*CreateContext)
			(PVRDRIAPIType eAPI,
			 PVRDRIConfig *psPVRDRIConfig,
			 struct PVRDRIContextConfig *psCtxConfig,
			 struct __DRIcontextRec *psDRIContext,
			 struct DRISUPContext *psDRISUPSharedContext,
			 struct DRISUPScreen *psDRISUPScreen,
			 struct DRISUPContext **ppsDRISUPContext);

		void (*DestroyContext)
			(struct DRISUPContext *psDRISUPContext);

		struct DRISUPDrawable *(*CreateDrawable)
			(struct __DRIdrawableRec *psDRIDrawable,
			 struct DRISUPScreen *psDRISUPDrawable,
			 void *pvLoaderPrivate,
			 PVRDRIConfig *psPVRDRIConfig);

		void (*DestroyDrawable)
			(struct DRISUPDrawable *psDRISUPDrawable);

		bool (*MakeCurrent)
			(struct DRISUPContext *psDRISUPContext,
			 struct DRISUPDrawable *psDRISUPWrite,
			 struct DRISUPDrawable *psDRISUPRead);

		bool (*UnbindContext)
			(struct DRISUPContext *psDRISUPContext);

		struct DRISUPBuffer *(*AllocateBuffer)
			(struct DRISUPScreen *psDRISUPScreen,
			 unsigned int uAttachment,
			 unsigned int uFormat,
			 int iWidth,
			 int iHeight,
			 unsigned int *puName,
			 unsigned int *puPitch,
			 unsigned int *puCPP,
			 unsigned int *puFlags);

		void (*ReleaseBuffer)
			(struct DRISUPScreen *psDRISUPScreen,
			 struct DRISUPBuffer *psDRISUPBuffer);

		/* Functions to support the DRI TexBuffer extension */
		void (*SetTexBuffer2)
			(struct DRISUPContext *psDRISUPContext,
			 int iTarget,
			 int iFormat,
			 struct DRISUPDrawable *psDRISUPDrawable);

		void (*ReleaseTexBuffer)
			(struct DRISUPContext *psDRISUPContext,
			 int iTarget,
			 struct DRISUPDrawable *psDRISUPDrawable);

		/* Functions to support the DRI Flush extension */
		void (*Flush)
			(struct DRISUPDrawable *psDRISUPDrawable);

		void (*Invalidate)
			(struct DRISUPDrawable *psDRISUPDrawable);

		void (*FlushWithFlags)
			(struct DRISUPContext *psDRISUPContext,
			 struct DRISUPDrawable *psDRISUPDrawable,
			 unsigned int uFlags,
			 unsigned int uThrottleReason);

		/* Functions to support the DRI Image extension */
		__DRIimage *(*CreateImageFromName)
			(struct DRISUPScreen *psDRISUPScreen,
			 int iWidth,
			 int iHeight,
			 int iFourCC,
			 int iName,
			 int iPitch,
			 void *pvLoaderPrivate);

		__DRIimage *(*CreateImageFromRenderbuffer)
			(struct DRISUPContext *psDRISUPContext,
			int iRenderBuffer,
			void *pvLoaderPrivate);

		void (*DestroyImage)
			(__DRIimage *psImage);

		__DRIimage *(*CreateImage)
			(struct DRISUPScreen *psDRISUPScreen,
			 int iWidth,
			 int iHeight,
			 int iFourCC,
			 unsigned int uUse,
			 void *pvLoaderPrivate);

		bool (*QueryImage)
			(__DRIimage *psImage,
			 int iAttrib,
			 int *iValue);

		__DRIimage *(*DupImage)
			(__DRIimage *psImage,
			 void *pvLoaderPrivate);

		bool (*ValidateImageUsage)
			(__DRIimage *psImage,
			 unsigned int uUse);

		__DRIimage *(*CreateImageFromNames)
			(struct DRISUPScreen *psDRISUPScreen,
			 int iWidth,
			 int iHeight,
			 int iFourCC,
			 int *piNames,
			 int iNumNames,
			 int *piStrides,
			 int *piOffsets,
			 void *pvLoaderPrivate);
			 __DRIimage *(*FromPlanar)(__DRIimage *psImage,
			 int iPlane,
			 void *pvLoaderPrivate);

		__DRIimage *(*CreateImageFromTexture)
			(struct DRISUPContext *psDRISUPContext,
			 int iTarget,
			 unsigned int uTexture,
			 int iDepth,
			 int iLevel,
			 unsigned int *puError,
			 void *pvLoaderPrivate);

		__DRIimage *(*CreateImageFromFDs)
			(struct DRISUPScreen *psDRISUPcreen,
			 int iWidth,
			 int iHeight,
			 int iFourCC,
			 int *piFDs,
			 int iNumFDs,
			 int *piStrides,
			 int *piOffsets,
			 void *pvLoaderPrivate);

		__DRIimage *(*CreateImageFromDMABufs)
			(struct DRISUPScreen *psDRISUPScreen,
			 int iWidth,
			 int iHeight,
			 int iFourCC,
			 int *piFDs,
			 int iNumFDs,
			 int *piStrides,
			 int *piOffsets,
			 unsigned int uColorSpace,
			 unsigned int uSampleRange,
			 unsigned int uHorizSiting,
			 unsigned int uVertSiting,
			 unsigned int *puError,
			 void *pvLoaderPrivate);

		int (*GetImageCapabilities)
			(struct DRISUPScreen *psDRISUPScreen);

		void (*BlitImage)
			(struct DRISUPContext *psDRISUPContext,
			 __DRIimage *psDst,
			 __DRIimage *psSrc,
			 int iDstX0,
			 int iDstY0,
			 int iDstWidth,
			 int iDstHeight,
			 int iSrcX0, int
			 iSrcY0,
			 int iSrcWidth,
			 int iSrcHeight,
			 int iFlushFlag);

		void *(*MapImage)
			(struct DRISUPContext *psDRISUPContext,
			 __DRIimage *psImage,
			 int iX0,
			 int iY0,
			 int iWidth,
			 int iHeight,
			 unsigned int uFlags,
			 int *piStride,
			 void **ppvData);

		void (*UnmapImage)
			(struct DRISUPContext *psDRISUPContext,
			 __DRIimage *psImage,
			 void *pvData);

		__DRIimage *(*CreateImageWithModifiers)
			(struct DRISUPScreen *psDRISUPScreen,
			 int iWidth,
			 int iHeight,
			 int iFourCC,
			 const uint64_t *puModifiers,
			 const unsigned int uModifierCount,
			 void *pvLoaderPrivate);

		__DRIimage *(*CreateImageFromDMABufs2)
			(struct DRISUPScreen *psDRISUPScreen,
			 int iWidth,
			 int iHeight,
			 int iFourCC,
			 uint64_t uModifier,
			 int *piFDs,
			 int iNumFDs,
			 int *piStrides,
			 int *piOffsets,
			 unsigned int uColorSpace,
			 unsigned int uSampleRange,
			 unsigned int uHorizSiting,
			 unsigned int uVertSiting,
			 unsigned int *puError,
			 void *pvLoaderPrivate);

		bool (*QueryDMABufFormats)
			(struct DRISUPScreen *psDRISUPScreen,
			 int iMax,
			 int *piFormats,
			 int *piCount);

		bool (*QueryDMABufModifiers)
			(struct DRISUPScreen *psDRISUPScreen,
			 int iFourCC,
			 int iMax,
			 uint64_t *puModifiers,
			 unsigned int *piExternalOnly,
			 int *piCount);

		bool (*QueryDMABufFormatModifierAttribs)
			(struct DRISUPScreen *psDRISUPScreen,
			 uint32_t uFourcc,
			 uint64_t uModifier,
			 int iAttrib,
			 uint64_t *puValue);

		__DRIimage *(*CreateImageFromRenderBuffer2)
			(struct DRISUPContext *psDRISUPContext,
			 int iRenderBuffer,
			 void *pvLoaderPrivate,
			 unsigned int *puError);

		__DRIimage *(*CreateImageFromBuffer)
			(struct DRISUPContext *psDRISUPContext,
			 int iTarget,
			 void *pvBuffer,
			 unsigned int *puError,
			 void *pvLoaderPrivate);

		/* Functions to support the DRI Renderer Query extension */
		int (*QueryRendererInteger)
			(struct DRISUPScreen *psDRISUPScreen,
			 int iAttribute,
			 unsigned int *puValue);

		int (*QueryRendererString)
			(struct DRISUPScreen *psDRISUPScreen,
			 int iAttribute,
			 const char **ppszValue);

		/* Functions to support the DRI Fence extension */
		void *(*CreateFence)
			(struct DRISUPContext *psDRISUPContext);

		void (*DestroyFence)
			(struct DRISUPScreen *psDRISUPScreen,
			 void *pvFence);

		bool (*ClientWaitSync)
			(struct DRISUPContext *psDRISUPContext,
			 void *pvFence,
			 unsigned int uFlags,
			 uint64_t uTimeout);

		void (*ServerWaitSync)
			(struct DRISUPContext *psDRISUPContext,
			 void *pvFence,
			 unsigned int uFlags);

		unsigned int (*GetFenceCapabilities)
			(struct DRISUPScreen *psDRISUPScreen);

		void *(*CreateFenceFD)
			(struct DRISUPContext *psDRISUPContext,
			 int iFD);

		int (*GetFenceFD)
			(struct DRISUPScreen *psDRISUPScreen,
			 void *pvFence);

		unsigned int (*GetNumAPIProcs)
			(struct DRISUPScreen *psDRISUPScreen,
			 PVRDRIAPIType eAPI);

		const char *(*GetAPIProcName)
			(struct DRISUPScreen *psDRISUPScreen,
			 PVRDRIAPIType eAPI,
			 unsigned int uIndex);

		void *(*GetAPIProcAddress)
			(struct DRISUPScreen *psDRISUPScreen,
			 PVRDRIAPIType eAPI,
			 unsigned int uIndex);

		void (*SetDamageRegion)
			(struct DRISUPDrawable *psDRISUPDrawable,
			 unsigned int uNRects,
			 int *piRects);
	} v0;
	/* The v1 interface is an extension of v0, so v0 is required as well */
	struct {
		void *(*GetFenceFromCLEvent)
			(struct DRISUPScreen *psDRISUPScreen,
			 intptr_t iCLEvent);
	} v1;
	/* The v2 interface is an extension of v1, so v1 is required as well */
	struct {
		int (*GetAPIVersion)
			(struct DRISUPScreen *psDRISUPScreen,
			 PVRDRIAPIType eAPI);
	} v2;
	/*
	 * The v3 interface has no additional entry points. It indicates that
	 * OpenCL event based fences are available, provided the DDK is built
	 * with OpenCL support.
	 */
};

/*
 * Buffer mask values for the GetBuffers callback. These are the same as
 * their enum __DRIimageBufferMask counterparts.
 */
#define PVRDRI_IMAGE_BUFFER_BACK	(1U << 0)
#define PVRDRI_IMAGE_BUFFER_FRONT	(1U << 1)
#define PVRDRI_IMAGE_BUFFER_PREV	(1U << 31)

struct PVRDRIImageList {
	uint32_t uImageMask;
	__DRIimage *psBack;
	__DRIimage *psFront;
	__DRIimage *psPrev;
};

/* The loader capabilities defines match their dri_loader_cap counterparts */
#define	PVRDRI_LOADER_CAP_RGBA_ORDERING	0

/*
 * PVR DRI Support callback interface V2.
 * This structure may change over time, as older interfaces become obsolete.
 * For example, the v0 interface may be removed if superseded by newer
 * interfaces.
 */
struct PVRDRICallbacksV2
{
	struct {
		bool (*RegisterSupportInterface)
			(const void *psInterface,
			 unsigned int uVersion,
			 unsigned int uMinVersion);

		int (*GetBuffers)
			(struct __DRIdrawableRec *psDRIDrawable,
			 unsigned int uFourCC,
			 uint32_t *puStamp,
			 void *pvLoaderPrivate,
			 uint32_t uBufferMask,
			 struct PVRDRIImageList *psImageList);

		bool (*CreateConfigs)
			(struct __DRIconfigRec ***pppsConfigs,
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
			 uint32_t uMaxPbufferHeight);

		struct __DRIconfigRec **(*ConcatConfigs)
			(struct __DRIscreenRec *psDRIScreen,
			 struct __DRIconfigRec **ppsConfigA,
			 struct __DRIconfigRec **ppsConfigB);

		bool (*ConfigQuery)
			(const PVRDRIConfig *psConfig,
			 PVRDRIConfigAttrib eConfigAttrib,
			 unsigned int *puValueOut);

		__DRIimage *(*LookupEGLImage)
			(struct __DRIscreenRec *psDRIScreen,
			 void *pvImage,
			 void *pvLoaderPrivate);

		unsigned int (*GetCapability)
			(struct __DRIscreenRec *psDRIScreen,
			 unsigned int uCapability);
	} v0;
	/* The v1 interface is an extension of v0, so v0 is required as well */
	struct {
		void (*FlushFrontBuffer)
			(struct __DRIdrawableRec *psDRIDrawable,
			 void *pvLoaderPrivate);
	} v1;
	/* The v2 interface is an extension of v1, so v1 is required as well */
	struct {
		int (*GetDisplayFD)
			(struct __DRIscreenRec *psDRIScreen,
			 void *pvLoaderPrivate);
	} v2;
};

#endif /* defined(__PVRDRIIFCE_H__) */
