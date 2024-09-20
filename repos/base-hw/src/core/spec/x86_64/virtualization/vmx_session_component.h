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
#include <base/allocator_avl.h>
#include <base/session_object.h>
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

#include <region_map_component.h>
#include <vm_session_component.h>

namespace Core { class Vmx_session_component; }


class Core::Vmx_session_component
:
	public Core::Vm_session_component,
	public  Region_map_detach
{
	private:

		using Avl_region = Allocator_avl_tpl<Rm_region>;

		using Vm_page_table_array =
			Hw::Ept::Allocator::Array<Kernel::DEFAULT_TRANSLATION_TABLE_MAX>;


		/*
		 * Noncopyable
		 */
		Vmx_session_component(Vmx_session_component const &);
		Vmx_session_component &operator = (Vmx_session_component const &);

		struct Vcpu : public Rpc_object<Vm_session::Native_vcpu, Vcpu>
		{
			Kernel::Vm::Identity      &id;
			Rpc_entrypoint            &ep;
			Ram_dataspace_capability   ds_cap   { };
			addr_t                     ds_addr  { };
			Kernel_object<Kernel::Vm>  kobj     { };
			Affinity::Location         location { };

			Vcpu(Kernel::Vm::Identity &id, Rpc_entrypoint &ep) : id(id), ep(ep)
			{
				ep.manage(this);
			}

			~Vcpu()
			{
				ep.dissolve(this);
			}

			/*******************************
			 ** Native_vcpu RPC interface **
			 *******************************/

			Capability<Dataspace>   state() const { return ds_cap; }
			Native_capability native_vcpu()       { return kobj.cap(); }

			void exception_handler(Signal_context_capability);
		};

		Constructible<Vcpu>       _vcpus[Board::VCPU_MAX];

		Rpc_entrypoint           &_ep;
		Constrained_ram_allocator _constrained_md_ram_alloc;
		Sliced_heap               _sliced_heap;
		Avl_region                _map { &_sliced_heap };
		Region_map               &_region_map;
		Hw::Ept                  &_table;
		Vm_page_table_array      &_table_array;
		Kernel::Vm::Identity      _id;
		unsigned                  _vcpu_id_alloc { 0 };

		static size_t _ds_size();
		static size_t _alloc_vcpu_data(Genode::addr_t ds_addr);

		void *_alloc_table();
		void  _attach(addr_t phys_addr, addr_t vm_addr, size_t size);

		/* helpers for vm_session_common.cc */
		void _attach_vm_memory(Dataspace_component &, addr_t, Attach_attr);
		void _detach_vm_memory(addr_t, size_t);
		void _with_region(addr_t, auto const &);

	public:

		Vmx_session_component(Rpc_entrypoint &, Resources, Label const &,
		                     Diag, Ram_allocator &ram, Region_map &, unsigned,
		                     Trace::Source_registry &);
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
};

#endif /* _CORE__VMX_VM_SESSION_COMPONENT_H_ */
