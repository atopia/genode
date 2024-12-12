/*
 * \brief  VMX implementation of the VM session component for 'base-hw'
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
#include <virtualization/vmx_session_component.h>
#include <platform.h>
#include <cpu_thread_component.h>
#include <core_env.h>

using namespace Core;



void Vmx_session_component::attach_pic(addr_t )
{ }


Vmx_session_component::Vmx_session_component(Vmid_allocator & vmid_alloc,
                                             Rpc_entrypoint &ds_ep,
                                             Resources resources,
                                             Label const &label,
                                             Diag diag,
                                             Ram_allocator &ram_alloc,
                                             Region_map &region_map,
                                             unsigned,
                                             Trace::Source_registry &,
                                             Ram_allocator &core_ram_alloc)
:
	Session_object(ds_ep, resources, label, diag),
	_ep(ds_ep),
	_constrained_md_ram_alloc(ram_alloc, _ram_quota_guard(), _cap_quota_guard()),
	_core_ram_alloc(core_ram_alloc),
	_region_map(region_map),
	_heap(_constrained_md_ram_alloc, region_map),
	_table(_ep, _core_ram_alloc, _region_map),
	_table_array(_ep, _core_ram_alloc, _region_map,
			[] (Phys_allocated<Vm_page_table_array> &table_array, auto *obj_ptr) {
				construct_at<Vm_page_table_array>(obj_ptr, [&] (void *virt) {
				return table_array.phys_addr() + ((addr_t) obj_ptr - (addr_t)virt);
				});
			}),
	_memory(_constrained_md_ram_alloc, region_map),
	_vmid_alloc(vmid_alloc),
	_id({(unsigned)_vmid_alloc.alloc(), (void *)_table.phys_addr()})
{
}


Vmx_session_component::~Vmx_session_component()
{
	_vcpus.for_each([&] (Registered<Vcpu> &vcpu) {
		destroy(_heap, &vcpu); });

	_vmid_alloc.free(_id.id);
}


void Vmx_session_component::attach(Dataspace_capability const cap,
                                   addr_t const guest_phys,
                                   Attach_attr attr)
{
	bool out_of_tables   = false;
	bool invalid_mapping = false;

	auto const &map_fn = [&](addr_t vm_addr, addr_t phys_addr, size_t size) {
		Page_flags const pflags { RW, EXEC, USER, NO_GLOBAL, RAM, CACHED };

		try {
			_table.obj.insert_translation(vm_addr, phys_addr, size, pflags, _table_array.obj.alloc());
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


void Vmx_session_component::detach(addr_t guest_phys, size_t size)
{
	auto const &unmap_fn = [&](addr_t vm_addr, size_t size) {
		_table.obj.remove_translation(vm_addr, size, _table_array.obj.alloc());
	};

	_memory.detach(guest_phys, size, unmap_fn);
}


void Vmx_session_component::detach_at(addr_t const addr)
{
	auto const &unmap_fn = [&](addr_t vm_addr, size_t size) {
		_table.obj.remove_translation(vm_addr, size, _table_array.obj.alloc());
	};

	_memory.detach_at(addr, unmap_fn);
}


void Vmx_session_component::unmap_region(addr_t base, size_t size)
{
	error(__func__, " unimplemented ", base, " ", size);
}


void Vmx_session_component::reserve_and_flush(addr_t const addr)
{
	auto const &unmap_fn = [&](addr_t vm_addr, size_t size) {
		_table.obj.remove_translation(vm_addr, size, _table_array.obj.alloc());
	};

	_memory.reserve_and_flush(addr, unmap_fn);
}


Capability<Vm_session::Native_vcpu> Vmx_session_component::create_vcpu(Thread_capability const tcap)
{
	if (!try_withdraw(Ram_quota{Vcpu_data::size()}))
		return { };

	Affinity::Location vcpu_location;
	_ep.apply(tcap, [&] (Cpu_thread_component *ptr) {
		if (!ptr) return;
		vcpu_location = ptr->platform_thread().affinity();
	});

	Vcpu &vcpu = *new (_heap)
				Registered<Vcpu>(_vcpus,
				                 _id,
	                                         _ep,
	                                         _core_ram_alloc,
	                                         _constrained_md_ram_alloc,
	                                         _region_map,
	                                         vcpu_location);

	return vcpu.cap();
}
