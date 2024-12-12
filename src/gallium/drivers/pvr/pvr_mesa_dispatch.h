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

#ifndef PVR_MESA_DISPATCH_H
#define PVR_MESA_DISPATCH_H

#include "pvr/ddk/pvr_dri_support.h"

struct pvr_screen;
struct pvr_context;

void pvrdri_free_dispatch_tables(struct pvr_screen *screen);
bool pvrdri_create_dispatch_table(struct pvr_screen *screen,
                                  PVRDRIAPIType api);
void pvrdri_set_null_dispatch_table(void);
void pvrdri_set_dispatch_table(struct pvr_context *context);

void pvrdri_set_null_current_context(void);
void pvrdri_set_current_context(struct pvr_context *context);
struct pvr_context *pvrdri_get_current_context(void);

#endif /* PVR_MESA_DISPATCH_H */
