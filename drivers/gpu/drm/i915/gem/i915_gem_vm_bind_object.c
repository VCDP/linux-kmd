// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_gem_gtt.h"

static struct i915_vma *
i915_gem_vm_bind_lookup_vma(struct i915_address_space *vm,
			    struct drm_i915_gem_object *obj,
			    struct drm_i915_gem_vm_bind *va)
{
	struct i915_vma *vma;

	/* FIXME: Optimize lookup (use hashtable or tree) */
	list_for_each_entry(vma, &vm->vm_bind_list, vm_bind_link) {
		if (vma->va_start != va->start)
			continue;

		if (vma->obj != obj || vma->size != va->length)
			return ERR_PTR(-EINVAL);

		return vma;
	}

	return ERR_PTR(-EINVAL);
}

int i915_gem_vm_bind_obj(struct i915_address_space *vm,
			 struct drm_i915_gem_vm_bind *va,
			 struct drm_file *file)
{
	struct drm_i915_gem_object *obj;
	struct i915_ggtt_view view;
	struct i915_vma *vma;
	int ret = 0;

	obj = i915_gem_object_lookup(file, va->handle);
	if (!obj)
		return -ENOENT;

	va->start = intel_noncanonical_addr(INTEL_PPGTT_MSB(vm->i915),
					    va->start);
	if (va->flags & I915_GEM_VM_BIND_UNBIND) {
		mutex_lock(&vm->vm_bind_mutex);
		vma = i915_gem_vm_bind_lookup_vma(vm, obj, va);
		if (IS_ERR(vma))
			ret = PTR_ERR(vma);
		else if (i915_vma_is_pinned(vma))
			ret = -EBUSY;
		else
			list_del_init(&vma->vm_bind_link);
		mutex_unlock(&vm->vm_bind_mutex);
		if (!ret)
			__i915_vma_put(vma);

		goto put_obj;
	}

	view.type = I915_GGTT_VIEW_PARTIAL;
	view.partial.offset = va->offset >> PAGE_SHIFT;
	view.partial.size = va->length >> PAGE_SHIFT;
	vma = i915_vma_instance(obj, vm, &view);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto put_obj;
	}
	vma->va_start = va->start;
	i915_vma_set_persistent(vma);
	if (va->flags & I915_GEM_VM_BIND_CAPTURE)
		i915_vma_set_capture(vma);

	mutex_lock(&vm->vm_bind_mutex);
	if (i915_vm_is_active(vm)) {
		u64 pin_flags = va->start | PIN_OFFSET_FIXED | PIN_USER;

		ret = i915_vma_pin(vma, 0, 0, pin_flags);
		if (ret)
			goto put_obj;

		__i915_vma_unpin(vma);
	}
	list_add_tail(&vma->vm_bind_link, &vm->vm_bind_list);
	mutex_unlock(&vm->vm_bind_mutex);

put_obj:
	i915_gem_object_put(obj);
	return ret;
}
