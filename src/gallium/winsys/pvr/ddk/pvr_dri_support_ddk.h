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

#if !defined(__PVR_DRI_SUPPORT_DDK_H__)
#define __PVR_DRI_SUPPORT_DDK_H__

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
#define PVRDRI_CONTEXT_PRIORITY_LOW		0
#define PVRDRI_CONTEXT_PRIORITY_MEDIUM		1
#define PVRDRI_CONTEXT_PRIORITY_HIGH		2
#define PVRDRI_CONTEXT_PRIORITY_REALTIME	3

/* The image error flags match their __DRI_IMAGE_ERROR counterparts */
#define PVRDRI_IMAGE_ERROR_SUCCESS		0
#define PVRDRI_IMAGE_ERROR_BAD_ALLOC		1
#define PVRDRI_IMAGE_ERROR_BAD_MATCH		2
#define PVRDRI_IMAGE_ERROR_BAD_PARAMETER	3
#define PVRDRI_IMAGE_ERROR_BAD_ACCESS		4

/* The buffer flags match their __DRI_IMAGE_USE counterparts */
#define PVRDRI_BUFFER_USE_SHARE		0x0001
#define PVRDRI_BUFFER_USE_SCANOUT	0x0002
#define PVRDRI_BUFFER_USE_CURSOR	0x0004
#define PVRDRI_BUFFER_USE_LINEAR	0x0008

#define PVRDRI_BUFFER_USE_BACKBUFFER	0x0010
#define PVRDRI_BUFFER_USE_PROTECTED	0x0020
#define PVRDRI_BUFFER_USE_PRIME_BUFFER	0x0040

/* The blit flags match their DRI counterparts */
#define PVRDRI_BLIT_FLAG_FLUSH		0x0001
#define PVRDRI_BLIT_FLAG_FINISH		0x0002

/*
 * Flags for createImageFromDmaBufs3 and createImageFromFds2.
 * These match their DRI counterparts.
 */
#define PVRDRI_IMAGE_PROTECTED_CONTENT_FLAG 0x00000001
#define PVRDRI_IMAGE_PRIME_LINEAR_BUFFER    0x00000002

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

/*
 * Capabilities that might be returned by PVRDRIInterface.GetFenceCapabilities.
 * These match their _DRI_FENCE_CAP counterparts.
 */
#define	PVRDRI_FENCE_CAP_NATIVE_FD 0x1

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

#define PVRDRI_RENDERER_HAS_PROTECTED_SURFACE		0x000e
#define PVRDRI_RENDERER_PREFER_BACK_BUFFER_REUSE	0x000f
#define PVRDRI_RENDERER_HAS_NO_ERROR_CONTEXT		0x0010

#define PVRDRI_RENDERER_HAS_PROTECTED_CONTENT		0x0020

#define PVRDRI_RENDERER_OPENGL_ES2_CONTEXT_CLIENT_VERSION_IMG 0x7001

#define PVRDRI_RENDERER_INVALID_PARAM_IMG		0xffff

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

/* The CL defines match their EGL_CL counterparts */
#define PVRDRI_CL_IMAGE_IMG			0x6010

/* The swap attribute defines match their __DRI_ATTRIB_SWAP counterparts */
#define PVRDRI_ATTRIB_SWAP_NONE			0x0000
#define PVRDRI_ATTRIB_SWAP_EXCHANGE		0x8061
#define PVRDRI_ATTRIB_SWAP_COPY			0x8062
#define PVRDRI_ATTRIB_SWAP_UNDEFINED		0x8063

/* The image capability defines match their __DRI_IMAGE_CAP counterparts */
#define	PVRDRI_IMAGE_CAP_GLOBAL_NAMES	0x0001
#define	PVRDRI_IMAGE_CAP_PRIME_IMPORT	0x2000
#define	PVRDRI_IMAGE_CAP_PRIME_EXPORT	0x4000

#define	PVRDRI_LOADER_CAP_RGBA_ORDERING	0
#define	PVRDRI_LOADER_CAP_YUV_SURFACE_IMG 0x7001

/*
 * Buffer mask values for the GetBuffers callback. These are the same as
 * their enum __DRIimageBufferMask counterparts.
 */
#define PVRDRI_IMAGE_BUFFER_BACK	(1U << 0)
#define PVRDRI_IMAGE_BUFFER_FRONT	(1U << 1)
#define PVRDRI_IMAGE_BUFFER_PREV	(1U << 31)

#endif /* defined(__PVR_DRI_SUPPORT_DDK_H__) */
