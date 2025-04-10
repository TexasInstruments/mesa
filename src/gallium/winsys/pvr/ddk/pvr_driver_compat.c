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

#include <linux/limits.h>

#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>

#include "util/macros.h"
#include "util/os_file.h"
#include "util/u_debug.h"

#include "pvr_ddk_private.h"
#include "pvr_ddk_public.h"

#define PVR_DRM_MINOR_RENDER_START 128
#define PVR_DRM_MINOR_RENDER_END   191

#define PVR_STRINGIFY_HELPER(x) # x
#define PVR_STRINGIFY(x) PVR_STRINGIFY_HELPER(x)

#if defined(GALLIUM_PVR_ALIAS)
#define GALLIUM_PVR_ALIAS_STRING PVR_STRINGIFY(GALLIUM_PVR_ALIAS)
#endif

static bool
pvr_driver_is_pvr_name(const char *name)
{
   return !strcmp(name, "pvr");
}

bool
pvr_ddk_is_driver_compat_name(const char *name)
{
   const char *compat_drivers[] = {
      "mediatek",
#if defined(GALLIUM_PVR_ALIAS_STRING)
      GALLIUM_PVR_ALIAS_STRING
#endif
   };

   if (pvr_driver_is_pvr_name(name))
      return true;

   for (unsigned int i = 0; i < ARRAY_SIZE(compat_drivers); i++)
      if (!strcmp(name, compat_drivers[i]))
         return true;

   return false;
}

static bool
pvr_driver_is_pvr_fd(int fd)
{
   drmVersionPtr version;
   bool compat = false;

   version = drmGetVersion(fd);
   if (version) {
      compat = pvr_driver_is_pvr_name(version->name);
      drmFreeVersion(version);
   }

   return compat;
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

static int
pvr_open_render_minor(const int nminor)
{
   char path[PATH_MAX];

   assert(nminor >= PVR_DRM_MINOR_RENDER_START &&
          nminor <= PVR_DRM_MINOR_RENDER_END);

   snprintf(path, sizeof(path), DRM_RENDER_DEV_NAME, DRM_DIR_NAME, nminor);

   return open(path, O_RDWR | O_CLOEXEC, 0);
}

static int
pvr_open_pvr_dev_minor(const int nminor)
{
   int fd = pvr_open_render_minor(nminor);

   if (fd != -1 && !pvr_driver_is_pvr_fd(fd)) {
      close(fd);
      fd = -1;

      errno = ENOENT;
   }

   return fd;
}

static bool
pvr_get_sys_dev_char_path(char *path, size_t path_size,
                          unsigned int nmajor, unsigned int nminor)
{
   int res = snprintf(path, path_size,
      "/sys/dev/char/%u:%u", nmajor, nminor);

   if (res < 0) {
      debug_printf("%s: snprintf failed", __func__);
      return false;
   } else if ((size_t)res >= path_size) {
      debug_printf("%s: snprintf result was truncated", __func__);
      return false;
   }

   return true;
}

static bool
pvr_read_link(char *result, size_t result_size, const char *link)
{
   ssize_t sres = readlink(link, result, result_size);

   if (sres == -1) {
      debug_printf("%s: readlink failed (errno=%d)", __func__, errno);
      return false;
   } else if ((size_t)sres >= result_size) {
      debug_printf( "%s: readlink result may have been truncated", __func__);
      return false;
   }

   return true;
}

static ssize_t
pvr_string_match_len(const char *pa, const char *pb)
{
   ssize_t i = 0;

   if (pa && pb) {
      for (;;i++) {
         char ca = pa[i];
         char cb = pb[i];

         if ((ca != cb) || !cb)
            break;
      }
   }

   return i;
}

/* Attempt to find the nearest GPU, in terms of bus topology, to the DRM
 * device represented by iMatchFd.
 *
 * For example, a DRM display device, /dev/dri/card2, has major number 226,
 * and minor number 2.
 *
 * In sysfs, this might have the following symbolic link:
 * /sys/dev/char/226:2 ->
 * ../../devices/pci0000:00/0000:00:01.0/drm/card2
 *
 * To find the "nearest" GPU, we search for a render node with the best
 * matching symbolic link value in /sys/dev/char. For the current example,
 * the best match might be /dev/dri/renderD129, which has major number 226,
 * and minor number 129:
 * /sys/dev/char/226:129 ->
 * ../../devices/pci0000:00/0000:00:01.0/drm/renderD129
 *
 * The symlink value for this node is a better match than the other render
 * node on the system, /dev/dri/renderD128, which has major number 226, and
 * minor number 128:
 *
 * /sys/dev/char/226:128 ->
 * ../../devices/pci0000:00/0000:00:02.0/drm/renderD128
 *
 * RenderD129 is the better match because the value of its symlink has a
 * longer common prefix with card2 than renderD128 does.
 */
static int
pvr_open_nearest_render(int match_fd)
{
   int fd = -1;
   size_t best_match = 0;
   char dev_path[PATH_MAX];
   char match_link[PATH_MAX];
   struct stat st;
   unsigned int nmajor, nminor;
   int i;

   if (fstat(match_fd, &st) == -1) {
      debug_printf("%s: couldn't stat the FD to match (errno=%d)",
                   __func__, errno);
      return -1;
   }

   nmajor = major(st.st_rdev);
   nminor = minor(st.st_rdev);

   if (!pvr_get_sys_dev_char_path(dev_path, sizeof(dev_path), nmajor, nminor))
      return -1;

   if (!pvr_read_link(match_link, sizeof(match_link), dev_path))
      return -1;

   for (i = PVR_DRM_MINOR_RENDER_START; i <= PVR_DRM_MINOR_RENDER_END; i++) {
      char dev_link[PATH_MAX];
      size_t match;
      int ret;

      ret = pvr_open_pvr_dev_minor(i);
      if (ret == -1)
         continue;

      if (!pvr_get_sys_dev_char_path(dev_path, sizeof(dev_path), nmajor, i)) {
         close(ret);
         continue;
      }

      if (!pvr_read_link(dev_link, sizeof(dev_link), dev_path)) {
         close(ret);
         continue;
      }

      match = pvr_string_match_len(dev_link, match_link);

      if (match > best_match || fd == -1) {
         if (fd != -1)
            close(fd);

         best_match = match;
         fd = ret;
      } else {
         close(ret);
      }
   }

   return fd;

}

int
pvr_ddk_query_compatible_render_only_device_fd(int match_fd)
{
   if (pvr_ddk_is_driver_compat_fd(match_fd))
      return pvr_open_nearest_render(match_fd);
   else
      return -1;
}
