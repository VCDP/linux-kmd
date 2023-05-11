/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_VIRTUAL_ENGINE_TYPES__
#define __INTEL_VIRTUAL_ENGINE_TYPES__

#include "intel_context_types.h"

struct intel_virtual_engine {
	struct intel_engine_cs base;
	struct intel_context context;

	/*
	 * We allow only a single request through the virtual engine at a time
	 * (each request in the timeline waits for the completion fence of
	 * the previous before being submitted). By restricting ourselves to
	 * only submitting a single request, each request is placed on to a
	 * physical to maximise load spreading (by virtue of the late greedy
	 * scheduling -- each real engine takes the next available request
	 * upon idling).
	 */
	struct i915_request *request;

	/*
	 * We keep a rbtree of available virtual engines inside each physical
	 * engine, sorted by priority. Here we preallocate the nodes we need
	 * for the virtual engine, indexed by physical_engine->id.
	 */
	struct ve_node {
		struct rb_node rb;
		int prio;
	} nodes[I915_NUM_ENGINES];

	/*
	 * Keep track of bonded pairs -- restrictions upon on our selection
	 * of physical engines any particular request may be submitted to.
	 * If we receive a submit-fence from a master engine, we will only
	 * use one of sibling_mask physical engines.
	 */
	struct ve_bond {
		const struct intel_engine_cs *master;
		intel_engine_mask_t sibling_mask;
	} *bonds;
	unsigned int num_bonds;

	/* And finally, which physical engines this virtual engine maps onto. */
	unsigned int num_siblings;
	struct intel_engine_cs *siblings[0];
};

#endif /* __INTEL_VIRTUAL_ENGINE_TYPES__ */
