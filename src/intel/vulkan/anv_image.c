/*
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "drm-uapi/drm_fourcc.h"

#include "anv_private.h"
#include "util/u_debug.h"
#include "vk_util.h"
#include "util/u_math.h"

#include "vk_format.h"

#define ANV_OFFSET_IMPLICIT UINT64_MAX

static const enum isl_surf_dim
vk_to_isl_surf_dim[] = {
   [VK_IMAGE_TYPE_1D] = ISL_SURF_DIM_1D,
   [VK_IMAGE_TYPE_2D] = ISL_SURF_DIM_2D,
   [VK_IMAGE_TYPE_3D] = ISL_SURF_DIM_3D,
};

static uint64_t MUST_CHECK UNUSED
memory_range_end(struct anv_image_memory_range memory_range)
{
   assert(anv_is_aligned(memory_range.offset, memory_range.alignment));
   return memory_range.offset + memory_range.size;
}

/**
 * Get binding for VkImagePlaneMemoryRequirementsInfo,
 * VkBindImagePlaneMemoryInfo and VkDeviceImageMemoryRequirements.
 */
static struct anv_image_binding *
image_aspect_to_binding(struct anv_image *image, VkImageAspectFlags aspect)
{
   uint32_t plane = 0;

   assert(image->disjoint);

   if (image->vk.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      /* Spec requires special aspects for modifier images. */
      assert(aspect == VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT ||
             aspect == VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT ||
             aspect == VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT ||
             aspect == VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT);

      /* We don't advertise DISJOINT for modifiers with aux, and therefore we
       * don't handle queries of the modifier's "aux plane" here.
       */
      assert(!isl_drm_modifier_has_aux(image->vk.drm_format_mod));

      switch(aspect) {
         case VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT: plane = 0; break;
         case VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT: plane = 1; break;
         case VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT: plane = 2; break;
         case VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT: plane = 3; break;
      }
   } else {
      plane = anv_image_aspect_to_plane(image, aspect);
   }

   return &image->bindings[ANV_IMAGE_MEMORY_BINDING_PLANE_0 + plane];
}

/**
 * Extend the memory binding's range by appending a new memory range with `size`
 * and `alignment` at `offset`. Return the appended range.
 *
 * Offset is ignored if ANV_OFFSET_IMPLICIT.
 *
 * The given binding must not be ANV_IMAGE_MEMORY_BINDING_MAIN. The function
 * converts to MAIN as needed.
 */
static VkResult MUST_CHECK
image_binding_grow(const struct anv_device *device,
                   struct anv_image *image,
                   enum anv_image_memory_binding binding,
                   uint64_t offset,
                   uint64_t size,
                   uint32_t alignment,
                   struct anv_image_memory_range *out_range)
{
   /* We overwrite 'offset' but need to remember if it was implicit. */
   const bool has_implicit_offset = (offset == ANV_OFFSET_IMPLICIT);

   assert(size > 0);
   assert(util_is_power_of_two_or_zero(alignment));

   switch (binding) {
   case ANV_IMAGE_MEMORY_BINDING_MAIN:
      /* The caller must not pre-translate BINDING_PLANE_i to BINDING_MAIN. */
      unreachable("ANV_IMAGE_MEMORY_BINDING_MAIN");
   case ANV_IMAGE_MEMORY_BINDING_PLANE_0:
   case ANV_IMAGE_MEMORY_BINDING_PLANE_1:
   case ANV_IMAGE_MEMORY_BINDING_PLANE_2:
      if (!image->disjoint)
         binding = ANV_IMAGE_MEMORY_BINDING_MAIN;
      break;
   case ANV_IMAGE_MEMORY_BINDING_PRIVATE:
      assert(offset == ANV_OFFSET_IMPLICIT);
      break;
   case ANV_IMAGE_MEMORY_BINDING_END:
      unreachable("ANV_IMAGE_MEMORY_BINDING_END");
   }

   struct anv_image_memory_range *container =
      &image->bindings[binding].memory_range;

   if (has_implicit_offset) {
      offset = align64(container->offset + container->size, alignment);
   } else {
      /* Offset must be validated because it comes from
       * VkImageDrmFormatModifierExplicitCreateInfoEXT.
       */
      if (unlikely(!anv_is_aligned(offset, alignment))) {
         return vk_errorf(device,
                          VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT,
                          "VkImageDrmFormatModifierExplicitCreateInfoEXT::"
                          "pPlaneLayouts[]::offset is misaligned");
      }
   }

   /* Surfaces can be added out of memory-order. Track the end of each memory
    * plane to update the binding size properly.
    */
   uint64_t memory_range_end;
   if (__builtin_add_overflow(offset, size, &memory_range_end)) {
      if (has_implicit_offset) {
         assert(!"overflow");
         return vk_errorf(device, VK_ERROR_UNKNOWN,
                          "internal error: overflow in %s", __func__);
      } else {
         return vk_errorf(device,
                          VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT,
                          "VkImageDrmFormatModifierExplicitCreateInfoEXT::"
                          "pPlaneLayouts[]::offset is too large");
      }
   }

   container->size = MAX2(container->size, memory_range_end);
   container->alignment = MAX2(container->alignment, alignment);

   *out_range = (struct anv_image_memory_range) {
      .binding = binding,
      .offset = offset,
      .size = size,
      .alignment = alignment,
   };

   return VK_SUCCESS;
}

/**
 * Adjust range 'a' to contain range 'b'.
 *
 * For simplicity's sake, the offset of 'a' must be 0 and remains 0.
 * If 'a' and 'b' target different bindings, then no merge occurs.
 */
static void
memory_range_merge(struct anv_image_memory_range *a,
                   const struct anv_image_memory_range b)
{
   if (b.size == 0)
      return;

   if (a->binding != b.binding)
      return;

   assert(a->offset == 0);
   assert(anv_is_aligned(a->offset, a->alignment));
   assert(anv_is_aligned(b.offset, b.alignment));

   a->alignment = MAX2(a->alignment, b.alignment);
   a->size = MAX2(a->size, b.offset + b.size);
}

static isl_surf_usage_flags_t
choose_isl_surf_usage(VkImageCreateFlags vk_create_flags,
                      VkImageUsageFlags vk_usage,
                      isl_surf_usage_flags_t isl_extra_usage,
                      VkImageAspectFlagBits aspect)
{
   isl_surf_usage_flags_t isl_usage = isl_extra_usage;

   if (vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT)
      isl_usage |= ISL_SURF_USAGE_TEXTURE_BIT;

   if (vk_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
      isl_usage |= ISL_SURF_USAGE_TEXTURE_BIT;

   if (vk_usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
      isl_usage |= ISL_SURF_USAGE_RENDER_TARGET_BIT;

   if (vk_usage & VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR)
      isl_usage |= ISL_SURF_USAGE_CPB_BIT;

   if (vk_usage & VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR ||
       vk_usage & VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR)
      isl_usage |= ISL_SURF_USAGE_VIDEO_DECODE_BIT;

   if (vk_create_flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
      isl_usage |= ISL_SURF_USAGE_CUBE_BIT;

   /* Even if we're only using it for transfer operations, clears to depth and
    * stencil images happen as depth and stencil so they need the right ISL
    * usage bits or else things will fall apart.
    */
   switch (aspect) {
   case VK_IMAGE_ASPECT_DEPTH_BIT:
      isl_usage |= ISL_SURF_USAGE_DEPTH_BIT;
      break;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      isl_usage |= ISL_SURF_USAGE_STENCIL_BIT;
      break;
   case VK_IMAGE_ASPECT_COLOR_BIT:
   case VK_IMAGE_ASPECT_PLANE_0_BIT:
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
      break;
   default:
      unreachable("bad VkImageAspect");
   }

   if (vk_usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
      /* blorp implements transfers by sampling from the source image. */
      isl_usage |= ISL_SURF_USAGE_TEXTURE_BIT;
   }

   if (vk_usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT &&
       aspect == VK_IMAGE_ASPECT_COLOR_BIT) {
      /* blorp implements transfers by rendering into the destination image.
       * Only request this with color images, as we deal with depth/stencil
       * formats differently. */
      isl_usage |= ISL_SURF_USAGE_RENDER_TARGET_BIT;
   }

   return isl_usage;
}

static isl_tiling_flags_t
choose_isl_tiling_flags(const struct intel_device_info *devinfo,
                        const struct anv_image_create_info *anv_info,
                        const struct isl_drm_modifier_info *isl_mod_info,
                        bool legacy_scanout)
{
   const VkImageCreateInfo *base_info = anv_info->vk_info;
   isl_tiling_flags_t flags = 0;

   assert((isl_mod_info != NULL) ==
          (base_info->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT));

   switch (base_info->tiling) {
   default:
      unreachable("bad VkImageTiling");
   case VK_IMAGE_TILING_OPTIMAL:
      flags = ISL_TILING_ANY_MASK;
      break;
   case VK_IMAGE_TILING_LINEAR:
      flags = ISL_TILING_LINEAR_BIT;
      break;
   case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT:
      flags = 1 << isl_mod_info->tiling;
   }

   if (anv_info->isl_tiling_flags) {
      assert(isl_mod_info == NULL);
      flags &= anv_info->isl_tiling_flags;
   }

   if (legacy_scanout) {
      isl_tiling_flags_t legacy_mask = ISL_TILING_LINEAR_BIT;
      if (devinfo->has_tiling_uapi)
         legacy_mask |= ISL_TILING_X_BIT;
      flags &= legacy_mask;
   }

   assert(flags);

   return flags;
}

/**
 * Add the surface to the binding at the given offset.
 *
 * \see image_binding_grow()
 */
static VkResult MUST_CHECK
add_surface(struct anv_device *device,
            struct anv_image *image,
            struct anv_surface *surf,
            enum anv_image_memory_binding binding,
            uint64_t offset)
{
   /* isl surface must be initialized */
   assert(surf->isl.size_B > 0);

   return image_binding_grow(device, image, binding, offset,
                             surf->isl.size_B,
                             surf->isl.alignment_B,
                             &surf->memory_range);
}

static bool
can_fast_clear_with_non_zero_color(const struct intel_device_info *devinfo,
                                   const struct anv_image *image,
                                   uint32_t plane,
                                   const VkImageFormatListCreateInfo *fmt_list)
{
   /* If we don't have an AUX surface where fast clears apply, we can return
    * early.
    */
   if (!isl_aux_usage_has_fast_clears(image->planes[plane].aux_usage))
      return false;

   /* On TGL (< C0), if a block of fragment shader outputs match the surface's
    * clear color, the HW may convert them to fast-clears (see HSD 1607794140).
    * This can lead to rendering corruptions if not handled properly. We
    * restrict the clear color to zero to avoid issues that can occur with:
    *     - Texture view rendering (including blorp_copy calls)
    *     - Images with multiple levels or array layers
    */
   if (image->planes[plane].aux_usage == ISL_AUX_USAGE_FCV_CCS_E)
      return false;

   /* Non mutable image, we can fast clear with any color supported by HW.
    */
   if (!(image->vk.create_flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT))
      return true;

   /* Mutable image with no format list, we have to assume all formats */
   if (!fmt_list || fmt_list->viewFormatCount == 0)
      return false;

   enum isl_format img_format = image->planes[plane].primary_surface.isl.format;

   /* Check bit compatibility for clear color components */
   for (uint32_t i = 0; i < fmt_list->viewFormatCount; i++) {
      if (fmt_list->pViewFormats[i] == VK_FORMAT_UNDEFINED)
         continue;

      struct anv_format_plane view_format_plane =
         anv_get_format_plane(devinfo, fmt_list->pViewFormats[i],
                              plane, image->vk.tiling);

      enum isl_format view_format = view_format_plane.isl_format;

      if (!isl_formats_have_same_bits_per_channel(img_format, view_format))
         return false;
   }

   return true;
}

/**
 * Return true if the storage image could be used with atomics.
 *
 * If the image was created with an explicit format, we check it for typed
 * atomic support.  If MUTABLE_FORMAT_BIT is set, then we check the optional
 * format list, seeing if /any/ of the formats support typed atomics.  If no
 * list is supplied, we fall back to using the bpb, as the application could
 * make an image view with a format that does use atomics.
 */
static bool
storage_image_format_supports_atomic(const struct intel_device_info *devinfo,
                                     VkImageCreateFlags create_flags,
                                     enum isl_format format,
                                     VkImageTiling vk_tiling,
                                     const VkImageFormatListCreateInfo *fmt_list)
{
   if (isl_format_supports_typed_atomics(devinfo, format))
      return true;

   if (!(create_flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT))
      return false;

   if (fmt_list) {
      for (uint32_t i = 0; i < fmt_list->viewFormatCount; i++) {
         if (fmt_list->pViewFormats[i] == VK_FORMAT_UNDEFINED)
            continue;

         enum isl_format view_format =
            anv_get_isl_format(devinfo, fmt_list->pViewFormats[i],
                               VK_IMAGE_ASPECT_COLOR_BIT, vk_tiling);

         if (isl_format_supports_typed_atomics(devinfo, view_format))
            return true;
      }

      return false;
   }

   /* No explicit format list.  Any 16/32/64bpp format could be used with atomics. */
   unsigned bpb = isl_format_get_layout(format)->bpb;
   return bpb == 16 || bpb == 32 || bpb == 64;
}

static enum isl_format
anv_get_isl_format_with_usage(const struct intel_device_info *devinfo,
                              VkFormat vk_format,
                              VkImageAspectFlagBits vk_aspect,
                              VkImageUsageFlags vk_usage,
                              VkImageTiling vk_tiling)
{
   assert(util_bitcount(vk_usage) == 1);
   struct anv_format_plane format =
      anv_get_format_aspect(devinfo, vk_format, vk_aspect,
                            vk_tiling);

   return format.isl_format;
}

static bool
formats_ccs_e_compatible(const struct intel_device_info *devinfo,
                         VkImageCreateFlags create_flags,
                         enum isl_format format, VkImageTiling vk_tiling,
                         VkImageUsageFlags vk_usage,
                         const VkImageFormatListCreateInfo *fmt_list)
{
   if (!isl_format_supports_ccs_e(devinfo, format))
      return false;

   /* For images created without MUTABLE_FORMAT_BIT set, we know that they will
    * always be used with the original format. In particular, they will always
    * be used with a format that supports color compression.
    */
   if (!(create_flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT))
      return true;

   if (!fmt_list || fmt_list->viewFormatCount == 0)
      return false;

   for (uint32_t i = 0; i < fmt_list->viewFormatCount; i++) {
      if (fmt_list->pViewFormats[i] == VK_FORMAT_UNDEFINED)
         continue;

      enum isl_format view_format =
         anv_get_isl_format_with_usage(devinfo, fmt_list->pViewFormats[i],
                                       VK_IMAGE_ASPECT_COLOR_BIT, vk_usage,
                                       vk_tiling);

      if (!isl_formats_are_ccs_e_compatible(devinfo, format, view_format))
         return false;
   }

   return true;
}

bool
anv_formats_ccs_e_compatible(const struct intel_device_info *devinfo,
                             VkImageCreateFlags create_flags,
                             VkFormat vk_format, VkImageTiling vk_tiling,
                             VkImageUsageFlags vk_usage,
                             const VkImageFormatListCreateInfo *fmt_list)
{
   enum isl_format format =
      anv_get_isl_format_with_usage(devinfo, vk_format,
                                    VK_IMAGE_ASPECT_COLOR_BIT,
                                    VK_IMAGE_USAGE_SAMPLED_BIT, vk_tiling);

   if (!formats_ccs_e_compatible(devinfo, create_flags, format, vk_tiling,
                                 VK_IMAGE_USAGE_SAMPLED_BIT, fmt_list))
      return false;

   if (vk_usage & VK_IMAGE_USAGE_STORAGE_BIT) {
      if (devinfo->verx10 < 125)
         return false;

      enum isl_format lower_format =
         anv_get_isl_format_with_usage(devinfo, vk_format,
                                       VK_IMAGE_ASPECT_COLOR_BIT,
                                       VK_IMAGE_USAGE_STORAGE_BIT, vk_tiling);

      if (!isl_formats_are_ccs_e_compatible(devinfo, format, lower_format))
         return false;

      if (!formats_ccs_e_compatible(devinfo, create_flags, format, vk_tiling,
                                    VK_IMAGE_USAGE_STORAGE_BIT, fmt_list))
         return false;

      /* Disable compression when surface can be potentially used for atomic
       * operation.
       */
      if (storage_image_format_supports_atomic(devinfo, create_flags, format,
                                               vk_tiling, fmt_list))
         return false;
   }

   return true;
}

/**
 * For color images that have an auxiliary surface, request allocation for an
 * additional buffer that mainly stores fast-clear values. Use of this buffer
 * allows us to access the image's subresources while being aware of their
 * fast-clear values in non-trivial cases (e.g., outside of a render pass in
 * which a fast clear has occurred).
 *
 * In order to avoid having multiple clear colors for a single plane of an
 * image (hence a single RENDER_SURFACE_STATE), we only allow fast-clears on
 * the first slice (level 0, layer 0).  At the time of our testing (Jan 17,
 * 2018), there were no known applications which would benefit from fast-
 * clearing more than just the first slice.
 *
 * The fast clear portion of the image is laid out in the following order:
 *
 *  * 1 or 4 dwords (depending on hardware generation) for the clear color
 *  * 1 dword for the anv_fast_clear_type of the clear color
 *  * On gfx9+, 1 dword per level and layer of the image (3D levels count
 *    multiple layers) in level-major order for compression state.
 *
 * For the purpose of discoverability, the algorithm used to manage
 * compression and fast-clears is described here:
 *
 *  * On a transition from UNDEFINED or PREINITIALIZED to a defined layout,
 *    all of the values in the fast clear portion of the image are initialized
 *    to default values.
 *
 *  * On fast-clear, the clear value is written into surface state and also
 *    into the buffer and the fast clear type is set appropriately.  Both
 *    setting the fast-clear value in the buffer and setting the fast-clear
 *    type happen from the GPU using MI commands.
 *
 *  * Whenever a render or blorp operation is performed with CCS_E, we call
 *    genX(cmd_buffer_mark_image_written) to set the compression state to
 *    true (which is represented by UINT32_MAX).
 *
 *  * On pipeline barrier transitions, the worst-case transition is computed
 *    from the image layouts.  The command streamer inspects the fast clear
 *    type and compression state dwords and constructs a predicate.  The
 *    worst-case resolve is performed with the given predicate and the fast
 *    clear and compression state is set accordingly.
 *
 * See anv_layout_to_aux_usage and anv_layout_to_fast_clear_type functions for
 * details on exactly what is allowed in what layouts.
 *
 * On gfx7-9, we do not have a concept of indirect clear colors in hardware.
 * In order to deal with this, we have to do some clear color management.
 *
 *  * For LOAD_OP_LOAD at the top of a renderpass, we have to copy the clear
 *    value from the buffer into the surface state with MI commands.
 *
 *  * For any blorp operations, we pass the address to the clear value into
 *    blorp and it knows to copy the clear color.
 */
static VkResult MUST_CHECK
add_aux_state_tracking_buffer(struct anv_device *device,
                              struct anv_image *image,
                              uint32_t plane)
{
   assert(image && device);
   assert(image->planes[plane].aux_usage != ISL_AUX_USAGE_NONE &&
          image->vk.aspects & (VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV |
                               VK_IMAGE_ASPECT_DEPTH_BIT));

   const unsigned clear_color_state_size = device->info->ver >= 10 ?
      device->isl_dev.ss.clear_color_state_size :
      device->isl_dev.ss.clear_value_size;

   /* Clear color and fast clear type */
   unsigned state_size = clear_color_state_size + 4;

   /* We only need to track compression on CCS_E surfaces. */
   if (isl_aux_usage_has_ccs_e(image->planes[plane].aux_usage)) {
      if (image->vk.image_type == VK_IMAGE_TYPE_3D) {
         for (uint32_t l = 0; l < image->vk.mip_levels; l++)
            state_size += u_minify(image->vk.extent.depth, l) * 4;
      } else {
         state_size += image->vk.mip_levels * image->vk.array_layers * 4;
      }
   }

   enum anv_image_memory_binding binding =
      ANV_IMAGE_MEMORY_BINDING_PLANE_0 + plane;

   /* If an auxiliary surface is used for an externally-shareable image,
    * we have to hide this from the memory of the image since other
    * processes with access to the memory may not be aware of it or of
    * its current state. So put that auxiliary data into a separate
    * buffer (ANV_IMAGE_MEMORY_BINDING_PRIVATE).
    */
   if (anv_image_is_externally_shared(image)) {
      binding = ANV_IMAGE_MEMORY_BINDING_PRIVATE;
   }

   /* We believe that 256B alignment may be sufficient, but we choose 4K due to
    * lack of testing.  And MI_LOAD/STORE operations require dword-alignment.
    */
   return image_binding_grow(device, image, binding,
                             ANV_OFFSET_IMPLICIT, state_size, 4096,
                             &image->planes[plane].fast_clear_memory_range);
}

/**
 * The return code indicates whether creation of the VkImage should continue
 * or fail, not whether the creation of the aux surface succeeded.  If the aux
 * surface is not required (for example, by neither hardware nor DRM format
 * modifier), then this may return VK_SUCCESS when creation of the aux surface
 * fails.
 *
 * @param offset See add_surface()
 */
static VkResult
add_aux_surface_if_supported(struct anv_device *device,
                             struct anv_image *image,
                             uint32_t plane,
                             struct anv_format_plane plane_format,
                             const VkImageFormatListCreateInfo *fmt_list,
                             uint64_t offset,
                             uint32_t stride,
                             isl_surf_usage_flags_t isl_extra_usage_flags)
{
   VkImageAspectFlags aspect = plane_format.aspect;
   VkResult result;
   bool ok;

   /* The aux surface must not be already added. */
   assert(!anv_surface_is_valid(&image->planes[plane].aux_surface));

   if ((isl_extra_usage_flags & ISL_SURF_USAGE_DISABLE_AUX_BIT))
      return VK_SUCCESS;

   if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
      /* We don't advertise that depth buffers could be used as storage
       * images.
       */
       assert(!(image->vk.usage & VK_IMAGE_USAGE_STORAGE_BIT));

      /* Allow the user to control HiZ enabling. Disable by default on gfx7
       * because resolves are not currently implemented pre-BDW.
       */
      if (!(image->vk.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
         /* It will never be used as an attachment, HiZ is pointless. */
         return VK_SUCCESS;
      }

      if (image->vk.mip_levels > 1) {
         anv_perf_warn(VK_LOG_OBJS(&image->vk.base), "Enable multi-LOD HiZ");
         return VK_SUCCESS;
      }

      if (INTEL_DEBUG(DEBUG_NO_HIZ))
         return VK_SUCCESS;

      ok = isl_surf_get_hiz_surf(&device->isl_dev,
                                 &image->planes[plane].primary_surface.isl,
                                 &image->planes[plane].aux_surface.isl);
      if (!ok)
         return VK_SUCCESS;

      if (!isl_surf_supports_ccs(&device->isl_dev,
                                 &image->planes[plane].primary_surface.isl,
                                 &image->planes[plane].aux_surface.isl)) {
         image->planes[plane].aux_usage = ISL_AUX_USAGE_HIZ;
      } else if (image->vk.usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                                    VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) &&
                 image->vk.samples == 1) {
         /* If it's used as an input attachment or a texture and it's
          * single-sampled (this is a requirement for HiZ+CCS write-through
          * mode), use write-through mode so that we don't need to resolve
          * before texturing.  This will make depth testing a bit slower but
          * texturing faster.
          *
          * TODO: This is a heuristic trade-off; we haven't tuned it at all.
          */
         assert(device->info->ver >= 12);
         image->planes[plane].aux_usage = ISL_AUX_USAGE_HIZ_CCS_WT;
      } else {
         assert(device->info->ver >= 12);
         image->planes[plane].aux_usage = ISL_AUX_USAGE_HIZ_CCS;
      }

      result = add_surface(device, image, &image->planes[plane].aux_surface,
                           ANV_IMAGE_MEMORY_BINDING_PLANE_0 + plane,
                           ANV_OFFSET_IMPLICIT);
      if (result != VK_SUCCESS)
         return result;

      if (image->planes[plane].aux_usage == ISL_AUX_USAGE_HIZ_CCS_WT)
         return add_aux_state_tracking_buffer(device, image, plane);
   } else if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {

      if (INTEL_DEBUG(DEBUG_NO_CCS))
         return VK_SUCCESS;

      if (!isl_surf_supports_ccs(&device->isl_dev,
                                 &image->planes[plane].primary_surface.isl,
                                 NULL))
         return VK_SUCCESS;

      image->planes[plane].aux_usage = ISL_AUX_USAGE_STC_CCS;
   } else if ((aspect & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV) && image->vk.samples == 1) {
      if (image->n_planes != 1) {
         /* Multiplanar images seem to hit a sampler bug with CCS and R16G16
          * format. (Putting the clear state a page/4096bytes further fixes
          * the issue).
          */
         return VK_SUCCESS;
      }

      if ((image->vk.create_flags & VK_IMAGE_CREATE_ALIAS_BIT) && !image->from_wsi) {
         /* The image may alias a plane of a multiplanar image. Above we ban
          * CCS on multiplanar images.
          *
          * We must also reject aliasing of any image that uses
          * ANV_IMAGE_MEMORY_BINDING_PRIVATE. Since we're already rejecting all
          * aliasing here, there's no need to further analyze if the image needs
          * a private binding.
          */
         return VK_SUCCESS;
      }

      if (INTEL_DEBUG(DEBUG_NO_CCS))
         return VK_SUCCESS;

      ok = isl_surf_get_ccs_surf(&device->isl_dev,
                                 &image->planes[plane].primary_surface.isl,
                                 NULL,
                                 &image->planes[plane].aux_surface.isl,
                                 stride);
      if (!ok)
         return VK_SUCCESS;

      /* Choose aux usage */
      if (anv_formats_ccs_e_compatible(device->info, image->vk.create_flags,
                                       image->vk.format, image->vk.tiling,
                                       image->vk.usage, fmt_list)) {
         image->planes[plane].aux_usage =
            intel_needs_workaround(device->info, 1607794140) ?
               ISL_AUX_USAGE_FCV_CCS_E :
               ISL_AUX_USAGE_CCS_E;
      } else if (device->info->ver >= 12) {
         anv_perf_warn(VK_LOG_OBJS(&image->vk.base),
                       "The CCS_D aux mode is not yet handled on "
                       "Gfx12+. Not allocating a CCS buffer.");
         image->planes[plane].aux_surface.isl.size_B = 0;
         return VK_SUCCESS;
      } else {
         image->planes[plane].aux_usage = ISL_AUX_USAGE_CCS_D;
      }

      if (!device->physical->has_implicit_ccs) {
         enum anv_image_memory_binding binding =
            ANV_IMAGE_MEMORY_BINDING_PLANE_0 + plane;

         if (image->vk.drm_format_mod != DRM_FORMAT_MOD_INVALID &&
             !isl_drm_modifier_has_aux(image->vk.drm_format_mod))
            binding = ANV_IMAGE_MEMORY_BINDING_PRIVATE;

         result = add_surface(device, image, &image->planes[plane].aux_surface,
                              binding, offset);
         if (result != VK_SUCCESS)
            return result;
      }

      return add_aux_state_tracking_buffer(device, image, plane);
   } else if ((aspect & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV) && image->vk.samples > 1) {
      assert(!(image->vk.usage & VK_IMAGE_USAGE_STORAGE_BIT));
      ok = isl_surf_get_mcs_surf(&device->isl_dev,
                                 &image->planes[plane].primary_surface.isl,
                                 &image->planes[plane].aux_surface.isl);
      if (!ok)
         return VK_SUCCESS;

      image->planes[plane].aux_usage = ISL_AUX_USAGE_MCS;

      result = add_surface(device, image, &image->planes[plane].aux_surface,
                           ANV_IMAGE_MEMORY_BINDING_PLANE_0 + plane,
                           ANV_OFFSET_IMPLICIT);
      if (result != VK_SUCCESS)
         return result;

      return add_aux_state_tracking_buffer(device, image, plane);
   }

   return VK_SUCCESS;
}

static VkResult
add_video_buffers(struct anv_device *device,
                  struct anv_image *image,
                  const struct VkVideoProfileListInfoKHR *profile_list)
{
   ASSERTED bool ok;
   unsigned size = 0;

   for (unsigned i = 0; i < profile_list->profileCount; i++) {
      if (profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
         unsigned w_mb = DIV_ROUND_UP(image->vk.extent.width, ANV_MB_WIDTH);
         unsigned h_mb = DIV_ROUND_UP(image->vk.extent.height, ANV_MB_HEIGHT);
         size = w_mb * h_mb * 128;
      }
      else if (profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
         unsigned w_mb = DIV_ROUND_UP(image->vk.extent.width, 32);
         unsigned h_mb = DIV_ROUND_UP(image->vk.extent.height, 32);
         size = ALIGN(w_mb * h_mb, 2) << 6;
      }
   }

   if (size == 0)
      return VK_SUCCESS;

   ok = image_binding_grow(device, image, ANV_IMAGE_MEMORY_BINDING_PRIVATE,
                           ANV_OFFSET_IMPLICIT, size, 65536, &image->vid_dmv_top_surface);
   return ok;
}

/**
 * Initialize the anv_image::*_surface selected by \a aspect. Then update the
 * image's memory requirements (that is, the image's size and alignment).
 *
 * @param offset See add_surface()
 */
static VkResult
add_primary_surface(struct anv_device *device,
                    struct anv_image *image,
                    uint32_t plane,
                    struct anv_format_plane plane_format,
                    uint64_t offset,
                    uint32_t stride,
                    isl_tiling_flags_t isl_tiling_flags,
                    isl_surf_usage_flags_t isl_usage)
{
   struct anv_surface *anv_surf = &image->planes[plane].primary_surface;
   bool ok;

   uint32_t width = image->vk.extent.width;
   uint32_t height = image->vk.extent.height;
   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(image->vk.format);
   if (ycbcr_info) {
      assert(plane < ycbcr_info->n_planes);
      width /= ycbcr_info->planes[plane].denominator_scales[0];
      height /= ycbcr_info->planes[plane].denominator_scales[1];
   }

   ok = isl_surf_init(&device->isl_dev, &anv_surf->isl,
      .dim = vk_to_isl_surf_dim[image->vk.image_type],
      .format = plane_format.isl_format,
      .width = width,
      .height = height,
      .depth = image->vk.extent.depth,
      .levels = image->vk.mip_levels,
      .array_len = image->vk.array_layers,
      .samples = image->vk.samples,
      .min_alignment_B = 0,
      .row_pitch_B = stride,
      .usage = isl_usage,
      .tiling_flags = isl_tiling_flags);

   if (!ok) {
      /* TODO: Should return
       * VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT in come cases.
       */
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   image->planes[plane].aux_usage = ISL_AUX_USAGE_NONE;

   return add_surface(device, image, anv_surf,
                      ANV_IMAGE_MEMORY_BINDING_PLANE_0 + plane, offset);
}

#ifndef NDEBUG
static bool MUST_CHECK
memory_range_is_aligned(struct anv_image_memory_range memory_range)
{
   return anv_is_aligned(memory_range.offset, memory_range.alignment);
}

static bool MUST_CHECK
memory_ranges_equal(struct anv_image_memory_range a,
                    struct anv_image_memory_range b)
{
   return a.binding == b.binding &&
          a.offset == b.offset &&
          a.size == b.size &&
          a.alignment == b.alignment;
}
#endif

struct check_memory_range_params {
   struct anv_image_memory_range *accum_ranges;
   const struct anv_surface *test_surface;
   const struct anv_image_memory_range *test_range;
   enum anv_image_memory_binding expect_binding;
};

#define check_memory_range(...) \
   check_memory_range_s(&(struct check_memory_range_params) { __VA_ARGS__ })

static void UNUSED
check_memory_range_s(const struct check_memory_range_params *p)
{
   assert((p->test_surface == NULL) != (p->test_range == NULL));

   const struct anv_image_memory_range *test_range =
      p->test_range ?: &p->test_surface->memory_range;

   struct anv_image_memory_range *accum_range =
      &p->accum_ranges[p->expect_binding];

   assert(test_range->binding == p->expect_binding);
   assert(test_range->offset >= memory_range_end(*accum_range));
   assert(memory_range_is_aligned(*test_range));

   if (p->test_surface) {
      assert(anv_surface_is_valid(p->test_surface));
      assert(p->test_surface->memory_range.alignment ==
             p->test_surface->isl.alignment_B);
   }

   memory_range_merge(accum_range, *test_range);
}

/**
 * Validate the image's memory bindings *after* all its surfaces and memory
 * ranges are final.
 *
 * For simplicity's sake, we do not validate free-form layout of the image's
 * memory bindings. We validate the layout described in the comments of struct
 * anv_image.
 */
static void
check_memory_bindings(const struct anv_device *device,
                     const struct anv_image *image)
{
#ifdef DEBUG
   /* As we inspect each part of the image, we merge the part's memory range
    * into these accumulation ranges.
    */
   struct anv_image_memory_range accum_ranges[ANV_IMAGE_MEMORY_BINDING_END];
   for (int i = 0; i < ANV_IMAGE_MEMORY_BINDING_END; ++i) {
      accum_ranges[i] = (struct anv_image_memory_range) {
         .binding = i,
      };
   }

   for (uint32_t p = 0; p < image->n_planes; ++p) {
      const struct anv_image_plane *plane = &image->planes[p];

      /* The binding that must contain the plane's primary surface. */
      const enum anv_image_memory_binding primary_binding = image->disjoint
         ? ANV_IMAGE_MEMORY_BINDING_PLANE_0 + p
         : ANV_IMAGE_MEMORY_BINDING_MAIN;

      /* Aliasing is incompatible with the private binding because it does not
       * live in a VkDeviceMemory.  The exception is either swapchain images or
       * that the private binding is for a video motion vector buffer.
       */
      assert(!(image->vk.create_flags & VK_IMAGE_CREATE_ALIAS_BIT) ||
             image->from_wsi ||
             (plane->primary_surface.isl.usage & ISL_SURF_USAGE_VIDEO_DECODE_BIT) ||
             image->bindings[ANV_IMAGE_MEMORY_BINDING_PRIVATE].memory_range.size == 0);

      /* Check primary surface */
      check_memory_range(accum_ranges,
                         .test_surface = &plane->primary_surface,
                         .expect_binding = primary_binding);

      /* Check aux_surface */
      if (anv_surface_is_valid(&plane->aux_surface)) {
         enum anv_image_memory_binding binding = primary_binding;

         /* If an auxiliary surface is used for an externally-shareable image,
          * we have to hide this from the memory of the image since other
          * processes with access to the memory may not be aware of it or of
          * its current state. So put that auxiliary data into a separate
          * buffer (ANV_IMAGE_MEMORY_BINDING_PRIVATE).
          */
         if (anv_image_is_externally_shared(image) &&
             !isl_drm_modifier_has_aux(image->vk.drm_format_mod)) {
            binding = ANV_IMAGE_MEMORY_BINDING_PRIVATE;
         }

         /* Display hardware requires that the aux surface start at
          * a higher address than the primary surface. The 3D hardware
          * doesn't care, but we enforce the display requirement in case
          * the image is sent to display.
          */
         check_memory_range(accum_ranges,
                            .test_surface = &plane->aux_surface,
                            .expect_binding = binding);
      }

      /* Check fast clear state */
      if (plane->fast_clear_memory_range.size > 0) {
         enum anv_image_memory_binding binding = primary_binding;

         /* If an auxiliary surface is used for an externally-shareable image,
          * we have to hide this from the memory of the image since other
          * processes with access to the memory may not be aware of it or of
          * its current state. So put that auxiliary data into a separate
          * buffer (ANV_IMAGE_MEMORY_BINDING_PRIVATE).
          */
         if (anv_image_is_externally_shared(image)) {
            binding = ANV_IMAGE_MEMORY_BINDING_PRIVATE;
         }

         /* We believe that 256B alignment may be sufficient, but we choose 4K
          * due to lack of testing.  And MI_LOAD/STORE operations require
          * dword-alignment.
          */
         assert(plane->fast_clear_memory_range.alignment == 4096);
         check_memory_range(accum_ranges,
                            .test_range = &plane->fast_clear_memory_range,
                            .expect_binding = binding);
      }
   }
#endif
}

/**
 * Check that the fully-initialized anv_image is compatible with its DRM format
 * modifier.
 *
 * Checking compatibility at the end of image creation is prudent, not
 * superfluous, because usage of modifiers triggers numerous special cases
 * throughout queries and image creation, and because
 * vkGetPhysicalDeviceImageFormatProperties2 has difficulty detecting all
 * incompatibilities.
 *
 * Return VK_ERROR_UNKNOWN if the incompatibility is difficult to detect in
 * vkGetPhysicalDeviceImageFormatProperties2.  Otherwise, assert fail.
 *
 * Ideally, if vkGetPhysicalDeviceImageFormatProperties2() succeeds with a given
 * modifier, then vkCreateImage() produces an image that is compatible with the
 * modifier. However, it is difficult to reconcile the two functions to agree
 * due to their complexity. For example, isl_surf_get_ccs_surf() may
 * unexpectedly fail in vkCreateImage(), eliminating the image's aux surface
 * even when the modifier requires one. (Maybe we should reconcile the two
 * functions despite the difficulty).
 */
static VkResult MUST_CHECK
check_drm_format_mod(const struct anv_device *device,
                     const struct anv_image *image)
{
   /* Image must have a modifier if and only if it has modifier tiling. */
   assert((image->vk.drm_format_mod != DRM_FORMAT_MOD_INVALID) ==
          (image->vk.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT));

   if (image->vk.drm_format_mod == DRM_FORMAT_MOD_INVALID)
      return VK_SUCCESS;

   const struct isl_drm_modifier_info *isl_mod_info =
      isl_drm_modifier_get_info(image->vk.drm_format_mod);

   /* Driver must support the modifier. */
   assert(isl_drm_modifier_get_score(device->info, isl_mod_info->modifier));

   /* Enforced by us, not the Vulkan spec. */
   assert(image->vk.image_type == VK_IMAGE_TYPE_2D);
   assert(!(image->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT));
   assert(!(image->vk.aspects & VK_IMAGE_ASPECT_STENCIL_BIT));
   assert(image->vk.mip_levels == 1);
   assert(image->vk.array_layers == 1);
   assert(image->vk.samples == 1);

   for (int i = 0; i < image->n_planes; ++i) {
      const struct anv_image_plane *plane = &image->planes[i];
      ASSERTED const struct isl_format_layout *isl_layout =
         isl_format_get_layout(plane->primary_surface.isl.format);

      /* Enforced by us, not the Vulkan spec. */
      assert(isl_layout->txc == ISL_TXC_NONE);
      assert(isl_layout->colorspace == ISL_COLORSPACE_LINEAR ||
             isl_layout->colorspace == ISL_COLORSPACE_SRGB);

      if (isl_drm_modifier_has_aux(isl_mod_info->modifier)) {
         /* Reject DISJOINT for consistency with the GL driver. */
         assert(!image->disjoint);

         /* The modifier's required aux usage mandates the image's aux usage.
          * The inverse, however, does not hold; if the modifier has no aux
          * usage, then we may enable a private aux surface.
          */
         if ((isl_mod_info->supports_media_compression &&
              plane->aux_usage != ISL_AUX_USAGE_MC) ||
             (isl_mod_info->supports_render_compression &&
              !isl_aux_usage_has_ccs_e(plane->aux_usage))) {
            return vk_errorf(device, VK_ERROR_UNKNOWN,
                             "image with modifier unexpectedly has wrong aux "
                             "usage");
         }
      }
   }

   return VK_SUCCESS;
}

/**
 * Use when the app does not provide
 * VkImageDrmFormatModifierExplicitCreateInfoEXT.
 */
static VkResult MUST_CHECK
add_all_surfaces_implicit_layout(
   struct anv_device *device,
   struct anv_image *image,
   const VkImageFormatListCreateInfo *format_list_info,
   uint32_t stride,
   isl_tiling_flags_t isl_tiling_flags,
   isl_surf_usage_flags_t isl_extra_usage_flags)
{
   const struct intel_device_info *devinfo = device->info;
   VkResult result;

   u_foreach_bit(b, image->vk.aspects) {
      VkImageAspectFlagBits aspect = 1 << b;
      const uint32_t plane = anv_image_aspect_to_plane(image, aspect);
      const  struct anv_format_plane plane_format =
         anv_get_format_plane(devinfo, image->vk.format, plane, image->vk.tiling);

      VkImageUsageFlags vk_usage = vk_image_usage(&image->vk, aspect);
      isl_surf_usage_flags_t isl_usage =
         choose_isl_surf_usage(image->vk.create_flags, vk_usage,
                               isl_extra_usage_flags, aspect);

      result = add_primary_surface(device, image, plane, plane_format,
                                   ANV_OFFSET_IMPLICIT, stride,
                                   isl_tiling_flags, isl_usage);
      if (result != VK_SUCCESS)
         return result;

      /* Disable aux if image supports export without modifiers. */
      if (image->vk.external_handle_types != 0 &&
          image->vk.tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
         continue;

      result = add_aux_surface_if_supported(device, image, plane, plane_format,
                                            format_list_info,
                                            ANV_OFFSET_IMPLICIT, stride,
                                            isl_extra_usage_flags);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

/**
 * Use when the app provides VkImageDrmFormatModifierExplicitCreateInfoEXT.
 */
static VkResult
add_all_surfaces_explicit_layout(
   struct anv_device *device,
   struct anv_image *image,
   const VkImageFormatListCreateInfo *format_list_info,
   const VkImageDrmFormatModifierExplicitCreateInfoEXT *drm_info,
   isl_tiling_flags_t isl_tiling_flags,
   isl_surf_usage_flags_t isl_extra_usage_flags)
{
   const struct intel_device_info *devinfo = device->info;
   const uint32_t mod_plane_count = drm_info->drmFormatModifierPlaneCount;
   const bool mod_has_aux =
      isl_drm_modifier_has_aux(drm_info->drmFormatModifier);
   VkResult result;

   /* About valid usage in the Vulkan spec:
    *
    * Unlike vanilla vkCreateImage, which produces undefined behavior on user
    * error, here the spec requires the implementation to return
    * VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT if the app provides
    * a bad plane layout. However, the spec does require
    * drmFormatModifierPlaneCount to be valid.
    *
    * Most validation of plane layout occurs in add_surface().
    */

   /* We support a restricted set of images with modifiers.
    *
    * With aux usage on platforms without flat-CCS,
    * - Format plane count must be 1.
    * - Memory plane count must be 2.
    * Otherwise,
    * - Each format plane must map to a distint memory plane.
    *
    * For the other cases, currently there is no way to properly map memory
    * planes to format planes and aux planes due to the lack of defined ABI
    * for external multi-planar images.
    */
   if (image->n_planes == 1)
      assert(image->vk.aspects == VK_IMAGE_ASPECT_COLOR_BIT);
   else
      assert(!(image->vk.aspects & ~VK_IMAGE_ASPECT_PLANES_BITS_ANV));

   if (mod_has_aux && !devinfo->has_flat_ccs)
      assert(image->n_planes == 1 && mod_plane_count == 2);
   else
      assert(image->n_planes == mod_plane_count);

   /* Reject special values in the app-provided plane layouts. */
   for (uint32_t i = 0; i < mod_plane_count; ++i) {
      if (drm_info->pPlaneLayouts[i].rowPitch == 0) {
         return vk_errorf(device,
                          VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT,
                          "VkImageDrmFormatModifierExplicitCreateInfoEXT::"
                          "pPlaneLayouts[%u]::rowPitch is 0", i);
      }

      if (drm_info->pPlaneLayouts[i].offset == ANV_OFFSET_IMPLICIT) {
         return vk_errorf(device,
                          VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT,
                          "VkImageDrmFormatModifierExplicitCreateInfoEXT::"
                          "pPlaneLayouts[%u]::offset is %" PRIu64,
                          i, ANV_OFFSET_IMPLICIT);
      }
   }

   u_foreach_bit(b, image->vk.aspects) {
      const VkImageAspectFlagBits aspect = 1 << b;
      const uint32_t plane = anv_image_aspect_to_plane(image, aspect);
      const struct anv_format_plane format_plane =
         anv_get_format_plane(devinfo, image->vk.format, plane, image->vk.tiling);
      const VkSubresourceLayout *primary_layout = &drm_info->pPlaneLayouts[plane];

      result = add_primary_surface(device, image, plane,
                                   format_plane,
                                   primary_layout->offset,
                                   primary_layout->rowPitch,
                                   isl_tiling_flags,
                                   isl_extra_usage_flags);
      if (result != VK_SUCCESS)
         return result;

      if (mod_has_aux) {
         assert(!isl_drm_modifier_get_info(
                  drm_info->drmFormatModifier)->supports_clear_color);

         const VkSubresourceLayout flat_ccs_layout = {
            .offset = ANV_OFFSET_IMPLICIT,
         };
         const VkSubresourceLayout *aux_layout = devinfo->has_flat_ccs ?
            &flat_ccs_layout : &drm_info->pPlaneLayouts[1];

         result = add_aux_surface_if_supported(device, image, plane,
                                               format_plane,
                                               format_list_info,
                                               aux_layout->offset,
                                               aux_layout->rowPitch,
                                               isl_extra_usage_flags);
         if (result != VK_SUCCESS)
            return result;

         assert(isl_aux_usage_has_ccs(image->planes[plane].aux_usage));
      }
   }

   return VK_SUCCESS;
}

static const struct isl_drm_modifier_info *
choose_drm_format_mod(const struct anv_physical_device *device,
                      uint32_t modifier_count, const uint64_t *modifiers)
{
   uint64_t best_mod = UINT64_MAX;
   uint32_t best_score = 0;

   for (uint32_t i = 0; i < modifier_count; ++i) {
      uint32_t score = isl_drm_modifier_get_score(&device->info, modifiers[i]);
      if (score > best_score) {
         best_mod = modifiers[i];
         best_score = score;
      }
   }

   if (best_score > 0)
      return isl_drm_modifier_get_info(best_mod);
   else
      return NULL;
}

static VkImageUsageFlags
anv_image_create_usage(const VkImageCreateInfo *pCreateInfo,
                       VkImageUsageFlags usage)
{
   /* Add TRANSFER_SRC usage for multisample attachment images. This is
    * because we might internally use the TRANSFER_SRC layout on them for
    * blorp operations associated with resolving those into other attachments
    * at the end of a subpass.
    *
    * Without this additional usage, we compute an incorrect AUX state in
    * anv_layout_to_aux_state().
    */
   if (pCreateInfo->samples > VK_SAMPLE_COUNT_1_BIT &&
       (usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)))
      usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
   return usage;
}

static VkResult MUST_CHECK
alloc_private_binding(struct anv_device *device,
                      struct anv_image *image,
                      const VkImageCreateInfo *create_info)
{
   struct anv_image_binding *binding =
      &image->bindings[ANV_IMAGE_MEMORY_BINDING_PRIVATE];

   if (binding->memory_range.size == 0)
      return VK_SUCCESS;

   const VkImageSwapchainCreateInfoKHR *swapchain_info =
      vk_find_struct_const(create_info->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);

   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
      /* The image will be bound to swapchain memory. */
      return VK_SUCCESS;
   }

   VkResult result = anv_device_alloc_bo(device, "image-binding-private",
                                         binding->memory_range.size, 0, 0,
                                         &binding->address.bo);
   if (result == VK_SUCCESS) {
      pthread_mutex_lock(&device->mutex);
      list_addtail(&image->link, &device->image_private_objects);
      pthread_mutex_unlock(&device->mutex);
   }

   return result;
}

VkResult
anv_image_init(struct anv_device *device, struct anv_image *image,
               const struct anv_image_create_info *create_info)
{
   const VkImageCreateInfo *pCreateInfo = create_info->vk_info;
   const struct VkImageDrmFormatModifierExplicitCreateInfoEXT *mod_explicit_info = NULL;
   const struct isl_drm_modifier_info *isl_mod_info = NULL;
   VkResult r;

   vk_image_init(&device->vk, &image->vk, pCreateInfo);

   image->vk.usage = anv_image_create_usage(pCreateInfo, image->vk.usage);
   image->vk.stencil_usage =
      anv_image_create_usage(pCreateInfo, image->vk.stencil_usage);

   if (pCreateInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      assert(!image->vk.wsi_legacy_scanout);
      mod_explicit_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
      if (mod_explicit_info) {
         isl_mod_info = isl_drm_modifier_get_info(mod_explicit_info->drmFormatModifier);
      } else {
         const struct VkImageDrmFormatModifierListCreateInfoEXT *mod_list_info =
            vk_find_struct_const(pCreateInfo->pNext,
                                 IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);
         isl_mod_info = choose_drm_format_mod(device->physical,
                                              mod_list_info->drmFormatModifierCount,
                                              mod_list_info->pDrmFormatModifiers);
      }

      assert(isl_mod_info);
      assert(image->vk.drm_format_mod == DRM_FORMAT_MOD_INVALID);
      image->vk.drm_format_mod = isl_mod_info->modifier;
   }

   for (int i = 0; i < ANV_IMAGE_MEMORY_BINDING_END; ++i) {
      image->bindings[i] = (struct anv_image_binding) {
         .memory_range = { .binding = i },
      };
   }

   /* In case of AHardwareBuffer import, we don't know the layout yet */
   if (image->vk.external_handle_types &
       VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID) {
      image->from_ahb = true;
#ifdef ANDROID
      image->vk.ahb_format = anv_ahb_format_for_vk_format(image->vk.format);
#endif
      return VK_SUCCESS;
   }

   image->n_planes = anv_get_format_planes(image->vk.format);

   image->from_wsi =
      vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA) != NULL;

   /* The Vulkan 1.2.165 glossary says:
    *
    *    A disjoint image consists of multiple disjoint planes, and is created
    *    with the VK_IMAGE_CREATE_DISJOINT_BIT bit set.
    */
   image->disjoint = image->n_planes > 1 &&
                     (pCreateInfo->flags & VK_IMAGE_CREATE_DISJOINT_BIT);

   const isl_tiling_flags_t isl_tiling_flags =
      choose_isl_tiling_flags(device->info, create_info, isl_mod_info,
                              image->vk.wsi_legacy_scanout);

   const VkImageFormatListCreateInfo *fmt_list =
      vk_find_struct_const(pCreateInfo->pNext,
                           IMAGE_FORMAT_LIST_CREATE_INFO);

   if (mod_explicit_info) {
      r = add_all_surfaces_explicit_layout(device, image, fmt_list,
                                           mod_explicit_info, isl_tiling_flags,
                                           create_info->isl_extra_usage_flags);
   } else {
      r = add_all_surfaces_implicit_layout(device, image, fmt_list, create_info->stride,
                                           isl_tiling_flags,
                                           create_info->isl_extra_usage_flags);
   }

   if (r != VK_SUCCESS)
      goto fail;

   const VkVideoProfileListInfoKHR *video_profile =
      vk_find_struct_const(pCreateInfo->pNext,
                           VIDEO_PROFILE_LIST_INFO_KHR);
   if (video_profile) {
      r = add_video_buffers(device, image, video_profile);
      if (r != VK_SUCCESS)
         goto fail;
   }

   if (!create_info->no_private_binding_alloc) {
      r = alloc_private_binding(device, image, pCreateInfo);
      if (r != VK_SUCCESS)
         goto fail;
   }

   check_memory_bindings(device, image);

   r = check_drm_format_mod(device, image);
   if (r != VK_SUCCESS)
      goto fail;

   /* Once we have all the bindings, determine whether we can do non 0 fast
    * clears for each plane.
    */
   for (uint32_t p = 0; p < image->n_planes; p++) {
      image->planes[p].can_non_zero_fast_clear =
         can_fast_clear_with_non_zero_color(device->info, image, p, fmt_list);
   }

   return VK_SUCCESS;

fail:
   vk_image_finish(&image->vk);
   return r;
}

void
anv_image_finish(struct anv_image *image)
{
   struct anv_device *device =
      container_of(image->vk.base.device, struct anv_device, vk);

   if (image->from_gralloc) {
      assert(!image->disjoint);
      assert(image->n_planes == 1);
      assert(image->planes[0].primary_surface.memory_range.binding ==
             ANV_IMAGE_MEMORY_BINDING_MAIN);
      assert(image->bindings[ANV_IMAGE_MEMORY_BINDING_MAIN].address.bo != NULL);
      anv_device_release_bo(device, image->bindings[ANV_IMAGE_MEMORY_BINDING_MAIN].address.bo);
   }

   struct anv_bo *private_bo = image->bindings[ANV_IMAGE_MEMORY_BINDING_PRIVATE].address.bo;
   if (private_bo) {
      pthread_mutex_lock(&device->mutex);
      list_del(&image->link);
      pthread_mutex_unlock(&device->mutex);
      anv_device_release_bo(device, private_bo);
   }

   vk_image_finish(&image->vk);
}

static struct anv_image *
anv_swapchain_get_image(VkSwapchainKHR swapchain,
                        uint32_t index)
{
   VkImage image = wsi_common_get_image(swapchain, index);
   return anv_image_from_handle(image);
}

static VkResult
anv_image_init_from_create_info(struct anv_device *device,
                                struct anv_image *image,
                                const VkImageCreateInfo *pCreateInfo,
                                bool no_private_binding_alloc)
{
   const VkNativeBufferANDROID *gralloc_info =
      vk_find_struct_const(pCreateInfo->pNext, NATIVE_BUFFER_ANDROID);
   if (gralloc_info)
      return anv_image_init_from_gralloc(device, image, pCreateInfo,
                                         gralloc_info);

   struct anv_image_create_info create_info = {
      .vk_info = pCreateInfo,
      .no_private_binding_alloc = no_private_binding_alloc,
   };

   /* For dmabuf imports, configure the primary surface without support for
    * compression if the modifier doesn't specify it. This helps to create
    * VkImages with memory requirements that are compatible with the buffers
    * apps provide.
    */
   const struct VkImageDrmFormatModifierExplicitCreateInfoEXT *mod_explicit_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
   if (mod_explicit_info &&
       !isl_drm_modifier_has_aux(mod_explicit_info->drmFormatModifier))
      create_info.isl_extra_usage_flags |= ISL_SURF_USAGE_DISABLE_AUX_BIT;

   return anv_image_init(device, image, &create_info);
}

VkResult anv_CreateImage(
    VkDevice                                    _device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

#ifndef VK_USE_PLATFORM_ANDROID_KHR
   /* Ignore swapchain creation info on Android. Since we don't have an
    * implementation in Mesa, we're guaranteed to access an Android object
    * incorrectly.
    */
   const VkImageSwapchainCreateInfoKHR *swapchain_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);
   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
      return wsi_common_create_swapchain_image(&device->physical->wsi_device,
                                               pCreateInfo,
                                               swapchain_info->swapchain,
                                               pImage);
   }
#endif

   struct anv_image *image =
      vk_object_zalloc(&device->vk, pAllocator, sizeof(*image),
                       VK_OBJECT_TYPE_IMAGE);
   if (!image)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = anv_image_init_from_create_info(device, image,
                                                     pCreateInfo,
                                                     false);
   if (result != VK_SUCCESS) {
      vk_object_free(&device->vk, pAllocator, image);
      return result;
   }

   *pImage = anv_image_to_handle(image);

   return result;
}

void
anv_DestroyImage(VkDevice _device, VkImage _image,
                 const VkAllocationCallbacks *pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, image, _image);

   if (!image)
      return;

   assert(&device->vk == image->vk.base.device);
   anv_image_finish(image);

   vk_free2(&device->vk.alloc, pAllocator, image);
}

/* We are binding AHardwareBuffer. Get a description, resolve the
 * format and prepare anv_image properly.
 */
static void
resolve_ahw_image(struct anv_device *device,
                  struct anv_image *image,
                  struct anv_device_memory *mem)
{
#if defined(ANDROID) && ANDROID_API_LEVEL >= 26
   assert(mem->vk.ahardware_buffer);
   AHardwareBuffer_Desc desc;
   AHardwareBuffer_describe(mem->vk.ahardware_buffer, &desc);
   VkResult result;

   /* Check tiling. */
   enum isl_tiling tiling;
   result = anv_device_get_bo_tiling(device, mem->bo, &tiling);
   assert(result == VK_SUCCESS);

   VkImageTiling vk_tiling =
      tiling == ISL_TILING_LINEAR ? VK_IMAGE_TILING_LINEAR :
                                    VK_IMAGE_TILING_OPTIMAL;
   isl_tiling_flags_t isl_tiling_flags = (1u << tiling);

   /* Check format. */
   VkFormat vk_format = vk_format_from_android(desc.format, desc.usage);
   enum isl_format isl_fmt = anv_get_isl_format(device->info,
                                                vk_format,
                                                VK_IMAGE_ASPECT_COLOR_BIT,
                                                vk_tiling);
   assert(isl_fmt != ISL_FORMAT_UNSUPPORTED);

   /* Handle RGB(X)->RGBA fallback. */
   switch (desc.format) {
   case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:
   case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
      if (isl_format_is_rgb(isl_fmt))
         isl_fmt = isl_format_rgb_to_rgba(isl_fmt);
      break;
   }

   /* Now we are able to fill anv_image fields properly and create
    * isl_surface for it.
    */
   vk_image_set_format(&image->vk, vk_format);
   image->n_planes = anv_get_format_planes(image->vk.format);

   uint32_t stride = desc.stride *
                     (isl_format_get_layout(isl_fmt)->bpb / 8);

   result = add_all_surfaces_implicit_layout(device, image, NULL, stride,
                                             isl_tiling_flags,
                                             ISL_SURF_USAGE_DISABLE_AUX_BIT);
   assert(result == VK_SUCCESS);
#endif
}

void
anv_image_get_memory_requirements(struct anv_device *device,
                                  struct anv_image *image,
                                  VkImageAspectFlags aspects,
                                  VkMemoryRequirements2 *pMemoryRequirements)
{
   /* The Vulkan spec (git aaed022) says:
    *
    *    memoryTypeBits is a bitfield and contains one bit set for every
    *    supported memory type for the resource. The bit `1<<i` is set if and
    *    only if the memory type `i` in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported.
    *
    * All types are currently supported for images.
    */
   uint32_t memory_types = (1ull << device->physical->memory.type_count) - 1;

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *requirements = (void *)ext;
         if (image->vk.wsi_legacy_scanout || image->from_ahb) {
            /* If we need to set the tiling for external consumers, we need a
             * dedicated allocation.
             *
             * See also anv_AllocateMemory.
             */
            requirements->prefersDedicatedAllocation = true;
            requirements->requiresDedicatedAllocation = true;
         } else {
            requirements->prefersDedicatedAllocation = false;
            requirements->requiresDedicatedAllocation = false;
         }
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }

   /* If the image is disjoint, then we must return the memory requirements for
    * the single plane specified in VkImagePlaneMemoryRequirementsInfo. If
    * non-disjoint, then exactly one set of memory requirements exists for the
    * whole image.
    *
    * This is enforced by the Valid Usage for VkImageMemoryRequirementsInfo2,
    * which requires that the app provide VkImagePlaneMemoryRequirementsInfo if
    * and only if the image is disjoint (that is, multi-planar format and
    * VK_IMAGE_CREATE_DISJOINT_BIT).
    */
   const struct anv_image_binding *binding;
   if (image->disjoint) {
      assert(util_bitcount(aspects) == 1);
      assert(aspects & image->vk.aspects);
      binding = image_aspect_to_binding(image, aspects);
   } else {
      assert(aspects == image->vk.aspects);
      binding = &image->bindings[ANV_IMAGE_MEMORY_BINDING_MAIN];
   }

   pMemoryRequirements->memoryRequirements = (VkMemoryRequirements) {
      .size = binding->memory_range.size,
      .alignment = binding->memory_range.alignment,
      .memoryTypeBits = memory_types,
   };
}

void anv_GetImageMemoryRequirements2(
    VkDevice                                    _device,
    const VkImageMemoryRequirementsInfo2*       pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, image, pInfo->image);

   VkImageAspectFlags aspects = image->vk.aspects;

   vk_foreach_struct_const(ext, pInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO: {
         assert(image->disjoint);
         const VkImagePlaneMemoryRequirementsInfo *plane_reqs =
            (const VkImagePlaneMemoryRequirementsInfo *) ext;
         aspects = plane_reqs->planeAspect;
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }

   anv_image_get_memory_requirements(device, image, aspects,
                                     pMemoryRequirements);
}

void anv_GetDeviceImageMemoryRequirementsKHR(
    VkDevice                                    _device,
    const VkDeviceImageMemoryRequirements*   pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_image image = { 0 };

   ASSERTED VkResult result =
      anv_image_init_from_create_info(device, &image, pInfo->pCreateInfo, true);
   assert(result == VK_SUCCESS);

   VkImageAspectFlags aspects =
      image.disjoint ? pInfo->planeAspect : image.vk.aspects;

   anv_image_get_memory_requirements(device, &image, aspects,
                                     pMemoryRequirements);
}

void anv_GetImageSparseMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements*            pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

void anv_GetImageSparseMemoryRequirements2(
    VkDevice                                    device,
    const VkImageSparseMemoryRequirementsInfo2* pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2*           pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

void anv_GetDeviceImageSparseMemoryRequirementsKHR(
    VkDevice                                    device,
    const VkDeviceImageMemoryRequirements* pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2*           pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

VkResult anv_BindImageMemory2(
    VkDevice                                    _device,
    uint32_t                                    bindInfoCount,
    const VkBindImageMemoryInfo*                pBindInfos)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   for (uint32_t i = 0; i < bindInfoCount; i++) {
      const VkBindImageMemoryInfo *bind_info = &pBindInfos[i];
      ANV_FROM_HANDLE(anv_device_memory, mem, bind_info->memory);
      ANV_FROM_HANDLE(anv_image, image, bind_info->image);
      bool did_bind = false;

      /* Resolve will alter the image's aspects, do this first. */
      if (mem && mem->vk.ahardware_buffer)
         resolve_ahw_image(device, image, mem);

      vk_foreach_struct_const(s, bind_info->pNext) {
         switch (s->sType) {
         case VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO: {
            const VkBindImagePlaneMemoryInfo *plane_info =
               (const VkBindImagePlaneMemoryInfo *) s;

            /* Workaround for possible spec bug.
             *
             * Unlike VkImagePlaneMemoryRequirementsInfo, which requires that
             * the image be disjoint (that is, multi-planar format and
             * VK_IMAGE_CREATE_DISJOINT_BIT), VkBindImagePlaneMemoryInfo allows
             * the image to be non-disjoint and requires only that the image
             * have the DISJOINT flag. In this case, regardless of the value of
             * VkImagePlaneMemoryRequirementsInfo::planeAspect, the behavior is
             * the same as if VkImagePlaneMemoryRequirementsInfo were omitted.
             */
            if (!image->disjoint)
               break;

            struct anv_image_binding *binding =
               image_aspect_to_binding(image, plane_info->planeAspect);

            binding->address = (struct anv_address) {
               .bo = mem->bo,
               .offset = bind_info->memoryOffset,
            };

            did_bind = true;
            break;
         }
         case VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR: {
            /* Ignore this struct on Android, we cannot access swapchain
             * structures there.
             */
#ifndef VK_USE_PLATFORM_ANDROID_KHR
            const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
               (const VkBindImageMemorySwapchainInfoKHR *) s;
            struct anv_image *swapchain_image =
               anv_swapchain_get_image(swapchain_info->swapchain,
                                       swapchain_info->imageIndex);
            assert(swapchain_image);
            assert(image->vk.aspects == swapchain_image->vk.aspects);
            assert(mem == NULL);

            for (int j = 0; j < ARRAY_SIZE(image->bindings); ++j) {
               assert(memory_ranges_equal(image->bindings[j].memory_range,
                                          swapchain_image->bindings[j].memory_range));
               image->bindings[j].address = swapchain_image->bindings[j].address;
            }

            /* We must bump the private binding's bo's refcount because, unlike the other
             * bindings, its lifetime is not application-managed.
             */
            struct anv_bo *private_bo =
               image->bindings[ANV_IMAGE_MEMORY_BINDING_PRIVATE].address.bo;
            if (private_bo)
               anv_bo_ref(private_bo);

            did_bind = true;
#endif
            break;
         }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
         case VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID: {
            const VkNativeBufferANDROID *gralloc_info =
               (const VkNativeBufferANDROID *)s;
            VkResult result = anv_image_bind_from_gralloc(device, image,
                                                          gralloc_info);
            if (result != VK_SUCCESS)
               return result;
            did_bind = true;
            break;
         }
#pragma GCC diagnostic pop
         default:
            anv_debug_ignored_stype(s->sType);
            break;
         }
      }

      if (!did_bind) {
         assert(!image->disjoint);

         image->bindings[ANV_IMAGE_MEMORY_BINDING_MAIN].address =
            (struct anv_address) {
               .bo = mem->bo,
               .offset = bind_info->memoryOffset,
            };

         did_bind = true;
      }

      /* On platforms that use implicit CCS, if the plane's bo lacks implicit
       * CCS then disable compression on the plane.
       */
      for (int p = 0; p < image->n_planes; ++p) {
         enum anv_image_memory_binding binding =
            image->planes[p].primary_surface.memory_range.binding;
         const struct anv_bo *bo =
            image->bindings[binding].address.bo;

         if (!bo || bo->has_implicit_ccs)
            continue;

         if (!device->physical->has_implicit_ccs)
            continue;

         if (!isl_aux_usage_has_ccs(image->planes[p].aux_usage))
            continue;

         anv_perf_warn(VK_LOG_OBJS(&image->vk.base),
                       "BO lacks implicit CCS. Disabling the CCS aux usage.");

         if (image->planes[p].aux_surface.memory_range.size > 0) {
            assert(image->planes[p].aux_usage == ISL_AUX_USAGE_HIZ_CCS ||
                   image->planes[p].aux_usage == ISL_AUX_USAGE_HIZ_CCS_WT);
            image->planes[p].aux_usage = ISL_AUX_USAGE_HIZ;
         } else {
            assert(image->planes[p].aux_usage == ISL_AUX_USAGE_CCS_E ||
                   image->planes[p].aux_usage == ISL_AUX_USAGE_FCV_CCS_E ||
                   image->planes[p].aux_usage == ISL_AUX_USAGE_STC_CCS);
            image->planes[p].aux_usage = ISL_AUX_USAGE_NONE;
         }
      }
   }

   return VK_SUCCESS;
}

void anv_GetImageSubresourceLayout(
    VkDevice                                    device,
    VkImage                                     _image,
    const VkImageSubresource*                   subresource,
    VkSubresourceLayout*                        layout)
{
   ANV_FROM_HANDLE(anv_image, image, _image);
   const struct anv_surface *surface;

   assert(__builtin_popcount(subresource->aspectMask) == 1);

   /* The Vulkan spec requires that aspectMask be
    * VK_IMAGE_ASPECT_MEMORY_PLANE_i_BIT_EXT if tiling is
    * VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT.
    *
    * For swapchain images, the Vulkan spec says that every swapchain image has
    * tiling VK_IMAGE_TILING_OPTIMAL, but we may choose
    * VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT internally.  Vulkan doesn't allow
    * vkGetImageSubresourceLayout for images with VK_IMAGE_TILING_OPTIMAL,
    * therefore it's invalid for the application to call this on a swapchain
    * image.  The WSI code, however, knows when it has internally created
    * a swapchain image with VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
    * so it _should_ correctly use VK_IMAGE_ASPECT_MEMORY_PLANE_* in that case.
    * But it incorrectly uses VK_IMAGE_ASPECT_PLANE_*, so we have a temporary
    * workaround.
    */
   if (image->vk.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      /* TODO(chadv): Drop this workaround when WSI gets fixed. */
      uint32_t mem_plane;
      switch (subresource->aspectMask) {
      case VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT:
      case VK_IMAGE_ASPECT_PLANE_0_BIT:
         mem_plane = 0;
         break;
      case VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT:
      case VK_IMAGE_ASPECT_PLANE_1_BIT:
         mem_plane = 1;
         break;
      case VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT:
      case VK_IMAGE_ASPECT_PLANE_2_BIT:
         mem_plane = 2;
         break;
      default:
         unreachable("bad VkImageAspectFlags");
      }

      if (mem_plane == 1 && isl_drm_modifier_has_aux(image->vk.drm_format_mod)) {
         assert(image->n_planes == 1);
         /* If the memory binding differs between primary and aux, then the
          * returned offset will be incorrect.
          */
         assert(image->planes[0].aux_surface.memory_range.binding ==
                image->planes[0].primary_surface.memory_range.binding);
         surface = &image->planes[0].aux_surface;
      } else {
         assert(mem_plane < image->n_planes);
         surface = &image->planes[mem_plane].primary_surface;
      }
   } else {
      const uint32_t plane =
         anv_image_aspect_to_plane(image, subresource->aspectMask);
      surface = &image->planes[plane].primary_surface;
   }

   layout->offset = surface->memory_range.offset;
   layout->rowPitch = surface->isl.row_pitch_B;
   layout->depthPitch = isl_surf_get_array_pitch(&surface->isl);
   layout->arrayPitch = isl_surf_get_array_pitch(&surface->isl);

   if (subresource->mipLevel > 0 || subresource->arrayLayer > 0) {
      assert(surface->isl.tiling == ISL_TILING_LINEAR);

      uint64_t offset_B;
      isl_surf_get_image_offset_B_tile_sa(&surface->isl,
                                          subresource->mipLevel,
                                          subresource->arrayLayer,
                                          0 /* logical_z_offset_px */,
                                          &offset_B, NULL, NULL);
      layout->offset += offset_B;
      layout->size = layout->rowPitch * u_minify(image->vk.extent.height,
                                                 subresource->mipLevel) *
                     image->vk.extent.depth;
   } else {
      layout->size = surface->memory_range.size;
   }
}

/**
 * This function returns the assumed isl_aux_state for a given VkImageLayout.
 * Because Vulkan image layouts don't map directly to isl_aux_state enums, the
 * returned enum is the assumed worst case.
 *
 * @param devinfo The device information of the Intel GPU.
 * @param image The image that may contain a collection of buffers.
 * @param aspect The aspect of the image to be accessed.
 * @param layout The current layout of the image aspect(s).
 *
 * @return The primary buffer that should be used for the given layout.
 */
enum isl_aux_state ATTRIBUTE_PURE
anv_layout_to_aux_state(const struct intel_device_info * const devinfo,
                        const struct anv_image * const image,
                        const VkImageAspectFlagBits aspect,
                        const VkImageLayout layout)
{
   /* Validate the inputs. */

   /* The devinfo is needed as the optimal buffer varies across generations. */
   assert(devinfo != NULL);

   /* The layout of a NULL image is not properly defined. */
   assert(image != NULL);

   /* The aspect must be exactly one of the image aspects. */
   assert(util_bitcount(aspect) == 1 && (aspect & image->vk.aspects));

   /* Determine the optimal buffer. */

   const uint32_t plane = anv_image_aspect_to_plane(image, aspect);

   /* If we don't have an aux buffer then aux state makes no sense */
   const enum isl_aux_usage aux_usage = image->planes[plane].aux_usage;
   assert(aux_usage != ISL_AUX_USAGE_NONE);

   /* All images that use an auxiliary surface are required to be tiled. */
   assert(image->planes[plane].primary_surface.isl.tiling != ISL_TILING_LINEAR);

   /* Handle a few special cases */
   switch (layout) {
   /* Invalid layouts */
   case VK_IMAGE_LAYOUT_MAX_ENUM:
      unreachable("Invalid image layout.");

   /* Undefined layouts
    *
    * The pre-initialized layout is equivalent to the undefined layout for
    * optimally-tiled images.  We can only do color compression (CCS or HiZ)
    * on tiled images.
    */
   case VK_IMAGE_LAYOUT_UNDEFINED:
   case VK_IMAGE_LAYOUT_PREINITIALIZED:
      return ISL_AUX_STATE_AUX_INVALID;

   case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: {
      assert(image->vk.aspects == VK_IMAGE_ASPECT_COLOR_BIT);

      enum isl_aux_state aux_state =
         isl_drm_modifier_get_default_aux_state(image->vk.drm_format_mod);

      switch (aux_state) {
      case ISL_AUX_STATE_AUX_INVALID:
         /* The modifier does not support compression. But, if we arrived
          * here, then we have enabled compression on it anyway, in which case
          * we must resolve the aux surface before we release ownership to the
          * presentation engine (because, having no modifier, the presentation
          * engine will not be aware of the aux surface). The presentation
          * engine will not access the aux surface (because it is unware of
          * it), and so the aux surface will still be resolved when we
          * re-acquire ownership.
          *
          * Therefore, at ownership transfers in either direction, there does
          * exist an aux surface despite the lack of modifier and its state is
          * pass-through.
          */
         return ISL_AUX_STATE_PASS_THROUGH;
      case ISL_AUX_STATE_COMPRESSED_NO_CLEAR:
         return ISL_AUX_STATE_COMPRESSED_NO_CLEAR;
      default:
         unreachable("unexpected isl_aux_state");
      }
   }

   default:
      break;
   }

   const bool read_only = vk_image_layout_is_read_only(layout, aspect);

   const VkImageUsageFlags image_aspect_usage =
      vk_image_usage(&image->vk, aspect);
   const VkImageUsageFlags usage =
      vk_image_layout_to_usage_flags(layout, aspect) & image_aspect_usage;

   bool aux_supported = true;
   bool clear_supported = isl_aux_usage_has_fast_clears(aux_usage);

   if ((usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) && !read_only) {
      /* This image could be used as both an input attachment and a render
       * target (depth, stencil, or color) at the same time and this can cause
       * corruption.
       *
       * We currently only disable aux in this way for depth even though we
       * disable it for color in GL.
       *
       * TODO: Should we be disabling this in more cases?
       */
      if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT && devinfo->ver <= 9) {
         aux_supported = false;
         clear_supported = false;
      }
   }

   if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
      switch (aux_usage) {
      case ISL_AUX_USAGE_HIZ:
         if (!anv_can_sample_with_hiz(devinfo, image)) {
            aux_supported = false;
            clear_supported = false;
         }
         break;

      case ISL_AUX_USAGE_HIZ_CCS:
         aux_supported = false;
         clear_supported = false;
         break;

      case ISL_AUX_USAGE_HIZ_CCS_WT:
         break;

      case ISL_AUX_USAGE_CCS_D:
         aux_supported = false;
         clear_supported = false;
         break;

      case ISL_AUX_USAGE_MCS:
         if (!anv_can_sample_mcs_with_clear(devinfo, image))
            clear_supported = false;
         break;

      case ISL_AUX_USAGE_CCS_E:
      case ISL_AUX_USAGE_FCV_CCS_E:
      case ISL_AUX_USAGE_STC_CCS:
         break;

      default:
         unreachable("Unsupported aux usage");
      }
   }

   switch (aux_usage) {
   case ISL_AUX_USAGE_HIZ:
   case ISL_AUX_USAGE_HIZ_CCS:
   case ISL_AUX_USAGE_HIZ_CCS_WT:
      if (aux_supported) {
         assert(clear_supported);
         return ISL_AUX_STATE_COMPRESSED_CLEAR;
      } else if (read_only) {
         return ISL_AUX_STATE_RESOLVED;
      } else {
         return ISL_AUX_STATE_AUX_INVALID;
      }

   case ISL_AUX_USAGE_CCS_D:
      /* We only support clear in exactly one state */
      if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
          layout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL) {
         assert(aux_supported);
         assert(clear_supported);
         return ISL_AUX_STATE_PARTIAL_CLEAR;
      } else {
         return ISL_AUX_STATE_PASS_THROUGH;
      }

   case ISL_AUX_USAGE_CCS_E:
   case ISL_AUX_USAGE_FCV_CCS_E:
      if (aux_supported) {
         assert(clear_supported);
         return ISL_AUX_STATE_COMPRESSED_CLEAR;
      } else {
         return ISL_AUX_STATE_PASS_THROUGH;
      }

   case ISL_AUX_USAGE_MCS:
      assert(aux_supported);
      if (clear_supported) {
         return ISL_AUX_STATE_COMPRESSED_CLEAR;
      } else {
         return ISL_AUX_STATE_COMPRESSED_NO_CLEAR;
      }

   case ISL_AUX_USAGE_STC_CCS:
      assert(aux_supported);
      assert(!clear_supported);
      return ISL_AUX_STATE_COMPRESSED_NO_CLEAR;

   default:
      unreachable("Unsupported aux usage");
   }
}

/**
 * This function determines the optimal buffer to use for a given
 * VkImageLayout and other pieces of information needed to make that
 * determination. This does not determine the optimal buffer to use
 * during a resolve operation.
 *
 * @param devinfo The device information of the Intel GPU.
 * @param image The image that may contain a collection of buffers.
 * @param aspect The aspect of the image to be accessed.
 * @param usage The usage which describes how the image will be accessed.
 * @param layout The current layout of the image aspect(s).
 *
 * @return The primary buffer that should be used for the given layout.
 */
enum isl_aux_usage ATTRIBUTE_PURE
anv_layout_to_aux_usage(const struct intel_device_info * const devinfo,
                        const struct anv_image * const image,
                        const VkImageAspectFlagBits aspect,
                        const VkImageUsageFlagBits usage,
                        const VkImageLayout layout)
{
   const uint32_t plane = anv_image_aspect_to_plane(image, aspect);

   /* If there is no auxiliary surface allocated, we must use the one and only
    * main buffer.
    */
   if (image->planes[plane].aux_usage == ISL_AUX_USAGE_NONE)
      return ISL_AUX_USAGE_NONE;

   enum isl_aux_state aux_state =
      anv_layout_to_aux_state(devinfo, image, aspect, layout);

   switch (aux_state) {
   case ISL_AUX_STATE_CLEAR:
      unreachable("We never use this state");

   case ISL_AUX_STATE_PARTIAL_CLEAR:
      assert(image->vk.aspects & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV);
      assert(image->planes[plane].aux_usage == ISL_AUX_USAGE_CCS_D);
      assert(image->vk.samples == 1);
      return ISL_AUX_USAGE_CCS_D;

   case ISL_AUX_STATE_COMPRESSED_CLEAR:
   case ISL_AUX_STATE_COMPRESSED_NO_CLEAR:
      return image->planes[plane].aux_usage;

   case ISL_AUX_STATE_RESOLVED:
      /* We can only use RESOLVED in read-only layouts because any write will
       * either land us in AUX_INVALID or COMPRESSED_NO_CLEAR.  We can do
       * writes in PASS_THROUGH without destroying it so that is allowed.
       */
      assert(vk_image_layout_is_read_only(layout, aspect));
      assert(util_is_power_of_two_or_zero(usage));
      if (usage == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
         /* If we have valid HiZ data and are using the image as a read-only
          * depth/stencil attachment, we should enable HiZ so that we can get
          * faster depth testing.
          */
         return image->planes[plane].aux_usage;
      } else {
         return ISL_AUX_USAGE_NONE;
      }

   case ISL_AUX_STATE_PASS_THROUGH:
   case ISL_AUX_STATE_AUX_INVALID:
      return ISL_AUX_USAGE_NONE;
   }

   unreachable("Invalid isl_aux_state");
}

/**
 * This function returns the level of unresolved fast-clear support of the
 * given image in the given VkImageLayout.
 *
 * @param devinfo The device information of the Intel GPU.
 * @param image The image that may contain a collection of buffers.
 * @param aspect The aspect of the image to be accessed.
 * @param usage The usage which describes how the image will be accessed.
 * @param layout The current layout of the image aspect(s).
 */
enum anv_fast_clear_type ATTRIBUTE_PURE
anv_layout_to_fast_clear_type(const struct intel_device_info * const devinfo,
                              const struct anv_image * const image,
                              const VkImageAspectFlagBits aspect,
                              const VkImageLayout layout)
{
   if (INTEL_DEBUG(DEBUG_NO_FAST_CLEAR))
      return ANV_FAST_CLEAR_NONE;

   const uint32_t plane = anv_image_aspect_to_plane(image, aspect);

   /* If there is no auxiliary surface allocated, there are no fast-clears */
   if (image->planes[plane].aux_usage == ISL_AUX_USAGE_NONE)
      return ANV_FAST_CLEAR_NONE;

   enum isl_aux_state aux_state =
      anv_layout_to_aux_state(devinfo, image, aspect, layout);

   const VkImageUsageFlags layout_usage =
      vk_image_layout_to_usage_flags(layout, aspect);

   switch (aux_state) {
   case ISL_AUX_STATE_CLEAR:
      unreachable("We never use this state");

   case ISL_AUX_STATE_PARTIAL_CLEAR:
   case ISL_AUX_STATE_COMPRESSED_CLEAR:
      if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
         return ANV_FAST_CLEAR_DEFAULT_VALUE;
      } else if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
                 layout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL) {
         /* The image might not support non zero fast clears when mutable. */
         if (!image->planes[plane].can_non_zero_fast_clear)
            return ANV_FAST_CLEAR_DEFAULT_VALUE;

         /* When we're in a render pass we have the clear color data from the
          * VkRenderPassBeginInfo and we can use arbitrary clear colors.  They
          * must get partially resolved before we leave the render pass.
          */
         return ANV_FAST_CLEAR_ANY;
      } else if (layout_usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
         /* Fast clear with non zero color is not supported during transfer
          * operations since transfer may do format reinterpretation.
          */
         return ANV_FAST_CLEAR_DEFAULT_VALUE;
      } else if (image->planes[plane].aux_usage == ISL_AUX_USAGE_MCS ||
                 image->planes[plane].aux_usage == ISL_AUX_USAGE_CCS_E ||
                 image->planes[plane].aux_usage == ISL_AUX_USAGE_FCV_CCS_E) {
         if (devinfo->ver >= 11) {
            /* The image might not support non zero fast clears when mutable. */
            if (!image->planes[plane].can_non_zero_fast_clear)
               return ANV_FAST_CLEAR_DEFAULT_VALUE;

            /* On ICL and later, the sampler hardware uses a copy of the clear
             * value that is encoded as a pixel value.  Therefore, we can use
             * any clear color we like for sampling.
             */
            return ANV_FAST_CLEAR_ANY;
         } else {
            /* If the image has MCS or CCS_E enabled all the time then we can
             * use fast-clear as long as the clear color is the default value
             * of zero since this is the default value we program into every
             * surface state used for texturing.
             */
            return ANV_FAST_CLEAR_DEFAULT_VALUE;
         }
      } else {
         return ANV_FAST_CLEAR_NONE;
      }

   case ISL_AUX_STATE_COMPRESSED_NO_CLEAR:
   case ISL_AUX_STATE_RESOLVED:
   case ISL_AUX_STATE_PASS_THROUGH:
   case ISL_AUX_STATE_AUX_INVALID:
      return ANV_FAST_CLEAR_NONE;
   }

   unreachable("Invalid isl_aux_state");
}


/**
 * This function determines if the layout & usage of an image can have
 * untracked aux writes. When we see a transition that matches this criteria,
 * we need to mark the image as compressed written so that our predicated
 * resolves work properly.
 *
 * @param devinfo The device information of the Intel GPU.
 * @param image The image that may contain a collection of buffers.
 * @param aspect The aspect of the image to be accessed.
 * @param layout The current layout of the image aspect(s).
 */
bool
anv_layout_has_untracked_aux_writes(const struct intel_device_info * const devinfo,
                                    const struct anv_image * const image,
                                    const VkImageAspectFlagBits aspect,
                                    const VkImageLayout layout)
{
   const VkImageUsageFlags image_aspect_usage =
      vk_image_usage(&image->vk, aspect);
   const VkImageUsageFlags usage =
      vk_image_layout_to_usage_flags(layout, aspect) & image_aspect_usage;

   /* Storage is the only usage where we do not write the image through a
    * render target but through a descriptor. Since VK_EXT_descriptor_indexing
    * and the update-after-bind feature, it has become impossible to track
    * writes to images in descriptor at the command buffer build time. So it's
    * not possible to mark an image as compressed like we do in
    * genX_cmd_buffer.c(EndRendering) or anv_blorp.c for all transfer
    * operations.
    */
   if (!(usage & VK_IMAGE_USAGE_STORAGE_BIT))
      return false;

   /* No AUX, no writes to the AUX surface :) */
   const uint32_t plane = anv_image_aspect_to_plane(image, aspect);
   const enum isl_aux_usage aux_usage = image->planes[plane].aux_usage;
   if (aux_usage == ISL_AUX_USAGE_NONE)
      return false;

   return true;
}

static struct anv_state
maybe_alloc_surface_state(struct anv_device *device)
{
   if (device->physical->indirect_descriptors) {
      return anv_state_pool_alloc(&device->bindless_surface_state_pool, 64, 64);
   } else {
      return ANV_STATE_NULL;
   }
}

static enum isl_channel_select
remap_swizzle(VkComponentSwizzle swizzle,
              struct isl_swizzle format_swizzle)
{
   switch (swizzle) {
   case VK_COMPONENT_SWIZZLE_ZERO:  return ISL_CHANNEL_SELECT_ZERO;
   case VK_COMPONENT_SWIZZLE_ONE:   return ISL_CHANNEL_SELECT_ONE;
   case VK_COMPONENT_SWIZZLE_R:     return format_swizzle.r;
   case VK_COMPONENT_SWIZZLE_G:     return format_swizzle.g;
   case VK_COMPONENT_SWIZZLE_B:     return format_swizzle.b;
   case VK_COMPONENT_SWIZZLE_A:     return format_swizzle.a;
   default:
      unreachable("Invalid swizzle");
   }
}

void
anv_image_fill_surface_state(struct anv_device *device,
                             const struct anv_image *image,
                             VkImageAspectFlagBits aspect,
                             const struct isl_view *view_in,
                             isl_surf_usage_flags_t view_usage,
                             enum isl_aux_usage aux_usage,
                             const union isl_color_value *clear_color,
                             enum anv_image_view_state_flags flags,
                             struct anv_surface_state *state_inout)
{
   const uint32_t plane = anv_image_aspect_to_plane(image, aspect);

   const struct anv_surface *surface = &image->planes[plane].primary_surface,
      *aux_surface = &image->planes[plane].aux_surface;

   struct isl_view view = *view_in;
   view.usage |= view_usage;

   if (view_usage == ISL_SURF_USAGE_RENDER_TARGET_BIT)
      view.swizzle = anv_swizzle_for_render(view.swizzle);

   /* If this is a HiZ buffer we can sample from with a programmable clear
    * value (SKL+), define the clear value to the optimal constant.
    */
   union isl_color_value default_clear_color = { .u32 = { 0, } };
   if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
      default_clear_color.f32[0] = ANV_HZ_FC_VAL;
   if (!clear_color)
      clear_color = &default_clear_color;

   const struct anv_address address =
      anv_image_address(image, &surface->memory_range);

   void *surface_state_map = state_inout->state_data.data;

   const struct isl_surf *isl_surf = &surface->isl;

   struct isl_surf tmp_surf;
   uint64_t offset_B = 0;
   uint32_t tile_x_sa = 0, tile_y_sa = 0;
   if (isl_format_is_compressed(surface->isl.format) &&
       !isl_format_is_compressed(view.format)) {
      /* We're creating an uncompressed view of a compressed surface. This is
       * allowed but only for a single level/layer.
       */
      assert(surface->isl.samples == 1);
      assert(view.levels == 1);
      assert(surface->isl.dim == ISL_SURF_DIM_3D || view.array_len == 1);

      ASSERTED bool ok =
         isl_surf_get_uncompressed_surf(&device->isl_dev, isl_surf, &view,
                                        &tmp_surf, &view,
                                        &offset_B, &tile_x_sa, &tile_y_sa);
      assert(ok);
      isl_surf = &tmp_surf;
   }

   state_inout->address = anv_address_add(address, offset_B);

   struct anv_address aux_address = ANV_NULL_ADDRESS;
   if (aux_usage != ISL_AUX_USAGE_NONE)
      aux_address = anv_image_address(image, &aux_surface->memory_range);
   state_inout->aux_address = aux_address;

   struct anv_address clear_address = ANV_NULL_ADDRESS;
   if (device->info->ver >= 10 && isl_aux_usage_has_fast_clears(aux_usage)) {
      clear_address = anv_image_get_clear_color_addr(device, image, aspect);
   }
   state_inout->clear_address = clear_address;

   isl_surf_fill_state(&device->isl_dev, surface_state_map,
                       .surf = isl_surf,
                       .view = &view,
                       .address = anv_address_physical(state_inout->address),
                       .clear_color = *clear_color,
                       .aux_surf = &aux_surface->isl,
                       .aux_usage = aux_usage,
                       .aux_address = anv_address_physical(aux_address),
                       .clear_address = anv_address_physical(clear_address),
                       .use_clear_address = !anv_address_is_null(clear_address),
                       .mocs = anv_mocs(device, state_inout->address.bo,
                                        view_usage),
                       .x_offset_sa = tile_x_sa,
                       .y_offset_sa = tile_y_sa,
                       .robust_image_access =
                          device->vk.enabled_features.robustImageAccess ||
                          device->vk.enabled_features.robustImageAccess2);

   /* With the exception of gfx8, the bottom 12 bits of the MCS base address
    * are used to store other information. This should be ok, however, because
    * the surface buffer addresses are always 4K page aligned.
    */
   if (!anv_address_is_null(aux_address)) {
      uint32_t *aux_addr_dw = surface_state_map +
         device->isl_dev.ss.aux_addr_offset;
      assert((aux_address.offset & 0xfff) == 0);
      state_inout->aux_address.offset |= *aux_addr_dw & 0xfff;
   }

   if (device->info->ver >= 10 && clear_address.bo) {
      uint32_t *clear_addr_dw = surface_state_map +
         device->isl_dev.ss.clear_color_state_offset;
      assert((clear_address.offset & 0x3f) == 0);
      state_inout->clear_address.offset |= *clear_addr_dw & 0x3f;
   }

   if (state_inout->state.map)
      memcpy(state_inout->state.map, surface_state_map, ANV_SURFACE_STATE_SIZE);
}

static uint32_t
anv_image_aspect_get_planes(VkImageAspectFlags aspect_mask)
{
   anv_assert_valid_aspect_set(aspect_mask);
   return util_bitcount(aspect_mask);
}

bool
anv_can_hiz_clear_ds_view(struct anv_device *device,
                          const struct anv_image_view *iview,
                          VkImageLayout layout,
                          VkImageAspectFlags clear_aspects,
                          float depth_clear_value,
                          VkRect2D render_area)
{
   /* If we're just clearing stencil, we can always HiZ clear */
   if (!(clear_aspects & VK_IMAGE_ASPECT_DEPTH_BIT))
      return true;

   /* We must have depth in order to have HiZ */
   if (!(iview->image->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT))
      return false;

   const enum isl_aux_usage clear_aux_usage =
      anv_layout_to_aux_usage(device->info, iview->image,
                              VK_IMAGE_ASPECT_DEPTH_BIT,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                              layout);
   if (!blorp_can_hiz_clear_depth(device->info,
                                  &iview->image->planes[0].primary_surface.isl,
                                  clear_aux_usage,
                                  iview->planes[0].isl.base_level,
                                  iview->planes[0].isl.base_array_layer,
                                  render_area.offset.x,
                                  render_area.offset.y,
                                  render_area.offset.x +
                                  render_area.extent.width,
                                  render_area.offset.y +
                                  render_area.extent.height))
      return false;

   if (depth_clear_value != ANV_HZ_FC_VAL)
      return false;

   /* If we got here, then we can fast clear */
   return true;
}

static bool
isl_color_value_requires_conversion(union isl_color_value color,
                                    const struct isl_surf *surf,
                                    const struct isl_view *view)
{
   if (surf->format == view->format && isl_swizzle_is_identity(view->swizzle))
      return false;

   uint32_t surf_pack[4] = { 0, 0, 0, 0 };
   isl_color_value_pack(&color, surf->format, surf_pack);

   uint32_t view_pack[4] = { 0, 0, 0, 0 };
   union isl_color_value swiz_color =
      isl_color_value_swizzle_inv(color, view->swizzle);
   isl_color_value_pack(&swiz_color, view->format, view_pack);

   return memcmp(surf_pack, view_pack, sizeof(surf_pack)) != 0;
}

bool
anv_can_fast_clear_color_view(struct anv_device *device,
                              struct anv_image_view *iview,
                              VkImageLayout layout,
                              union isl_color_value clear_color,
                              uint32_t num_layers,
                              VkRect2D render_area)
{
   if (iview->planes[0].isl.base_array_layer >=
       anv_image_aux_layers(iview->image, VK_IMAGE_ASPECT_COLOR_BIT,
                            iview->planes[0].isl.base_level))
      return false;

   /* Start by getting the fast clear type.  We use the first subpass
    * layout here because we don't want to fast-clear if the first subpass
    * to use the attachment can't handle fast-clears.
    */
   enum anv_fast_clear_type fast_clear_type =
      anv_layout_to_fast_clear_type(device->info, iview->image,
                                    VK_IMAGE_ASPECT_COLOR_BIT,
                                    layout);
   switch (fast_clear_type) {
   case ANV_FAST_CLEAR_NONE:
      return false;
   case ANV_FAST_CLEAR_DEFAULT_VALUE:
      if (!isl_color_value_is_zero(clear_color, iview->planes[0].isl.format))
         return false;
      break;
   case ANV_FAST_CLEAR_ANY:
      break;
   }

   /* Potentially, we could do partial fast-clears but doing so has crazy
    * alignment restrictions.  It's easier to just restrict to full size
    * fast clears for now.
    */
   if (render_area.offset.x != 0 ||
       render_area.offset.y != 0 ||
       render_area.extent.width != iview->vk.extent.width ||
       render_area.extent.height != iview->vk.extent.height)
      return false;

   /* If the clear color is one that would require non-trivial format
    * conversion on resolve, we don't bother with the fast clear.  This
    * shouldn't be common as most clear colors are 0/1 and the most common
    * format re-interpretation is for sRGB.
    */
   if (isl_color_value_requires_conversion(clear_color,
                                           &iview->image->planes[0].primary_surface.isl,
                                           &iview->planes[0].isl)) {
      anv_perf_warn(VK_LOG_OBJS(&iview->vk.base),
                    "Cannot fast-clear to colors which would require "
                    "format conversion on resolve");
      return false;
   }

   /* We only allow fast clears to the first slice of an image (level 0,
    * layer 0) and only for the entire slice.  This guarantees us that, at
    * any given time, there is only one clear color on any given image at
    * any given time.  At the time of our testing (Jan 17, 2018), there
    * were no known applications which would benefit from fast-clearing
    * more than just the first slice.
    */
   if (iview->planes[0].isl.base_level > 0 ||
       iview->planes[0].isl.base_array_layer > 0) {
      anv_perf_warn(VK_LOG_OBJS(&iview->image->vk.base),
                    "Rendering with multi-lod or multi-layer framebuffer "
                    "with LOAD_OP_LOAD and baseMipLevel > 0 or "
                    "baseArrayLayer > 0.  Not fast clearing.");
      return false;
   }

   if (num_layers > 1) {
      anv_perf_warn(VK_LOG_OBJS(&iview->image->vk.base),
                    "Rendering to a multi-layer framebuffer with "
                    "LOAD_OP_CLEAR.  Only fast-clearing the first slice");
   }

   return true;
}

VkResult
anv_CreateImageView(VkDevice _device,
                    const VkImageViewCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkImageView *pView)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, image, pCreateInfo->image);
   struct anv_image_view *iview;

   iview = vk_image_view_create(&device->vk, false, pCreateInfo,
                                pAllocator, sizeof(*iview));
   if (iview == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   iview->image = image;
   iview->n_planes = anv_image_aspect_get_planes(iview->vk.aspects);

   /* Now go through the underlying image selected planes and map them to
    * planes in the image view.
    */
   anv_foreach_image_aspect_bit(iaspect_bit, image, iview->vk.aspects) {
      const uint32_t iplane =
         anv_aspect_to_plane(image->vk.aspects, 1UL << iaspect_bit);
      const uint32_t vplane =
         anv_aspect_to_plane(iview->vk.aspects, 1UL << iaspect_bit);
      struct anv_format_plane format;
      format = anv_get_format_plane(device->info, iview->vk.view_format,
                                    vplane, image->vk.tiling);

      iview->planes[vplane].image_plane = iplane;

      iview->planes[vplane].isl = (struct isl_view) {
         .format = format.isl_format,
         .base_level = iview->vk.base_mip_level,
         .levels = iview->vk.level_count,
         .base_array_layer = iview->vk.base_array_layer,
         .array_len = iview->vk.layer_count,
         .min_lod_clamp = iview->vk.min_lod,
         .swizzle = {
            .r = remap_swizzle(iview->vk.swizzle.r, format.swizzle),
            .g = remap_swizzle(iview->vk.swizzle.g, format.swizzle),
            .b = remap_swizzle(iview->vk.swizzle.b, format.swizzle),
            .a = remap_swizzle(iview->vk.swizzle.a, format.swizzle),
         },
      };

      if (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_3D) {
         iview->planes[vplane].isl.base_array_layer = 0;
         iview->planes[vplane].isl.array_len = iview->vk.extent.depth;
      }

      if (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_CUBE ||
          pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
         iview->planes[vplane].isl.usage = ISL_SURF_USAGE_CUBE_BIT;
      } else {
         iview->planes[vplane].isl.usage = 0;
      }

      if (iview->vk.usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
         iview->planes[vplane].optimal_sampler.state =
            maybe_alloc_surface_state(device);
         iview->planes[vplane].general_sampler.state =
            maybe_alloc_surface_state(device);

         enum isl_aux_usage general_aux_usage =
            anv_layout_to_aux_usage(device->info, image, 1UL << iaspect_bit,
                                    VK_IMAGE_USAGE_SAMPLED_BIT,
                                    VK_IMAGE_LAYOUT_GENERAL);
         enum isl_aux_usage optimal_aux_usage =
            anv_layout_to_aux_usage(device->info, image, 1UL << iaspect_bit,
                                    VK_IMAGE_USAGE_SAMPLED_BIT,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

         anv_image_fill_surface_state(device, image, 1ULL << iaspect_bit,
                                      &iview->planes[vplane].isl,
                                      ISL_SURF_USAGE_TEXTURE_BIT,
                                      optimal_aux_usage, NULL,
                                      ANV_IMAGE_VIEW_STATE_TEXTURE_OPTIMAL,
                                      &iview->planes[vplane].optimal_sampler);

         anv_image_fill_surface_state(device, image, 1ULL << iaspect_bit,
                                      &iview->planes[vplane].isl,
                                      ISL_SURF_USAGE_TEXTURE_BIT,
                                      general_aux_usage, NULL,
                                      0,
                                      &iview->planes[vplane].general_sampler);
      }

      /* NOTE: This one needs to go last since it may stomp isl_view.format */
      if (iview->vk.usage & VK_IMAGE_USAGE_STORAGE_BIT) {
         struct isl_view storage_view = iview->planes[vplane].isl;
         if (iview->vk.view_type == VK_IMAGE_VIEW_TYPE_3D) {
            storage_view.base_array_layer = iview->vk.storage.z_slice_offset;
            storage_view.array_len = iview->vk.storage.z_slice_count;
         }

         enum isl_aux_usage general_aux_usage =
            anv_layout_to_aux_usage(device->info, image, 1UL << iaspect_bit,
                                    VK_IMAGE_USAGE_STORAGE_BIT,
                                    VK_IMAGE_LAYOUT_GENERAL);
         iview->planes[vplane].storage.state =
            maybe_alloc_surface_state(device);

         anv_image_fill_surface_state(device, image, 1ULL << iaspect_bit,
                                      &storage_view,
                                      ISL_SURF_USAGE_STORAGE_BIT,
                                      general_aux_usage, NULL,
                                      0,
                                      &iview->planes[vplane].storage);
      }
   }

   *pView = anv_image_view_to_handle(iview);

   return VK_SUCCESS;
}

void
anv_DestroyImageView(VkDevice _device, VkImageView _iview,
                     const VkAllocationCallbacks *pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image_view, iview, _iview);

   if (!iview)
      return;

   for (uint32_t plane = 0; plane < iview->n_planes; plane++) {
      if (iview->planes[plane].optimal_sampler.state.alloc_size) {
         anv_state_pool_free(&device->bindless_surface_state_pool,
                             iview->planes[plane].optimal_sampler.state);
      }

      if (iview->planes[plane].general_sampler.state.alloc_size) {
         anv_state_pool_free(&device->bindless_surface_state_pool,
                             iview->planes[plane].general_sampler.state);
      }

      if (iview->planes[plane].storage.state.alloc_size) {
         anv_state_pool_free(&device->bindless_surface_state_pool,
                             iview->planes[plane].storage.state);
      }
   }

   vk_image_view_destroy(&device->vk, pAllocator, &iview->vk);
}

static void
anv_fill_buffer_view_surface_state(struct anv_device *device,
                                   struct anv_buffer_state *state,
                                   enum isl_format format,
                                   struct isl_swizzle swizzle,
                                   isl_surf_usage_flags_t usage,
                                   struct anv_address address,
                                   uint32_t range, uint32_t stride)
{
   anv_fill_buffer_surface_state(device,
                                 state->state_data.data,
                                 format, swizzle,
                                 ISL_SURF_USAGE_TEXTURE_BIT,
                                 address, range, stride);

   if (state->state.map)
      memcpy(state->state.map, state->state_data.data, ANV_SURFACE_STATE_SIZE);
}

VkResult
anv_CreateBufferView(VkDevice _device,
                     const VkBufferViewCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkBufferView *pView)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_buffer, buffer, pCreateInfo->buffer);
   struct anv_buffer_view *view;

   view = vk_object_alloc(&device->vk, pAllocator, sizeof(*view),
                          VK_OBJECT_TYPE_BUFFER_VIEW);
   if (!view)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct anv_format_plane format;
   format = anv_get_format_plane(device->info, pCreateInfo->format,
                                 0, VK_IMAGE_TILING_LINEAR);

   const uint32_t format_bs = isl_format_get_layout(format.isl_format)->bpb / 8;
   view->range = vk_buffer_range(&buffer->vk, pCreateInfo->offset,
                                              pCreateInfo->range);
   view->range = align_down_npot_u32(view->range, format_bs);

   view->address = anv_address_add(buffer->address, pCreateInfo->offset);

   if (buffer->vk.usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) {
      view->general.state = maybe_alloc_surface_state(device);

      anv_fill_buffer_view_surface_state(device,
                                         &view->general,
                                         format.isl_format,
                                         format.swizzle,
                                         ISL_SURF_USAGE_TEXTURE_BIT,
                                         view->address, view->range, format_bs);
   } else {
      view->general.state = ANV_STATE_NULL;
   }

   if (buffer->vk.usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) {
      view->storage.state = maybe_alloc_surface_state(device);

      anv_fill_buffer_view_surface_state(device,
                                         &view->storage,
                                         format.isl_format, format.swizzle,
                                         ISL_SURF_USAGE_STORAGE_BIT,
                                         view->address, view->range, format_bs);
   } else {
      view->storage.state = ANV_STATE_NULL;
   }

   *pView = anv_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

void
anv_DestroyBufferView(VkDevice _device, VkBufferView bufferView,
                      const VkAllocationCallbacks *pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_buffer_view, view, bufferView);

   if (!view)
      return;

   if (view->general.state.alloc_size > 0) {
      anv_state_pool_free(&device->bindless_surface_state_pool,
                          view->general.state);
   }

   if (view->storage.state.alloc_size > 0) {
      anv_state_pool_free(&device->bindless_surface_state_pool,
                          view->storage.state);
   }

   vk_object_free(&device->vk, pAllocator, view);
}
