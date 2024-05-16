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

#include <kernel/configuration.h>
#include <util/construct_at.h>
#include <spec/x86_64/virtualization/ept.h>
#include <spec/x86_64/virtualization/hpt.h>

namespace Board {
	using namespace Genode;

	class Vm_page_table
	{
	public:
		/* Both Ept and Hpt need to actually use this allocator */
		using Allocator = Genode::Page_table_allocator<1UL << SIZE_LOG2_4KB>;
		static constexpr size_t ALIGNM_LOG2 = Hw::SIZE_LOG2_4KB;

	private:
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

		static_assert(sizeof(Hw::Ept) == sizeof(Hw::Hpt),
			"Ept and Hpt differ in size");

		void * _table_ptr { nullptr };

	public:
		void * table_ptr() { return _table_ptr; }

		virtual void insert_translation(addr_t vo, addr_t pa,
		                                size_t size,
		                                Page_flags const & flags,
		                                Allocator & alloc);

		virtual void remove_translation(addr_t vo, size_t size,
		                                Allocator & alloc);

		Vm_page_table(void * table_ptr) : _table_ptr(table_ptr)
		{ }

		virtual ~Vm_page_table() = default;

		Vm_page_table(Vm_page_table&) = delete;
		Vm_page_table& operator=(const Vm_page_table&) = delete;
	};

	struct Vm_page_table_ept : Vm_page_table
	{

		void insert_translation(addr_t vo,
				addr_t pa,
				size_t size,
				Page_flags const & flags,
				Allocator & alloc) override
		{
			((Hw::Ept *) table_ptr())->insert_translation(vo, pa, size, flags, alloc);
		}

		void remove_translation(addr_t vo, size_t size, Allocator & alloc) override
		{
			((Hw::Ept *) table_ptr())->remove_translation(vo, size, alloc);
		}

		Vm_page_table_ept(void * table_ptr) : Vm_page_table(table_ptr)
		{
			construct_at<Hw::Ept>(table_ptr);
		}

		~Vm_page_table_ept() = default;
	};

	struct Vm_page_table_hpt : Vm_page_table
	{

		void insert_translation(addr_t vo,
				addr_t pa,
				size_t size,
				Page_flags const & flags,
				Allocator & alloc) override
		{
			((Hw::Ept *) table_ptr())->insert_translation(vo, pa, size, flags, alloc);
		}

		void remove_translation(addr_t vo, size_t size, Allocator & alloc) override
		{
			((Hw::Ept *) table_ptr())->remove_translation(vo, size, alloc);
		}

		Vm_page_table_hpt(void * table_ptr) : Vm_page_table(table_ptr)
		{
			construct_at<Hw::Ept>(table_ptr);
		}

		~Vm_page_table_hpt() = default;
	};

	using Vm_page_table_array =
		Vm_page_table::Allocator::Array<Kernel::DEFAULT_TRANSLATION_TABLE_MAX>;
};

#endif /* _CORE__SPEC__PC__VIRTUALIZATION__VM_PAGE_TABLE_H_ */
