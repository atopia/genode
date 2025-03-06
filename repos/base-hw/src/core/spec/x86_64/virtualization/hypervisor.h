/*
 * \brief  Interface between kernel and hypervisor
 * \author Benjamin Lamowski
 * \date   2022-10-14
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SPEC__PC__VIRTUALIZATION_HYPERVISOR_H_
#define _SPEC__PC__VIRTUALIZATION_HYPERVISOR_H_

#include <base/stdint.h>
#include <base/log.h>

namespace Hypervisor {

	using Call_arg = Genode::umword_t;
	using Call_ret = Genode::umword_t;


	inline void jump_to_kernel_entry(Genode::addr_t stack_start, Genode::uint64_t trap_value)
	{
		asm volatile(
		      "mov  %[stack], %%rsp;"
		      "subq $568, %%rsp;" /* keep room for fpu and general-purpose registers */
		      "pushq %[trap_val];" /* make the stack point to trapno, the right place */
		      "jmp _kernel_entry;"
		      :
		      : [stack]    "r" (stack_start),
		        [trap_val] "i"(trap_value)

		      : "memory");
	};
}

#endif /* _SPEC__PC__VIRTUALIZATION_HYPERVISOR_H_ */
