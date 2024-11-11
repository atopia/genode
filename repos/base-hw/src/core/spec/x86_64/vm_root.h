/*
 * \brief  x86_64 specific Vm root interface
 * \author Stefan Kalkowski
 * \author Benjamin Lamowski
 * \date   2012-10-08
 */

/*
 * Copyright (C) 2012-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__INCLUDE__VM_ROOT_H_
#define _CORE__INCLUDE__VM_ROOT_H_

/* Genode includes */
#include <root/component.h>
#include <base/heap.h>

/* Hw includes */
 #include <hw/spec/x86_64/x86_64.h>

/* core includes */
#include <vm_session_component.h>
#include <virtualization/vmx_vm_session_component.h>
#include <virtualization/svm_vm_session_component.h>

namespace Core { class Vm_root; }


class Core::Vm_root : public Root_component<Vm_session_component>
{
	private:

		Ram_allocator          &_ram_allocator;
		Region_map             &_local_rm;
		Trace::Source_registry &_trace_sources;

	protected:

		Vm_session_component *_create_session(const char *args) override
		{
			unsigned priority = 0;
			Arg a = Arg_string::find_arg(args, "priority");
			if (a.valid()) {
				priority = (unsigned)a.ulong_value(0);

				/* clamp priority value to valid range */
				priority = min((unsigned)Cpu_session::PRIORITY_LIMIT - 1, priority);
			}
			switch (Hw::Virtualization_support::virt_type()) {
			case Hw::VIRT_TYPE_SVM:
				return new (md_alloc())
					Svm_vm_session_component(*ep(),
				                             session_resources_from_args(args),
				                             session_label_from_args(args),
				                             session_diag_from_args(args),
				                             _ram_allocator, _local_rm, priority,
				                             _trace_sources);
			case Hw::VIRT_TYPE_VMX:
				return new (md_alloc())
					Vmx_vm_session_component(*ep(),
				                             session_resources_from_args(args),
				                             session_label_from_args(args),
				                             session_diag_from_args(args),
				                             _ram_allocator, _local_rm, priority,
				                             _trace_sources);
			default:
				Genode::error( "No virtualization support detected.");
				throw Core::Service_denied();
			}
		}

		void _upgrade_session(Vm_session_component *vm, const char *args) override
		{
			vm->upgrade(ram_quota_from_args(args));
			vm->upgrade(cap_quota_from_args(args));
		}

	public:

		/**
		 * Constructor
		 *
		 * \param session_ep  entrypoint managing vm_session components
		 * \param md_alloc    meta-data allocator to be used by root component
		 */
		Vm_root(Rpc_entrypoint         &session_ep,
		        Allocator              &md_alloc,
		        Ram_allocator          &ram_alloc,
		        Region_map             &local_rm,
		        Trace::Source_registry &trace_sources)
		:
			Root_component<Vm_session_component>(&session_ep, &md_alloc),
			_ram_allocator(ram_alloc),
			_local_rm(local_rm),
			_trace_sources(trace_sources)
		{ }
};

#endif /* _CORE__INCLUDE__VM_ROOT_H_ */
