/*
 * \brief  x86_64-specific instance of the VM session interface
 * \author Benjamin Lamowski
 * \date   2024-09-20
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__VM_SESSION_COMPONENT_H_
#define _CORE__VM_SESSION_COMPONENT_H_

/* base includes */
#include <base/session_object.h>
#include <vm_session/vm_session.h>
#include <dataspace/capability.h>

/* base-hw includes */
#include <hw_native_vcpu/hw_native_vcpu.h>

/* core includes */
#include <object.h>
#include <kernel/vm.h>
#include <trace/source_registry.h>

namespace Core { class Vm_session_component; }


class Core::Vm_session_component
:
	private Ram_quota_guard,
	private Cap_quota_guard,
	public  Rpc_object<Vm_session, Vm_session_component>
{
	protected:

		Ram_quota_guard &_ram_quota_guard() { return *this; }
		Cap_quota_guard &_cap_quota_guard() { return *this; }

	public:

		using Ram_quota_guard::upgrade;
		using Cap_quota_guard::upgrade;

		using Rpc_object<Vm_session, Vm_session_component>::cap;

		virtual Capability<Native_vcpu> create_vcpu(Thread_capability)
		{
			return Capability<Native_vcpu>();
		}

		Vm_session_component(Resources resources)
		:
			Ram_quota_guard(resources.ram_quota),
			Cap_quota_guard(resources.cap_quota)
		{}
};

#endif /* _CORE__VM_SESSION_COMPONENT_H_ */
