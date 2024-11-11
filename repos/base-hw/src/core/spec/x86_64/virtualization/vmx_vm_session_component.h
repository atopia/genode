/*
 * \brief  VMX implementation of the VM session interface
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

/* base-hw includes */
#include <hw_native_vcpu/hw_native_vcpu.h>
#include <vm_session_component.h>

#include <spec/x86_64/virtualization/ept.h>

namespace Core { class Vmx_vm_session_component; }


class Core::Vmx_vm_session_component
:
	public Core::Vm_session_component
{
	private:

		using Vm_page_table_array =
			Hw::Ept::Allocator::Array<Kernel::DEFAULT_TRANSLATION_TABLE_MAX>;

		/*
		 * Noncopyable
		 */
		Vmx_vm_session_component(Vmx_vm_session_component const &);
		Vmx_vm_session_component &operator = (Vmx_vm_session_component const &);

		Hw::Ept              &_table;
		Vm_page_table_array  &_table_array;

		void *_alloc_table() override;
		void  _attach(addr_t phys_addr, addr_t vm_addr, size_t size) override;
		void _attach_vm_memory(Dataspace_component &, addr_t, Attach_attr) override;
		void _detach_vm_memory(addr_t, size_t) override;

	public:

		Vmx_vm_session_component(Rpc_entrypoint &, Resources, Label const &,
		                         Diag, Ram_allocator &ram, Region_map &, unsigned,
		                         Trace::Source_registry &);
		~Vmx_vm_session_component();
};

#endif /* _CORE__VMX_VM_SESSION_COMPONENT_H_ */
