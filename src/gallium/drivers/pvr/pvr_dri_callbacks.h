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

#ifndef PVR_DRI_CALLBACKS_H
#define PVR_DRI_CALLBACKS_H

#include "pvr/ddk/dri_support.h"

struct pvr_config {
   bool have_mesa_format[PVRDRI_MESA_FORMAT_MAX + 1];
   uint8_t *depth_bits;
   uint8_t *stencil_bits;
   unsigned int num_depth_stencil_bits;
   unsigned int *db_modes;
   unsigned int num_db_modes;
   uint8_t *msaa_samples;
   unsigned int num_msaa_modes;
   bool enable_accum;
   bool color_depth_match;
   bool mutable_render_buffer;
   uint32_t max_pbuffer_width;
   uint32_t max_pbuffer_height;
};

int
MODSUPGetBuffers(struct __DRIdrawableRec *psDRIDrawable,
                 unsigned int uFourCC,
                 uint32_t *puStamp,
                 void *pvLoaderPrivate,
                 uint32_t uBufferMask,
                 struct PVRDRIImageList *psImageList);

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
                    uint32_t uMaxPbufferHeight);

struct __DRIconfigRec **
MODSUPConcatConfigs(struct __DRIscreenRec *psDRIScreen,
                    struct __DRIconfigRec **ppsConfigA,
                    struct __DRIconfigRec **ppsConfigB);

bool
MODSUPConfigQuery(const PVRDRIConfig *psConfig,
                  PVRDRIConfigAttrib eConfigAttrib,
                  unsigned int *puValueOut);

__DRIimage *
MODSUPLookupEGLImage(struct __DRIscreenRec *psDRIScreen,
                     void *pvImage,
                     void *pvLoaderPrivate);

unsigned int
MODSUPGetCapability(struct __DRIscreenRec *psDRIScreen,
                    unsigned int uCapability);

void
MODSUPFlushFrontBuffer(struct __DRIdrawableRec *psDRIDrawable,
                                   void *pvLoaderPrivate);

int
MODSUPGetDisplayFD(struct __DRIscreenRec *psDRIScreen,
                   void *pvLoaderPrivate);

void *
MODSUPDrawableGetReferenceHandle(struct __DRIdrawableRec *psDRIDrawable);

void
MODSUPDrawableAddReference(void *pvReferenceHandle);

void
MODSUPDrawableRemoveReference(void *pvReferenceHandle);

void
MODSUPDestroyLoaderImageState(const struct __DRIscreenRec *psDRIScreen,
                              void *pvLoaderPrivate);

bool
MODSUPIsExplicitDriverLoad(const struct __DRIscreenRec *psDRIScreen);
#endif /* PVR_DRI_CALLBACKS_H */
