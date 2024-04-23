/*
 * \brief   Generic page flags
 * \author  Stefan Kalkowski
 * \date    2014-02-24
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__LIB__HW__PAGE_FLAGS_H_
#define _SRC__LIB__HW__PAGE_FLAGS_H_

#include <base/cache.h>
#include <base/output.h>
#include <page_table/page_flags.h>

namespace Hw {
	using Genode::RO;
	using Genode::RW;

	using Genode::NO_EXEC;
	using Genode::EXEC;

	using Genode::USER;
	using Genode::KERN;

	using Genode::NO_GLOBAL;
	using Genode::GLOBAL;

	using Genode::RAM;
	using Genode::DEVICE;

	using Genode::Page_flags;
}


namespace Hw {

	static constexpr Page_flags PAGE_FLAGS_KERN_IO
		{ RW, NO_EXEC, KERN, GLOBAL, DEVICE, Genode::UNCACHED };
	static constexpr Page_flags PAGE_FLAGS_KERN_DATA
		{ RW, EXEC,    KERN, GLOBAL, RAM,    Genode::CACHED   };
	static constexpr Page_flags PAGE_FLAGS_KERN_TEXT
		{ RW, EXEC,    KERN, GLOBAL, RAM,    Genode::CACHED   };
	static constexpr Page_flags PAGE_FLAGS_KERN_EXCEP
		{ RW, EXEC,    KERN, GLOBAL, RAM,    Genode::CACHED   };
	static constexpr Page_flags PAGE_FLAGS_UTCB
		{ RW, NO_EXEC, USER, NO_GLOBAL, RAM, Genode::CACHED   };
}

#endif /* _SRC__LIB__HW__PAGE_FLAGS_H_ */
