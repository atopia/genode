/*
 * \brief  SVM implementation of the VM session interface for 'base-hw'
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
#include <util/flex_iterator.h>

/* base internal includes */
#include <base/internal/unmanaged_singleton.h>

/* core includes */
#include <dataspace_component.h>
#include <kernel/core_interface.h>
#include <virtualization/svm_session_component.h>
#include <platform.h>
#include <cpu_thread_component.h>
#include <core_env.h>

using namespace Core;



static Core_mem_allocator & cma() {
	return static_cast<Core_mem_allocator&>(platform().core_mem_alloc()); }


void Svm_session_component::_attach(addr_t phys_addr, addr_t vm_addr, size_t size)
{
	using namespace Hw;

	Page_flags pflags { RW, EXEC, USER, NO_GLOBAL, RAM, CACHED };

	try {
		_table.insert_translation(vm_addr, phys_addr, size, pflags,
		                          _table_array.alloc());
		return;
	} catch(Hw::Out_of_tables &) {
		Genode::error("Translation table needs to much RAM");
	} catch(...) {
		Genode::error("Invalid mapping ", Genode::Hex(phys_addr), " -> ",
		              Genode::Hex(vm_addr), " (", size, ")");
	}
}


void Svm_session_component::_attach_vm_memory(Dataspace_component &dsc,
                                              addr_t const vm_addr,
                                              Attach_attr const attribute)
{
	_attach(dsc.phys_addr() + attribute.offset, vm_addr, attribute.size);
}


void Svm_session_component::attach_pic(addr_t )
{ }


void Svm_session_component::_detach_vm_memory(addr_t vm_addr, size_t size)
{
	_table.remove_translation(vm_addr, size, _table_array.alloc());
}


void * Svm_session_component::_alloc_table()
{
	/* get some aligned space for the translation table */
	return cma().alloc_aligned(sizeof(Hw::Hpt),
	                           Hw::Hpt::ALIGNM_LOG2).convert<void *>(
		[&] (void *table_ptr) {
			return table_ptr; },

		[&] (Range_allocator::Alloc_error) -> void * {
			/* XXX handle individual error conditions */
			error("failed to allocate kernel object");
			throw Insufficient_ram_quota(); }
	);
}


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


Genode::addr_t Svm_session_component::_alloc_vcpu_data(Genode::addr_t ds_addr)
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

	using Genode::error;

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


Svm_session_component::Svm_session_component(Rpc_entrypoint &ds_ep,
                                             Resources resources,
                                             Label const &label,
                                             Diag diag,
                                             Ram_allocator &ram_alloc,
                                             Region_map &region_map,
                                             unsigned,
                                             Trace::Source_registry &)
:
	Session_object(ds_ep, resources, label, diag),
	_ep(ds_ep),
	_constrained_md_ram_alloc(ram_alloc, _ram_quota_guard(), _cap_quota_guard()),
	_sliced_heap(_constrained_md_ram_alloc, region_map),
	_region_map(region_map),
	_table(*construct_at<Hw::Hpt>(_alloc_table())),
	_table_array(*(new (cma()) Vm_page_table_array([] (void * virt) {
	                           return (addr_t)cma().phys_addr(virt);}))),
	_id({(unsigned)alloc().alloc(), cma().phys_addr(&_table)})
{
	/* configure managed VM area */
	_map.add_range(0UL, ~0UL);
}


Svm_session_component::~Svm_session_component()
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

	/* free guest-to-host page tables */
	destroy(platform().core_mem_alloc(), &_table);
	destroy(platform().core_mem_alloc(), &_table_array);
	alloc().free(_id.id);
}


void Svm_session_component::attach(Dataspace_capability const cap,
                                  addr_t const guest_phys,
                                  Attach_attr attribute)
{
	if (!cap.valid())
		throw Invalid_dataspace();

	/* check dataspace validity */
	_ep.apply(cap, [&] (Dataspace_component *ptr) {
		if (!ptr)
			throw Invalid_dataspace();

		Dataspace_component &dsc = *ptr;

		/* unsupported - deny otherwise arbitrary physical memory can be mapped to a VM */
		if (dsc.managed())
			throw Invalid_dataspace();

		if (guest_phys & 0xffful || attribute.offset & 0xffful ||
		    attribute.size & 0xffful)
			throw Invalid_dataspace();

		if (!attribute.size) {
			attribute.size = dsc.size();

			if (attribute.offset < attribute.size)
				attribute.size -= attribute.offset;
		}

		if (attribute.size > dsc.size())
			attribute.size = dsc.size();

		if (attribute.offset >= dsc.size() ||
		    attribute.offset > dsc.size() - attribute.size)
			throw Invalid_dataspace();

		using Alloc_error = Range_allocator::Alloc_error;

		Region_map_detach &rm_detach = *this;

		_map.alloc_addr(attribute.size, guest_phys).with_result(

			[&] (void *) {

				Rm_region::Attr const region_attr
				{
					.base  = guest_phys,
					.size  = attribute.size,
					.write = dsc.writeable() && attribute.writeable,
					.exec  = attribute.executable,
					.off   = attribute.offset,
					.dma   = false,
				};

				/* store attachment info in meta data */
				try {
					_map.construct_metadata((void *)guest_phys,
					                        dsc, rm_detach, region_attr);

				} catch (Allocator_avl_tpl<Rm_region>::Assign_metadata_failed) {
					error("failed to store attachment info");
					throw Invalid_dataspace();
				}

				Rm_region &region = *_map.metadata((void *)guest_phys);

				/* inform dataspace about attachment */
				dsc.attached_to(region);
			},

			[&] (Alloc_error error) {

				switch (error) {

				case Alloc_error::OUT_OF_RAM:  throw Out_of_ram();
				case Alloc_error::OUT_OF_CAPS: throw Out_of_caps();
				case Alloc_error::DENIED:
					{
						/*
						 * Handle attach after partial detach
						 */
						Rm_region *region_ptr = _map.metadata((void *)guest_phys);
						if (!region_ptr)
							throw Region_conflict();

						Rm_region &region = *region_ptr;

						region.with_dataspace([&] (Dataspace_component &dataspace) {
							if (!(cap == dataspace.cap()))
								throw Region_conflict();
						});

						if (guest_phys < region.base() ||
						    guest_phys > region.base() + region.size() - 1)
							throw Region_conflict();
					}
				};
			}
		);

		/* kernel specific code to attach memory to guest */
		_attach_vm_memory(dsc, guest_phys, attribute);
	});
}


void Svm_session_component::detach(addr_t guest_phys, size_t size)
{
	if (!size || (guest_phys & 0xffful) || (size & 0xffful)) {
		warning("vm_session: skipping invalid memory detach addr=",
		        (void *)guest_phys, " size=", (void *)size);
		return;
	}

	addr_t const guest_phys_end = guest_phys + (size - 1);
	addr_t       addr           = guest_phys;
	do {
		Rm_region *region = _map.metadata((void *)addr);

		/* walk region holes page-by-page */
		size_t iteration_size = 0x1000;

		if (region) {
			iteration_size = region->size();
			detach_at(region->base());
		}

		if (addr >= guest_phys_end - (iteration_size - 1))
			break;

		addr += iteration_size;
	} while (true);
}


void Svm_session_component::_with_region(addr_t const addr,
                                        auto const &fn)
{
	Rm_region *region = _map.metadata((void *)addr);
	if (region)
		fn(*region);
	else
		error(__PRETTY_FUNCTION__, " unknown region");
}


void Svm_session_component::detach_at(addr_t const addr)
{
	_with_region(addr, [&] (Rm_region &region) {

		if (!region.reserved())
			reserve_and_flush(addr);

		/* free the reserved region */
		_map.free(reinterpret_cast<void *>(region.base()));
	});
}


void Svm_session_component::unmap_region(addr_t base, size_t size)
{
	error(__func__, " unimplemented ", base, " ", size);
}


void Svm_session_component::reserve_and_flush(addr_t const addr)
{
	_with_region(addr, [&] (Rm_region &region) {

		/* inform dataspace */
		region.with_dataspace([&] (Dataspace_component &dataspace) {
			dataspace.detached_from(region);
		});

		region.mark_as_reserved();

		/* kernel specific code to detach memory from guest */
		_detach_vm_memory(region.base(), region.size());
	});
}


Capability<Vm_session::Native_vcpu> Svm_session_component::create_vcpu(Thread_capability const tcap)
{
	if (_vcpu_id_alloc == Board::VCPU_MAX) return { };

	Affinity::Location vcpu_location;
	_ep.apply(tcap, [&] (Cpu_thread_component *ptr) {
		if (!ptr) return;
		vcpu_location = ptr->platform_thread().affinity();
	});

	if (_vcpus[_vcpu_id_alloc].constructed())
		return { };

	_vcpus[_vcpu_id_alloc].construct(_id, _ep);
	Vcpu & vcpu = *_vcpus[_vcpu_id_alloc];

	try {
		vcpu.ds_cap = _constrained_md_ram_alloc.alloc(_ds_size(), Cache::UNCACHED);

		Region_map::Attr attr { };
		attr.writeable = true;
		vcpu.ds_addr = _region_map.attach(vcpu.ds_cap, attr).convert<addr_t>(
			[&] (Region_map::Range range) { return _alloc_vcpu_data(range.start); },
			[&] (Region_map::Attach_error) -> addr_t {
				error("failed to attach VCPU data within core");
				if (vcpu.ds_cap.valid())
					_constrained_md_ram_alloc.free(vcpu.ds_cap);
				_vcpus[_vcpu_id_alloc].destruct();
				return 0;
			});
	} catch (...) {
		if (vcpu.ds_cap.valid())
			_constrained_md_ram_alloc.free(vcpu.ds_cap);
		_vcpus[_vcpu_id_alloc].destruct();
		throw;
	}

	vcpu.location = vcpu_location;

	_vcpu_id_alloc ++;
	return vcpu.cap();
}


size_t Svm_session_component::_ds_size() {
	return align_addr(sizeof(Board::Vcpu_state), get_page_size_log2()); }


void Svm_session_component::Vcpu::exception_handler(Signal_context_capability handler)
{
	using Genode::warning;
	if (!handler.valid()) {
		warning("invalid signal");
		return;
	}

	if (kobj.constructed()) {
		warning("Cannot register vcpu handler twice");
		return;
	}

	unsigned const cpu = location.xpos();

	if (!kobj.create(cpu, (void *)ds_addr, Capability_space::capid(handler), id))
		warning("Cannot instantiate vm kernel object, invalid signal context?");
}
