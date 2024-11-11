/*
 * \brief  SVM VM session component for 'base-hw'
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
#include <virtualization/svm_vm_session_component.h>
#include <platform.h>
#include <core_env.h>

using namespace Core;



static Core_mem_allocator & cma() {
	return static_cast<Core_mem_allocator&>(platform().core_mem_alloc()); }


void Svm_vm_session_component::_attach(addr_t phys_addr, addr_t vm_addr, size_t size)
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


void Svm_vm_session_component::_attach_vm_memory(Dataspace_component &dsc,
                                                 addr_t const vm_addr,
                                                 Attach_attr const attribute)
{
	_attach(dsc.phys_addr() + attribute.offset, vm_addr, attribute.size);
}


void Svm_vm_session_component::_detach_vm_memory(addr_t vm_addr, size_t size)
{
	_table.remove_translation(vm_addr, size, _table_array.alloc());
}


void * Svm_vm_session_component::_alloc_table()
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


Svm_vm_session_component::Svm_vm_session_component(Rpc_entrypoint &ds_ep,
                                                   Resources resources,
                                                   Label const &label,
                                                   Diag diag,
                                                   Ram_allocator &ram_alloc,
                                                   Region_map &region_map,
                                                   unsigned prio,
                                                   Trace::Source_registry &source_registry)
:
	Vm_session_component(ds_ep,
	                     resources,
	                     label,
	                     diag,
	                     ram_alloc,
	                     region_map,
	                     prio,
	                     source_registry),
	_table(*construct_at<Hw::Hpt>(_alloc_table())),
	_table_array(*(new (cma()) Vm_page_table_array([] (void * virt) {
	                           return (addr_t)cma().phys_addr(virt);})))
{
	set_page_table_addr(cma().phys_addr(&_table));
}


Svm_vm_session_component::~Svm_vm_session_component()
{
	/* free guest-to-host page tables */
	destroy(platform().core_mem_alloc(), &_table);
	destroy(platform().core_mem_alloc(), &_table_array);
}
