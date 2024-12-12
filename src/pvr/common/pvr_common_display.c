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

#include "pvr/common/pvr_common_display.h"

#define PVR_DRM_MINOR_PRIMARY_START 0
#define PVR_DRM_MINOR_PRIMARY_END   63

#define PVR_DRM_MINOR_RENDER_START 128
#define PVR_DRM_MINOR_RENDER_END   191

static bool
pvr_common_driver_name_match(int fd, const char *driver_name)
{
   drmVersionPtr version;
   bool match = false;

   version = drmGetVersion(fd);
   if (version) {
      match = !strcmp(version->name, driver_name);
      drmFreeVersion(version);
   }

   return match;
}

static int
pvr_common_open_drm_minor(const int nminor)
{
   char path[PATH_MAX];
   const char *dev_name;

   if (nminor >= PVR_DRM_MINOR_PRIMARY_START &&
       nminor <= PVR_DRM_MINOR_PRIMARY_END)
      dev_name = DRM_DEV_NAME;
   else if (nminor >= PVR_DRM_MINOR_RENDER_START &&
            nminor <= PVR_DRM_MINOR_RENDER_END)
      dev_name = DRM_RENDER_DEV_NAME;
   else
      return -1;

   snprintf(path, sizeof(path), dev_name, DRM_DIR_NAME, nminor);

   return open(path, O_RDWR | O_CLOEXEC, 0);
}

static bool
pvr_common_get_sys_dev_char_path(char *path, size_t path_size,
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
pvr_common_read_link(char *result, size_t result_size, const char *link)
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
pvr_common_string_match_len(const char *pa, const char *pb)
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

static int
pvr_common_open_nearest_display_range(const char *match_link,
                                      const char *match_name,
                                      const unsigned int nmajor,
                                      const int first_minor,
                                      const int last_minor)
{
   int fd = -1;
   size_t best_match = 0;
   char dev_path[PATH_MAX];
   int i;

   for (i = first_minor; i <= last_minor; i++) {
      char dev_link[PATH_MAX];
      size_t match;
      int ret;

      ret = pvr_common_open_drm_minor(i);
      if (ret == -1)
         continue;

      if (!pvr_common_driver_name_match(ret, match_name)) {
         close(ret);
         continue;
      }

      if (!pvr_common_get_sys_dev_char_path(dev_path, sizeof(dev_path),
                                            nmajor, i)) {
         close(ret);
         continue;
      }

      if (!pvr_common_read_link(dev_link, sizeof(dev_link), dev_path)) {
         close(ret);
         continue;
      }

      match = pvr_common_string_match_len(dev_link, match_link);

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

/* Attempt to find the nearest display, in terms of bus topology, to the DRM
 * device represented by iMatchFd.
 *
 * For example, a DRM display device, /dev/dri/renderD129, has major number 226,
 * and minor number 129.
 *
 * In sysfs, this might have the following symbolic link:
 * /sys/dev/char/226:129 ->
 * ../../devices/pci0000:00/0000:00:01.0/drm/renderD129
 *
 * To find the "nearest" GPU, we search for a device node with the best
 * matching symbolic link value in /sys/dev/char. For the current example,
 * the best match might be /dev/dri/card2, which has major number 226,
 * and minor number 2:
 * /sys/dev/char/226:2 ->
 * ../../devices/pci0000:00/0000:00:01.0/drm/card2
 *
 * The symlink value for this node is a better match than another device
 * node on the system, /dev/dri/card3, which has major number 226, and
 * minor number 3:
 *
 * /sys/dev/char/226:3 ->
 * ../../devices/pci0000:00/0000:00:02.0/drm/card3
 *
 * card2 is the better match because the value of its symlink has a
 * longer common prefix with renderD129 than card3 does.
 */
static int
pvr_common_open_nearest_display(int match_fd, const char *match_name)
{
   int fd;
   char dev_path[PATH_MAX];
   char match_link[PATH_MAX];
   struct stat st;
   unsigned int nmajor, nminor;

   if (fstat(match_fd, &st) == -1) {
      debug_printf("%s: couldn't stat the FD to match (errno=%d)",
                   __func__, errno);
      return -1;
   }

   nmajor = major(st.st_rdev);
   nminor = minor(st.st_rdev);

   if (!pvr_common_get_sys_dev_char_path(dev_path, sizeof(dev_path),
                                         nmajor, nminor))
      return -1;

   if (!pvr_common_read_link(match_link, sizeof(match_link), dev_path))
      return -1;

   fd = pvr_common_open_nearest_display_range(match_link, match_name, nmajor,
                                              PVR_DRM_MINOR_RENDER_START,
                                              PVR_DRM_MINOR_RENDER_END);
   if (fd == -1)
      fd = pvr_common_open_nearest_display_range(match_link, match_name, nmajor,
                                                 PVR_DRM_MINOR_PRIMARY_START,
                                                 PVR_DRM_MINOR_PRIMARY_END);
   return fd;
}

int
pvr_common_query_compatible_display_device_fd(int match_fd,
                                              const char *match_name)
{
   if (!match_name)
      return -1;
   else if (pvr_common_driver_name_match(match_fd, match_name))
      return os_dupfd_cloexec(match_fd);
   else if (pvr_common_driver_name_match(match_fd, "pvr"))
      return pvr_common_open_nearest_display(match_fd, match_name);
   else
      return -1;
}
