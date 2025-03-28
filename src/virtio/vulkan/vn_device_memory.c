/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_device_memory.h"

#include "venus-protocol/vn_protocol_driver_device_memory.h"
#include "venus-protocol/vn_protocol_driver_transport.h"

#include "vn_android.h"
#include "vn_buffer.h"
#include "vn_device.h"
#include "vn_image.h"
#include "vn_physical_device.h"

/* device memory commands */

static VkResult
vn_device_memory_pool_grow_alloc(struct vn_device *dev,
                                 uint32_t mem_type_index,
                                 VkDeviceSize size,
                                 struct vn_device_memory **out_mem)
{
   VkDevice dev_handle = vn_device_to_handle(dev);
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;
   struct vn_device_memory *mem = NULL;
   VkDeviceMemory mem_handle = VK_NULL_HANDLE;
   VkResult result;

   mem = vk_zalloc(alloc, sizeof(*mem), VN_DEFAULT_ALIGN,
                   VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!mem)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vn_object_base_init(&mem->base, VK_OBJECT_TYPE_DEVICE_MEMORY, &dev->base);
   mem->size = size;
   mem->type = dev->physical_device->memory_properties.memoryProperties
                  .memoryTypes[mem_type_index];

   mem_handle = vn_device_memory_to_handle(mem);
   result = vn_call_vkAllocateMemory(
      dev->instance, dev_handle,
      &(const VkMemoryAllocateInfo){
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = size,
         .memoryTypeIndex = mem_type_index,
      },
      NULL, &mem_handle);
   if (result != VK_SUCCESS) {
      mem_handle = VK_NULL_HANDLE;
      goto fail;
   }

   result = vn_renderer_bo_create_from_device_memory(
      dev->renderer, mem->size, mem->base.id, mem->type.propertyFlags, 0,
      &mem->base_bo);
   if (result != VK_SUCCESS) {
      assert(!mem->base_bo);
      goto fail;
   }

   result =
      vn_instance_submit_roundtrip(dev->instance, &mem->bo_roundtrip_seqno);
   if (result != VK_SUCCESS)
      goto fail;

   mem->bo_roundtrip_seqno_valid = true;
   *out_mem = mem;

   return VK_SUCCESS;

fail:
   if (mem->base_bo)
      vn_renderer_bo_unref(dev->renderer, mem->base_bo);
   if (mem_handle != VK_NULL_HANDLE)
      vn_async_vkFreeMemory(dev->instance, dev_handle, mem_handle, NULL);
   vn_object_base_fini(&mem->base);
   vk_free(alloc, mem);
   return result;
}

static struct vn_device_memory *
vn_device_memory_pool_ref(struct vn_device *dev,
                          struct vn_device_memory *pool_mem)
{
   assert(pool_mem->base_bo);

   vn_renderer_bo_ref(dev->renderer, pool_mem->base_bo);

   return pool_mem;
}

static void
vn_device_memory_pool_unref(struct vn_device *dev,
                            struct vn_device_memory *pool_mem)
{
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   assert(pool_mem->base_bo);

   if (!vn_renderer_bo_unref(dev->renderer, pool_mem->base_bo))
      return;

   /* wait on valid bo_roundtrip_seqno before vkFreeMemory */
   if (pool_mem->bo_roundtrip_seqno_valid)
      vn_instance_wait_roundtrip(dev->instance, pool_mem->bo_roundtrip_seqno);

   vn_async_vkFreeMemory(dev->instance, vn_device_to_handle(dev),
                         vn_device_memory_to_handle(pool_mem), NULL);
   vn_object_base_fini(&pool_mem->base);
   vk_free(alloc, pool_mem);
}

void
vn_device_memory_pool_fini(struct vn_device *dev, uint32_t mem_type_index)
{
   struct vn_device_memory_pool *pool = &dev->memory_pools[mem_type_index];
   if (pool->memory)
      vn_device_memory_pool_unref(dev, pool->memory);
   mtx_destroy(&pool->mutex);
}

static VkResult
vn_device_memory_pool_grow_locked(struct vn_device *dev,
                                  uint32_t mem_type_index,
                                  VkDeviceSize size)
{
   struct vn_device_memory *mem;
   VkResult result =
      vn_device_memory_pool_grow_alloc(dev, mem_type_index, size, &mem);
   if (result != VK_SUCCESS)
      return result;

   struct vn_device_memory_pool *pool = &dev->memory_pools[mem_type_index];
   if (pool->memory)
      vn_device_memory_pool_unref(dev, pool->memory);

   pool->memory = mem;
   pool->used = 0;

   return VK_SUCCESS;
}

static VkResult
vn_device_memory_pool_suballocate(struct vn_device *dev,
                                  struct vn_device_memory *mem,
                                  uint32_t mem_type_index)
{
   const VkDeviceSize pool_size = 16 * 1024 * 1024;
   /* TODO fix https://gitlab.freedesktop.org/mesa/mesa/-/issues/9351
    * Before that, we use 64K default alignment because some GPUs have 64K
    * pages. It is also required by newer Intel GPUs. Meanwhile, use prior 4K
    * align on implementations known to fit.
    */
   const VkDeviceSize pool_align =
      dev->physical_device->renderer_driver_id == VK_DRIVER_ID_ARM_PROPRIETARY
         ? 4096
         : 64 * 1024;
   struct vn_device_memory_pool *pool = &dev->memory_pools[mem_type_index];

   assert(mem->size <= pool_size);

   mtx_lock(&pool->mutex);

   if (!pool->memory || pool->used + mem->size > pool_size) {
      VkResult result =
         vn_device_memory_pool_grow_locked(dev, mem_type_index, pool_size);
      if (result != VK_SUCCESS) {
         mtx_unlock(&pool->mutex);
         return result;
      }
   }

   mem->base_memory = vn_device_memory_pool_ref(dev, pool->memory);

   /* point mem->base_bo at pool base_bo and assign base_offset accordingly */
   mem->base_bo = pool->memory->base_bo;
   mem->base_offset = pool->used;
   pool->used += align64(mem->size, pool_align);

   mtx_unlock(&pool->mutex);

   return VK_SUCCESS;
}

static bool
vn_device_memory_should_suballocate(const struct vn_device *dev,
                                    const VkMemoryAllocateInfo *alloc_info,
                                    const VkMemoryPropertyFlags flags)
{
   const struct vn_instance *instance = dev->physical_device->instance;
   const struct vn_renderer_info *renderer = &instance->renderer->info;

   if (VN_PERF(NO_MEMORY_SUBALLOC))
      return false;

   if (renderer->has_guest_vram)
      return false;

   /* We should not support suballocations because apps can do better.  But
    * each BO takes up a KVM memslot currently and some CTS tests exhausts
    * them.  This might not be needed on newer (host) kernels where there are
    * many more KVM memslots.
    */

   /* consider host-visible memory only */
   if (!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
      return false;

   /* reject larger allocations */
   if (alloc_info->allocationSize > 64 * 1024)
      return false;

   /* reject if there is any pnext struct other than
    * VkMemoryDedicatedAllocateInfo, or if dedicated allocation is required
    */
   if (alloc_info->pNext) {
      const VkMemoryDedicatedAllocateInfo *dedicated = alloc_info->pNext;
      if (dedicated->sType !=
             VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO ||
          dedicated->pNext)
         return false;

      const struct vn_image *img = vn_image_from_handle(dedicated->image);
      if (img) {
         for (uint32_t i = 0; i < ARRAY_SIZE(img->requirements); i++) {
            if (img->requirements[i].dedicated.requiresDedicatedAllocation)
               return false;
         }
      }

      const struct vn_buffer *buf = vn_buffer_from_handle(dedicated->buffer);
      if (buf && buf->requirements.dedicated.requiresDedicatedAllocation)
         return false;
   }

   return true;
}

VkResult
vn_device_memory_import_dma_buf(struct vn_device *dev,
                                struct vn_device_memory *mem,
                                const VkMemoryAllocateInfo *alloc_info,
                                bool force_unmappable,
                                int fd)
{
   VkDevice device = vn_device_to_handle(dev);
   VkDeviceMemory memory = vn_device_memory_to_handle(mem);
   const VkPhysicalDeviceMemoryProperties *mem_props =
      &dev->physical_device->memory_properties.memoryProperties;
   VkMemoryPropertyFlags mem_flags =
      mem_props->memoryTypes[alloc_info->memoryTypeIndex].propertyFlags;
   struct vn_renderer_bo *bo;
   VkResult result = VK_SUCCESS;

   if (force_unmappable)
      mem_flags &= ~VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

   result = vn_renderer_bo_create_from_dma_buf(
      dev->renderer, alloc_info->allocationSize, fd, mem_flags, &bo);
   if (result != VK_SUCCESS)
      return result;

   vn_instance_roundtrip(dev->instance);

   /* XXX fix VkImportMemoryResourceInfoMESA to support memory planes */
   const VkImportMemoryResourceInfoMESA import_memory_resource_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_RESOURCE_INFO_MESA,
      .pNext = alloc_info->pNext,
      .resourceId = bo->res_id,
   };
   const VkMemoryAllocateInfo memory_allocate_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &import_memory_resource_info,
      .allocationSize = alloc_info->allocationSize,
      .memoryTypeIndex = alloc_info->memoryTypeIndex,
   };
   result = vn_call_vkAllocateMemory(dev->instance, device,
                                     &memory_allocate_info, NULL, &memory);
   if (result != VK_SUCCESS) {
      vn_renderer_bo_unref(dev->renderer, bo);
      return result;
   }

   /* need to close import fd on success to avoid fd leak */
   close(fd);
   mem->base_bo = bo;

   return VK_SUCCESS;
}

static VkResult
vn_device_memory_alloc_guest_vram(
   struct vn_device *dev,
   struct vn_device_memory *mem,
   const VkMemoryAllocateInfo *alloc_info,
   VkExternalMemoryHandleTypeFlags external_handles)
{
   VkDevice dev_handle = vn_device_to_handle(dev);
   VkDeviceMemory mem_handle = vn_device_memory_to_handle(mem);
   VkResult result = VK_SUCCESS;

   result = vn_renderer_bo_create_from_device_memory(
      dev->renderer, mem->size, 0, mem->type.propertyFlags, external_handles,
      &mem->base_bo);
   if (result != VK_SUCCESS) {
      return result;
   }

   const VkImportMemoryResourceInfoMESA import_memory_resource_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_RESOURCE_INFO_MESA,
      .pNext = alloc_info->pNext,
      .resourceId = mem->base_bo->res_id,
   };

   const VkMemoryAllocateInfo memory_allocate_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &import_memory_resource_info,
      .allocationSize = alloc_info->allocationSize,
      .memoryTypeIndex = alloc_info->memoryTypeIndex,
   };

   vn_instance_roundtrip(dev->instance);

   result = vn_call_vkAllocateMemory(
      dev->instance, dev_handle, &memory_allocate_info, NULL, &mem_handle);
   if (result != VK_SUCCESS) {
      vn_renderer_bo_unref(dev->renderer, mem->base_bo);
      return result;
   }

   result =
      vn_instance_submit_roundtrip(dev->instance, &mem->bo_roundtrip_seqno);
   if (result != VK_SUCCESS) {
      vn_renderer_bo_unref(dev->renderer, mem->base_bo);
      vn_async_vkFreeMemory(dev->instance, dev_handle, mem_handle, NULL);
      return result;
   }

   mem->bo_roundtrip_seqno_valid = true;

   return VK_SUCCESS;
}

static VkResult
vn_device_memory_alloc_generic(
   struct vn_device *dev,
   struct vn_device_memory *mem,
   const VkMemoryAllocateInfo *alloc_info,
   VkExternalMemoryHandleTypeFlags external_handles)
{
   VkDevice dev_handle = vn_device_to_handle(dev);
   VkDeviceMemory mem_handle = vn_device_memory_to_handle(mem);
   VkResult result = VK_SUCCESS;

   result = vn_call_vkAllocateMemory(dev->instance, dev_handle, alloc_info,
                                     NULL, &mem_handle);
   if (result != VK_SUCCESS || !external_handles)
      return result;

   result = vn_renderer_bo_create_from_device_memory(
      dev->renderer, mem->size, mem->base.id, mem->type.propertyFlags,
      external_handles, &mem->base_bo);
   if (result != VK_SUCCESS) {
      vn_async_vkFreeMemory(dev->instance, dev_handle, mem_handle, NULL);
      return result;
   }

   result =
      vn_instance_submit_roundtrip(dev->instance, &mem->bo_roundtrip_seqno);
   if (result != VK_SUCCESS) {
      vn_renderer_bo_unref(dev->renderer, mem->base_bo);
      vn_async_vkFreeMemory(dev->instance, dev_handle, mem_handle, NULL);
      return result;
   }

   mem->bo_roundtrip_seqno_valid = true;

   return VK_SUCCESS;
}

struct vn_device_memory_alloc_info {
   VkMemoryAllocateInfo alloc;
   VkExportMemoryAllocateInfo export;
   VkMemoryAllocateFlagsInfo flags;
   VkMemoryDedicatedAllocateInfo dedicated;
   VkMemoryOpaqueCaptureAddressAllocateInfo capture;
};

static const VkMemoryAllocateInfo *
vn_device_memory_fix_alloc_info(
   const VkMemoryAllocateInfo *alloc_info,
   const VkExternalMemoryHandleTypeFlagBits renderer_handle_type,
   bool has_guest_vram,
   struct vn_device_memory_alloc_info *local_info)
{
   local_info->alloc = *alloc_info;
   VkBaseOutStructure *cur = (void *)&local_info->alloc;

   vk_foreach_struct_const(src, alloc_info->pNext) {
      void *next = NULL;
      switch (src->sType) {
      case VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO:
         /* guest vram turns export alloc into import, so drop export info */
         if (has_guest_vram)
            break;
         memcpy(&local_info->export, src, sizeof(local_info->export));
         local_info->export.handleTypes = renderer_handle_type;
         next = &local_info->export;
         break;
      case VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO:
         memcpy(&local_info->flags, src, sizeof(local_info->flags));
         next = &local_info->flags;
         break;
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO:
         memcpy(&local_info->dedicated, src, sizeof(local_info->dedicated));
         next = &local_info->dedicated;
         break;
      case VK_STRUCTURE_TYPE_MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO:
         memcpy(&local_info->capture, src, sizeof(local_info->capture));
         next = &local_info->capture;
         break;
      default:
         break;
      }

      if (next) {
         cur->pNext = next;
         cur = next;
      }
   }

   cur->pNext = NULL;

   return &local_info->alloc;
}

static VkResult
vn_device_memory_alloc(struct vn_device *dev,
                       struct vn_device_memory *mem,
                       const VkMemoryAllocateInfo *alloc_info,
                       VkExternalMemoryHandleTypeFlags external_handles)
{
   const struct vn_instance *instance = dev->physical_device->instance;
   const struct vn_renderer_info *renderer_info = &instance->renderer->info;

   const VkExternalMemoryHandleTypeFlagBits renderer_handle_type =
      dev->physical_device->external_memory.renderer_handle_type;
   struct vn_device_memory_alloc_info local_info;
   if (external_handles && external_handles != renderer_handle_type) {
      alloc_info = vn_device_memory_fix_alloc_info(
         alloc_info, renderer_handle_type, renderer_info->has_guest_vram,
         &local_info);

      /* ensure correct blob flags */
      external_handles = renderer_handle_type;
   }

   if (renderer_info->has_guest_vram) {
      return vn_device_memory_alloc_guest_vram(dev, mem, alloc_info,
                                               external_handles);
   }

   return vn_device_memory_alloc_generic(dev, mem, alloc_info,
                                         external_handles);
}

static void
vn_device_memory_emit_report(struct vn_device *dev,
                             struct vn_device_memory *mem,
                             bool is_alloc,
                             VkResult result)
{
   if (likely(!dev->memory_reports))
      return;

   VkDeviceMemoryReportEventTypeEXT type;
   if (result != VK_SUCCESS) {
      type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATION_FAILED_EXT;
   } else if (is_alloc) {
      type = mem->is_import ? VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_IMPORT_EXT
                            : VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT;
   } else {
      type = mem->is_import ? VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_UNIMPORT_EXT
                            : VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT;
   }
   const uint64_t mem_obj_id =
      mem->is_external ? mem->base_bo->res_id : mem->base.id;
   vn_device_emit_device_memory_report(dev, type, mem_obj_id, mem->size,
                                       &mem->base, mem->type.heapIndex);
}

VkResult
vn_AllocateMemory(VkDevice device,
                  const VkMemoryAllocateInfo *pAllocateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkDeviceMemory *pMemory)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   const VkExportMemoryAllocateInfo *export_info = NULL;
   const VkImportAndroidHardwareBufferInfoANDROID *import_ahb_info = NULL;
   const VkImportMemoryFdInfoKHR *import_fd_info = NULL;
   bool export_ahb = false;
   vk_foreach_struct_const(pnext, pAllocateInfo->pNext) {
      switch (pnext->sType) {
      case VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO:
         export_info = (void *)pnext;
         if (export_info->handleTypes &
             VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID)
            export_ahb = true;
         else if (!export_info->handleTypes)
            export_info = NULL;
         break;
      case VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID:
         import_ahb_info = (void *)pnext;
         break;
      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR:
         import_fd_info = (void *)pnext;
         break;
      default:
         break;
      }
   }

   struct vn_device_memory *mem =
      vk_zalloc(alloc, sizeof(*mem), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!mem)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&mem->base, VK_OBJECT_TYPE_DEVICE_MEMORY, &dev->base);
   mem->size = pAllocateInfo->allocationSize;
   mem->type = dev->physical_device->memory_properties.memoryProperties
                  .memoryTypes[pAllocateInfo->memoryTypeIndex];
   mem->is_import = import_ahb_info || import_fd_info;
   mem->is_external = mem->is_import || export_info;

   VkDeviceMemory mem_handle = vn_device_memory_to_handle(mem);
   VkResult result;
   if (import_ahb_info) {
      result = vn_android_device_import_ahb(dev, mem, pAllocateInfo, alloc,
                                            import_ahb_info->buffer, false);
   } else if (export_ahb) {
      result = vn_android_device_allocate_ahb(dev, mem, pAllocateInfo, alloc);
   } else if (import_fd_info) {
      result = vn_device_memory_import_dma_buf(dev, mem, pAllocateInfo, false,
                                               import_fd_info->fd);
   } else if (export_info) {
      result = vn_device_memory_alloc(dev, mem, pAllocateInfo,
                                      export_info->handleTypes);
   } else if (vn_device_memory_should_suballocate(dev, pAllocateInfo,
                                                  mem->type.propertyFlags)) {
      result = vn_device_memory_pool_suballocate(
         dev, mem, pAllocateInfo->memoryTypeIndex);
   } else {
      result = vn_device_memory_alloc(dev, mem, pAllocateInfo, 0);
   }

   vn_device_memory_emit_report(dev, mem, /* is_alloc */ true, result);

   if (result != VK_SUCCESS) {
      vn_object_base_fini(&mem->base);
      vk_free(alloc, mem);
      return vn_error(dev->instance, result);
   }

   *pMemory = mem_handle;

   return VK_SUCCESS;
}

void
vn_FreeMemory(VkDevice device,
              VkDeviceMemory memory,
              const VkAllocationCallbacks *pAllocator)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_device_memory *mem = vn_device_memory_from_handle(memory);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!mem)
      return;

   vn_device_memory_emit_report(dev, mem, /* is_alloc */ false, VK_SUCCESS);

   if (mem->base_memory) {
      vn_device_memory_pool_unref(dev, mem->base_memory);
   } else {
      if (mem->base_bo)
         vn_renderer_bo_unref(dev->renderer, mem->base_bo);

      if (mem->bo_roundtrip_seqno_valid)
         vn_instance_wait_roundtrip(dev->instance, mem->bo_roundtrip_seqno);

      vn_async_vkFreeMemory(dev->instance, device, memory, NULL);
   }

   if (mem->ahb)
      vn_android_release_ahb(mem->ahb);

   vn_object_base_fini(&mem->base);
   vk_free(alloc, mem);
}

uint64_t
vn_GetDeviceMemoryOpaqueCaptureAddress(
   VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);
   ASSERTED struct vn_device_memory *mem =
      vn_device_memory_from_handle(pInfo->memory);

   assert(!mem->base_memory);
   return vn_call_vkGetDeviceMemoryOpaqueCaptureAddress(dev->instance, device,
                                                        pInfo);
}

VkResult
vn_MapMemory(VkDevice device,
             VkDeviceMemory memory,
             VkDeviceSize offset,
             VkDeviceSize size,
             VkMemoryMapFlags flags,
             void **ppData)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_device_memory *mem = vn_device_memory_from_handle(memory);
   const bool need_bo = !mem->base_bo;
   void *ptr = NULL;
   VkResult result;

   assert(mem->type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   /* We don't want to blindly create a bo for each HOST_VISIBLE memory as
    * that has a cost. By deferring bo creation until now, we can avoid the
    * cost unless a bo is really needed. However, that means
    * vn_renderer_bo_map will block until the renderer creates the resource
    * and injects the pages into the guest.
    *
    * XXX We also assume that a vn_renderer_bo can be created as long as the
    * renderer VkDeviceMemory has a mappable memory type.  That is plain
    * wrong.  It is impossible to fix though until some new extension is
    * created and supported by the driver, and that the renderer switches to
    * the extension.
    */
   if (need_bo) {
      result = vn_renderer_bo_create_from_device_memory(
         dev->renderer, mem->size, mem->base.id, mem->type.propertyFlags, 0,
         &mem->base_bo);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);
   }

   ptr = vn_renderer_bo_map(dev->renderer, mem->base_bo);
   if (!ptr) {
      /* vn_renderer_bo_map implies a roundtrip on success, but not here. */
      if (need_bo) {
         result = vn_instance_submit_roundtrip(dev->instance,
                                               &mem->bo_roundtrip_seqno);
         if (result != VK_SUCCESS)
            return vn_error(dev->instance, result);

         mem->bo_roundtrip_seqno_valid = true;
      }

      return vn_error(dev->instance, VK_ERROR_MEMORY_MAP_FAILED);
   }

   mem->map_end = size == VK_WHOLE_SIZE ? mem->size : offset + size;

   *ppData = ptr + mem->base_offset + offset;

   return VK_SUCCESS;
}

void
vn_UnmapMemory(VkDevice device, VkDeviceMemory memory)
{
   VN_TRACE_FUNC();
}

VkResult
vn_FlushMappedMemoryRanges(VkDevice device,
                           uint32_t memoryRangeCount,
                           const VkMappedMemoryRange *pMemoryRanges)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);

   for (uint32_t i = 0; i < memoryRangeCount; i++) {
      const VkMappedMemoryRange *range = &pMemoryRanges[i];
      struct vn_device_memory *mem =
         vn_device_memory_from_handle(range->memory);

      const VkDeviceSize size = range->size == VK_WHOLE_SIZE
                                   ? mem->map_end - range->offset
                                   : range->size;
      vn_renderer_bo_flush(dev->renderer, mem->base_bo,
                           mem->base_offset + range->offset, size);
   }

   return VK_SUCCESS;
}

VkResult
vn_InvalidateMappedMemoryRanges(VkDevice device,
                                uint32_t memoryRangeCount,
                                const VkMappedMemoryRange *pMemoryRanges)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);

   for (uint32_t i = 0; i < memoryRangeCount; i++) {
      const VkMappedMemoryRange *range = &pMemoryRanges[i];
      struct vn_device_memory *mem =
         vn_device_memory_from_handle(range->memory);

      const VkDeviceSize size = range->size == VK_WHOLE_SIZE
                                   ? mem->map_end - range->offset
                                   : range->size;
      vn_renderer_bo_invalidate(dev->renderer, mem->base_bo,
                                mem->base_offset + range->offset, size);
   }

   return VK_SUCCESS;
}

void
vn_GetDeviceMemoryCommitment(VkDevice device,
                             VkDeviceMemory memory,
                             VkDeviceSize *pCommittedMemoryInBytes)
{
   struct vn_device *dev = vn_device_from_handle(device);
   ASSERTED struct vn_device_memory *mem =
      vn_device_memory_from_handle(memory);

   assert(!mem->base_memory);
   vn_call_vkGetDeviceMemoryCommitment(dev->instance, device, memory,
                                       pCommittedMemoryInBytes);
}

VkResult
vn_GetMemoryFdKHR(VkDevice device,
                  const VkMemoryGetFdInfoKHR *pGetFdInfo,
                  int *pFd)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_device_memory *mem =
      vn_device_memory_from_handle(pGetFdInfo->memory);

   /* At the moment, we support only the below handle types. */
   assert(pGetFdInfo->handleType &
          (VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
           VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT));
   assert(!mem->base_memory && mem->base_bo);
   *pFd = vn_renderer_bo_export_dma_buf(dev->renderer, mem->base_bo);
   if (*pFd < 0)
      return vn_error(dev->instance, VK_ERROR_TOO_MANY_OBJECTS);

   return VK_SUCCESS;
}

VkResult
vn_get_memory_dma_buf_properties(struct vn_device *dev,
                                 int fd,
                                 uint64_t *out_alloc_size,
                                 uint32_t *out_mem_type_bits)
{
   VkDevice device = vn_device_to_handle(dev);

   struct vn_renderer_bo *bo;
   VkResult result = vn_renderer_bo_create_from_dma_buf(
      dev->renderer, 0 /* size */, fd, 0 /* flags */, &bo);
   if (result != VK_SUCCESS)
      return result;

   vn_instance_roundtrip(dev->instance);

   VkMemoryResourceAllocationSizePropertiesMESA alloc_size_props = {
      .sType =
         VK_STRUCTURE_TYPE_MEMORY_RESOURCE_ALLOCATION_SIZE_PROPERTIES_MESA,
   };
   VkMemoryResourcePropertiesMESA props = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_RESOURCE_PROPERTIES_MESA,
      .pNext = &alloc_size_props,
   };
   result = vn_call_vkGetMemoryResourcePropertiesMESA(dev->instance, device,
                                                      bo->res_id, &props);
   vn_renderer_bo_unref(dev->renderer, bo);
   if (result != VK_SUCCESS)
      return result;

   *out_alloc_size = alloc_size_props.allocationSize;
   *out_mem_type_bits = props.memoryTypeBits;

   return VK_SUCCESS;
}

VkResult
vn_GetMemoryFdPropertiesKHR(VkDevice device,
                            VkExternalMemoryHandleTypeFlagBits handleType,
                            int fd,
                            VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   uint64_t alloc_size = 0;
   uint32_t mem_type_bits = 0;
   VkResult result = VK_SUCCESS;

   if (handleType != VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT)
      return vn_error(dev->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   result =
      vn_get_memory_dma_buf_properties(dev, fd, &alloc_size, &mem_type_bits);
   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   pMemoryFdProperties->memoryTypeBits = mem_type_bits;

   return VK_SUCCESS;
}
