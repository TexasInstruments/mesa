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

#include <string.h>
#include <xf86drm.h>

#include "util/macros.h"

#include "pvr_ddk_private.h"

bool
pvr_ddk_is_driver_compat_name(const char *name)
{
   const char *compat_drivers[] = {
      "pvr",
      "mediatek",
   };

   for (unsigned int i = 0; i < ARRAY_SIZE(compat_drivers); i++)
      if (!strcmp(name, compat_drivers[i]))
         return true;

   return false;
}

bool
pvr_ddk_is_driver_compat_fd(int fd)
{
   drmVersionPtr version;
   bool compat = false;

   version = drmGetVersion(fd);
   if (version) {
      compat = pvr_ddk_is_driver_compat_name(version->name);
      drmFreeVersion(version);
   }

   return compat;
}
