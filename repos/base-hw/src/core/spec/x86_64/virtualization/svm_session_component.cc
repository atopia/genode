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

/* core includes */
#include <kernel/core_interface.h>
#include <virtualization/svm_session_component.h>
#include <platform.h>
#include <cpu_thread_component.h>
#include <core_env.h>

using namespace Core;



static Core_mem_allocator & cma() {
	return static_cast<Core_mem_allocator&>(platform().core_mem_alloc()); }


void Svm_session_component::attach_pic(addr_t )
{ }


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


Svm_session_component::Svm_session_component(Vmid_allocator & vmid_alloc,
                                             Rpc_entrypoint &ds_ep,
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
	_region_map(region_map),
	_table(*construct_at<Hw::Hpt>(_alloc_table())),
	_table_array(*(new (cma()) Vm_page_table_array([] (void * virt) {
	                           return (addr_t)cma().phys_addr(virt);}))),
	_memory(_constrained_md_ram_alloc, region_map),
	_vmid_alloc(vmid_alloc),
	_id({(unsigned)_vmid_alloc.alloc(), cma().phys_addr(&_table)})
{
}


Svm_session_component::~Svm_session_component()
{
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
	_vmid_alloc.free(_id.id);
}


void Svm_session_component::attach(Dataspace_capability const cap,
                                  addr_t const guest_phys,
                                  Attach_attr attr)
{
	bool out_of_tables   = false;
	bool invalid_mapping = false;

	auto const &map_fn = [&](addr_t vm_addr, addr_t phys_addr, size_t size) {
		Page_flags const pflags { RW, EXEC, USER, NO_GLOBAL, RAM, CACHED };

		try {
			_table.insert_translation(vm_addr, phys_addr, size, pflags, _table_array.alloc());
		} catch(Hw::Out_of_tables &) {
			Genode::error("Translation table needs to much RAM");
			out_of_tables = true;
		} catch(...) {
			Genode::error("Invalid mapping ", Genode::Hex(phys_addr), " -> ",
				      Genode::Hex(vm_addr), " (", size, ")");
			invalid_mapping = true;
		}
	};

	if (!cap.valid())
		throw Invalid_dataspace();

	/* check dataspace validity */
	_ep.apply(cap, [&] (Dataspace_component *ptr) {
		if (!ptr)
			throw Invalid_dataspace();

		Dataspace_component &dsc = *ptr;

		Region_map_detach &rm_detach = *this;

		Guest_memory::Attach_result result =
			_memory.attach(rm_detach, dsc, guest_phys, attr, map_fn);

		if (out_of_tables)
			throw Out_of_ram();

		if (invalid_mapping)
			throw Invalid_dataspace();

		switch (result) {
		case Guest_memory::Attach_result::OK             : break;
		case Guest_memory::Attach_result::INVALID_DS     : throw Invalid_dataspace(); break;
		case Guest_memory::Attach_result::OUT_OF_RAM     : throw Out_of_ram(); break;
		case Guest_memory::Attach_result::OUT_OF_CAPS    : throw Out_of_caps(); break;
		case Guest_memory::Attach_result::REGION_CONFLICT: throw Region_conflict(); break;
		}
	});
}


void Svm_session_component::detach(addr_t guest_phys, size_t size)
{
	auto const &unmap_fn = [&](addr_t vm_addr, size_t size) {
		_table.remove_translation(vm_addr, size, _table_array.alloc());
	};

	_memory.detach(guest_phys, size, unmap_fn);
}


void Svm_session_component::detach_at(addr_t const addr)
{
	auto const &unmap_fn = [&](addr_t vm_addr, size_t size) {
		_table.remove_translation(vm_addr, size, _table_array.alloc());
	};

	_memory.detach_at(addr, unmap_fn);
}


void Svm_session_component::unmap_region(addr_t base, size_t size)
{
	error(__func__, " unimplemented ", base, " ", size);
}


void Svm_session_component::reserve_and_flush(addr_t const addr)
{
	auto const &unmap_fn = [&](addr_t vm_addr, size_t size) {
		_table.remove_translation(vm_addr, size, _table_array.alloc());
	};

	_memory.reserve_and_flush(addr, unmap_fn);
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
