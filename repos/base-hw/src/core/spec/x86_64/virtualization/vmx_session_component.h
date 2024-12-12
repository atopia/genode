/*
 * \brief  VMX VM session component for 'base-hw'
 * \author Benjamin Lamowski
 * \date   2024-09-20
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__VMX_VM_SESSION_COMPONENT_H_
#define _CORE__VMX_VM_SESSION_COMPONENT_H_

/* base includes */
#include <base/allocator.h>
#include <base/session_object.h>
#include <base/registry.h>
#include <vm_session/vm_session.h>
#include <dataspace/capability.h>

/* base-hw includes */
#include <hw_native_vcpu/hw_native_vcpu.h>

#include <spec/x86_64/virtualization/ept.h>

/* core includes */
#include <object.h>
#include <region_map_component.h>
#include <kernel/vm.h>
#include <trace/source_registry.h>

#include <vcpu.h>
#include <vmid_allocator.h>
#include <guest_memory.h>
#include <phys_allocated.h>


namespace Core { class Vmx_session_component; }


class Core::Vmx_session_component
:
	public Session_object<Vm_session>,
	public Region_map_detach
{
	private:

		using Vm_page_table = Hw::Ept;

		using Vm_page_table_array =
			Vm_page_table::Allocator::Array<Kernel::DEFAULT_TRANSLATION_TABLE_MAX>;


		/*
		 * Noncopyable
		 */
		Vmx_session_component(Vmx_session_component const &);
		Vmx_session_component &operator = (Vmx_session_component const &);

		Registry<Registered<Vcpu>>          _vcpus { };

		Rpc_entrypoint                     &_ep;
		Constrained_ram_allocator           _constrained_md_ram_alloc;
		Ram_allocator                      &_core_ram_alloc;
		Region_map                         &_region_map;
		Heap                                _heap;
		Phys_allocated<Vm_page_table>       _table;
		Phys_allocated<Vm_page_table_array> _table_array;
		Guest_memory                        _memory;
		Vmid_allocator                     &_vmid_alloc;
		Kernel::Vm::Identity                _id;

	public:

		Vmx_session_component(Vmid_allocator &, Rpc_entrypoint &,
		                      Resources, Label const &, Diag,
		                      Ram_allocator &, Region_map &, unsigned,
		                      Trace::Source_registry &,
		                      Ram_allocator &);
		~Vmx_session_component();


		/*********************************
		 ** Region_map_detach interface **
		 *********************************/

		void detach_at         (addr_t)         override;
		void unmap_region      (addr_t, size_t) override;
		void reserve_and_flush (addr_t)         override;


		/**************************
		 ** Vm session interface **
		 **************************/

		void attach(Dataspace_capability, addr_t, Attach_attr) override;
		void attach_pic(addr_t) override;
		void detach(addr_t, size_t) override;

		Capability<Native_vcpu> create_vcpu(Thread_capability) override;

		static constexpr size_t CORE_MEM_SIZE { sizeof(Vm_page_table) + sizeof(Vm_page_table_array) };
};

#endif /* _CORE__VMX_VM_SESSION_COMPONENT_H_ */
