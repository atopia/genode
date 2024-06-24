/*
 * \brief  VM page table abstraction between VMX and SVM for x86
 * \author Benjamin Lamowski
 * \date   2024-04-23
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__SPEC__PC__VIRTUALIZATION__VM_PAGE_TABLE_H_
#define _CORE__SPEC__PC__VIRTUALIZATION__VM_PAGE_TABLE_H_

#include <base/log.h>
#include <util/construct_at.h>
#include <spec/x86_64/virtualization/ept.h>
#include <spec/x86_64/virtualization/hpt.h>

namespace Board {
	using namespace Genode;

	struct Vm_page_table
	{
		/* Both Ept and Hpt need to actually use this allocator */
		using Allocator = Genode::Page_table_allocator<1UL << SIZE_LOG2_4KB>;

		template <class T, class U>
		struct is_same {
			static const bool value = false;
		};

		template <class T>
		struct is_same <T, T> {
			static const bool value = true;
		};

		static_assert(is_same<Allocator, Hw::Ept::Allocator>::value,
			"Ept uses different allocator");
		static_assert(is_same<Allocator, Hw::Hpt::Allocator>::value,
			"Hpt uses different allocator");

		static constexpr size_t ALIGNM_LOG2 = Hw::SIZE_LOG2_4KB;

		union {
			Hw::Ept ept;
			Hw::Hpt hpt;
		};
		Vm_page_table()
		{ }
	};

	using Vm_page_table_array =
		Vm_page_table::Allocator::Array<Kernel::DEFAULT_TRANSLATION_TABLE_MAX>;
};

#endif /* _CORE__SPEC__PC__VIRTUALIZATION__VM_PAGE_TABLE_H_ */
