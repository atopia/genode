/*
 * \brief  VM session component for 'base-hw'
 * \author Stefan Kalkowski
 * \author Benjamin Lamowski
 * \date   2015-02-17
 */

/*
 * Copyright (C) 2015-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <util/construct_at.h>

/* base internal includes */
#include <base/internal/unmanaged_singleton.h>

/* core includes */
#include <kernel/core_interface.h>
#include <vm_session_component.h>
#include <platform.h>
#include <cpu_thread_component.h>
#include <core_env.h>

using namespace Core;


static Core_mem_allocator & cma() {
	return static_cast<Core_mem_allocator&>(platform().core_mem_alloc()); }


using Vmid_allocator = Genode::Bit_allocator<256>;

static Vmid_allocator &alloc()
{
	static Vmid_allocator * allocator = nullptr;
	if (!allocator) {
		allocator = unmanaged_singleton<Vmid_allocator>();

		/* reserve VM ID 0 for the hypervisor */
		addr_t id = allocator->alloc();
		assert (id == 0);
	}
	return *allocator;
}


Genode::addr_t Vm_session_component::_alloc_vcpu_data(Genode::addr_t ds_addr)
{
	/*
	 * XXX these allocations currently leak memory on VM Session
	 * destruction. This cannot be easily fixed because the
	 * Core Mem Allocator does not implement free().
	 *
	 * Normally we would use constrained_md_ram_alloc to make the allocation,
	 * but to get the physical address of the pages in virt_area, we need
	 * to use the Core Mem Allocator.
	 */

	Vcpu_data * vcpu_data = (Vcpu_data *) cma()
	                        .try_alloc(sizeof(Board::Vcpu_data))
	                        .convert<void *>(
	                                [&](void *ptr) { return ptr; },
	                                [&](Range_allocator::Alloc_error) -> void * {
	                                        /* XXX handle individual error conditions */
	                                        error("failed to allocate kernel object");
	                                        throw Insufficient_ram_quota();
	                                });

	vcpu_data->virt_area = cma()
	                       .alloc_aligned(Vcpu_data::size(), 12)
	                       .convert<void *>(
	                                [&](void *ptr) { return ptr; },
	                                [&](Range_allocator::Alloc_error) -> void * {
	                                        /* XXX handle individual error conditions */
	                                        error("failed to allocate kernel object");
	                                        throw Insufficient_ram_quota();
	                                });

	vcpu_data->vcpu_state = (Vcpu_state *) ds_addr;
	vcpu_data->phys_addr  = (addr_t)cma().phys_addr(vcpu_data->virt_area);

	return (Genode::addr_t) vcpu_data;
}


Vm_session_component::Vm_session_component(Rpc_entrypoint &ds_ep,
                                           Resources resources,
                                           Label const &,
                                           Diag,
                                           Ram_allocator &ram_alloc,
                                           Region_map &region_map,
                                           unsigned,
                                           Trace::Source_registry &)
:
	Ram_quota_guard(resources.ram_quota),
	Cap_quota_guard(resources.cap_quota),
	_ep(ds_ep),
	_constrained_md_ram_alloc(ram_alloc, _ram_quota_guard(), _cap_quota_guard()),
	_sliced_heap(_constrained_md_ram_alloc, region_map),
	_region_map(region_map),
	_id({(unsigned)alloc().alloc(), nullptr})
{
	/* configure managed VM area */
	_map.add_range(0UL, ~0UL);
}


Vm_session_component::~Vm_session_component()
{
	/* detach all regions */
	while (true) {
		addr_t out_addr = 0;

		if (!_map.any_block_addr(&out_addr))
			break;

		detach_at(out_addr);
	}

	/* free region in allocator */
	for (unsigned i = 0; i < _vcpu_id_alloc; i++) {
		if (!_vcpus[i].constructed())
			continue;

		Vcpu & vcpu = *_vcpus[i];
		if (vcpu.ds_cap.valid()) {
			_region_map.detach(vcpu.ds_addr);
			_constrained_md_ram_alloc.free(vcpu.ds_cap);
		}
	}

	alloc().free(_id.id);
}
