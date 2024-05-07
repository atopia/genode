/*
 * \brief  x86_64 page table definitions
 * \author Adrian-Ken Rueegsegger
 * \date   2015-02-06
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__LIB__HW__SPEC__X86_64__PAGE_TABLE_H_
#define _SRC__LIB__HW__SPEC__X86_64__PAGE_TABLE_H_

#include <hw/page_table_allocator.h>
#include <page_table/page_table_base.h>
#include <util/register.h>

namespace Hw {
	using namespace Genode;

	class Page_table;

	/**
	 * IA-32e common descriptor.
	 *
	 * Table entry containing descriptor fields common to all four levels.
	 */
	struct Common_descriptor : Register<64>
	{
		struct P   : Bitfield<0, 1> { };   /* present         */
		struct Rw  : Bitfield<1, 1> { };   /* read/write      */
		struct Us  : Bitfield<2, 1> { };   /* user/supervisor */
		struct Pwt : Bitfield<3, 1> { };   /* write-through or PAT defined */
		struct Pcd : Bitfield<4, 1> { };   /* cache disable or PAT defined */
		struct A   : Bitfield<5, 1> { };   /* accessed        */
		struct D   : Bitfield<6, 1> { };   /* dirty           */
		struct Xd  : Bitfield<63, 1> { };  /* execute-disable */

		static bool present(access_t const v) { return P::get(v); }

		static access_t create(Page_flags const &flags)
		{
			return P::bits(1)
				| Rw::bits(flags.writeable)
				| Us::bits(!flags.privileged)
				| Xd::bits(!flags.executable);
		}

		/**
		 * Return descriptor value with cleared accessed and dirty flags. These
		 * flags can be set by the MMU.
		 */
		static access_t clear_mmu_flags(access_t value)
		{
			A::clear(value);
			D::clear(value);
			return value;
		}
	};

	template <unsigned _PAGE_SIZE_LOG2, unsigned _SIZE_LOG2>
	struct Pml4_table_descriptor : Common_descriptor
	{
		static constexpr size_t SIZE_LOG2      = _SIZE_LOG2;
		static constexpr size_t PAGE_SIZE_LOG2 = _PAGE_SIZE_LOG2;

		struct Pa  : Bitfield<12, SIZE_LOG2> { };    /* physical address */
		/* struct Mt  : Bitset_2<Pwt, Pcd> { }; /1* memory type      *1/ */

		static access_t create(addr_t const pa)
		{
			/* XXX: Set memory type depending on active PAT */
			static Page_flags flags { RW, EXEC, USER, NO_GLOBAL,
				                  RAM, CACHED };
			return Common_descriptor::create(flags) | Pa::masked(pa);
		}
	};

	struct Page_directory_base_descriptor : Common_descriptor
	{
		using Common = Common_descriptor;

		struct Ps : Common::template Bitfield<7, 1> { };  /* page size */

		static bool maps_page(access_t const v) { return Ps::get(v); }
	};

	template <unsigned _PAGE_SIZE_LOG2>
	struct Page_directory_descriptor : Page_directory_base_descriptor
	{
		static constexpr size_t PAGE_SIZE_LOG2 = _PAGE_SIZE_LOG2;

		struct Page;
		struct Table;
	};


	template <unsigned _PAGE_SIZE_LOG2>
	struct Page_table_entry_descriptor : Common_descriptor
	{
		using Common = Common_descriptor;

		static constexpr size_t PAGE_SIZE_LOG2 = _PAGE_SIZE_LOG2;

		struct Pat : Bitfield<7, 1> { };          /* page attribute table */
		struct G   : Bitfield<8, 1> { };          /* global               */
		struct Pa  : Bitfield<12, 36> { };        /* physical address     */
		/* struct Mt  : Bitset_3<Pwt, Pcd, Pat> { }; /1* memory type          *1/ */

		static access_t create(Page_flags const &flags, addr_t const pa)
		{
			bool const wc = flags.cacheable == Cache::WRITE_COMBINED;

			return Common::create(flags)
				| G::bits(flags.global)
				| Pa::masked(pa)
				| Pwt::bits(wc ? 1 : 0);
		}
	};

	struct Level1_translation_table
	:
		Final_table<Page_table_entry_descriptor<Genode::SIZE_LOG2_4KB>>
	{ } __attribute__((aligned(1 << ALIGNM_LOG2)));

	struct Pd
	:
		Page_directory<Level1_translation_table,
		Page_directory_descriptor<SIZE_LOG2_2MB>>
	{ } __attribute__((aligned(1 << ALIGNM_LOG2)));

	struct Pdpt
	:
		Page_directory<Pd,
		Page_directory_descriptor<SIZE_LOG2_1GB>>
	{ } __attribute__((aligned(1 << ALIGNM_LOG2)));

	struct Pml4_adapted_table
	:
		Pml4_table<Pdpt,
		Pml4_table_descriptor<SIZE_LOG2_512GB, Genode::SIZE_LOG2_256TB>>
	{ } __attribute__((aligned(1 << ALIGNM_LOG2)));
}


template <unsigned _PAGE_SIZE_LOG2>
struct Hw::Page_directory_descriptor<_PAGE_SIZE_LOG2>::Table
	: Page_directory_base_descriptor
{
	using Base = Page_directory_base_descriptor;

	/**
	 * Physical address
	 */
	struct Pa : Base::template Bitfield<12, 36> { };

	/**
	 * Memory types
	 */
	/* struct Mt : Base::template Bitset_2<Base::Pwt, */
			                    /* Base::Pcd> { }; */

	static typename Base::access_t create(addr_t const pa)
	{
		/* XXX: Set memory type depending on active PAT */
		static Page_flags flags { RW, EXEC, USER, NO_GLOBAL,
				          RAM, CACHED };
		return Base::create(flags) | Pa::masked(pa);
	}
};


template <unsigned _PAGE_SIZE_LOG2>
struct Hw::Page_directory_descriptor<_PAGE_SIZE_LOG2>::Page
	: Page_directory_base_descriptor
{
	using Base = Page_directory_base_descriptor;

	/**
	 * Global attribute
	 */
	struct G : Base::template Bitfield<8, 1> { };

	/**
	 * Page attribute table
	 */
	struct Pat : Base::template Bitfield<12, 1> { };

	/**
	 * Physical address
	 */
	struct Pa : Base::template Bitfield<PAGE_SIZE_LOG2,
			                     48 - PAGE_SIZE_LOG2> { };

	/**
	 * Memory type
	 */
	/* struct Mt : Base::template Bitset_3<Base::Pwt, */
	/* 		                    Base::Pcd, Pat> { }; */

	static typename Base::access_t create(Page_flags const &flags,
			                      addr_t const pa)
	{
		bool const wc = flags.cacheable == Cache::WRITE_COMBINED;

		return Base::create(flags)
		     | Base::Ps::bits(1)
		     | G::bits(flags.global)
		     | Pa::masked(pa)
		     | Base::Pwt::bits(wc ? 1 : 0);
	}
};


class Hw::Page_table : public Pml4_adapted_table
{
	public:
		using Allocator = Page_table_allocator<1UL << SIZE_LOG2_4KB>;
		using Pml4_adapted_table::Pml4_adapted_table;

		bool lookup_rw_translation(addr_t const, addr_t &, Allocator &)
		{
			raw(__func__, " not implemented yet");
			return false;
		}

		enum {
			TABLE_LEVEL_X_SIZE_LOG2 = SIZE_LOG2_4KB,
			CORE_VM_AREA_SIZE       = 1024 * 1024 * 1024,
			CORE_TRANS_TABLE_COUNT  =
			_count(CORE_VM_AREA_SIZE, SIZE_LOG2_512GB)
			+ _count(CORE_VM_AREA_SIZE, SIZE_LOG2_1GB)
			+ _count(CORE_VM_AREA_SIZE, SIZE_LOG2_2MB)
		};
};

#endif /* _SRC__LIB__HW__SPEC__X86_64__PAGE_TABLE_H_ */
