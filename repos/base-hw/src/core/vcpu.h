/*
 * \brief   Vm_session vCPU
 * \author  Benjamin Lamowski
 * \date    2024-11-26
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__VCPU_H_
#define _CORE__VCPU_H_

/* base includes */
#include <vm_session/vm_session.h>

/* base-hw includes */
#include <hw_native_vcpu/hw_native_vcpu.h>
#include <kernel/vm.h>

namespace Core { struct Vcpu; }


struct Core::Vcpu : public Rpc_object<Vm_session::Native_vcpu, Vcpu>
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

	void exception_handler(Signal_context_capability handler)
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
};


#endif /* _CORE__VCPU_H_ */
