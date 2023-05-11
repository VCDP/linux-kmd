// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_gem_gtt.h"

int i915_gem_vm_bind_svm_obj(struct i915_address_space *vm,
			     struct drm_i915_gem_vm_bind_va *va,
			     struct drm_file *file)
{
	struct i915_svm_obj *svm_obj, *tmp;
	struct drm_i915_gem_object *obj;
	int ret = 0;

	obj = i915_gem_object_lookup(file, va->handle);
	if (!obj)
		return -ENOENT;

	/* FIXME: Need to handle case with unending batch buffers */
	if (!(va->flags & I915_GEM_VM_BIND_UNBIND)) {
		svm_obj = kmalloc(sizeof(*svm_obj), GFP_KERNEL);
		if (!svm_obj) {
			ret = -ENOMEM;
			goto put_obj;
		}
		svm_obj->handle = va->handle;
		svm_obj->offset = va->start;
	}

	mutex_lock(&vm->mutex);
	if (!(va->flags & I915_GEM_VM_BIND_UNBIND)) {
		list_add(&svm_obj->link, &vm->svm_list);
		vm->svm_count++;
	} else {
		/*
		 * FIXME: Need to handle case where object is migrated/closed
		 * without unbinding first.
		 */
		list_for_each_entry_safe(svm_obj, tmp, &vm->svm_list, link) {
			if (svm_obj->handle != va->handle)
				continue;

			list_del_init(&svm_obj->link);
			vm->svm_count--;
			kfree(svm_obj);
			break;
		}
	}
	mutex_unlock(&vm->mutex);
put_obj:
	i915_gem_object_put(obj);
	return ret;
}
