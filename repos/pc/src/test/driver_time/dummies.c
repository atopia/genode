/*
 * \brief  Dummy definitions of Linux Kernel functions - handled manually
 * \author Alexander Boettcher
 * \date   2022-07-01
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <lx_emul/debug.h>
#include <linux/math64.h>

#include <linux/clocksource.h>
#include <linux/cpuhotplug.h>
#include <linux/kernel_stat.h>
#include <linux/kernfs.h>
#include <linux/kobject.h>
#include <linux/nls.h>
#include <linux/property.h>
#include <linux/random.h>
#include <linux/rcupdate.h>
#include <linux/sched/loadavg.h>
#include <linux/sched/signal.h>
#include <linux/syscore_ops.h>
#include <linux/timekeeper_internal.h>


void calc_load_nohz_start(void)
{
	lx_emul_trace(__func__);
}


void calc_load_nohz_stop(void)
{
	lx_emul_trace(__func__);
}


void account_idle_ticks(unsigned long ticks)
{
	lx_emul_trace(__func__);
}



bool irq_work_needs_cpu(void)
{
	return false;
}


int ___ratelimit(struct ratelimit_state * rs, const char * func)
{
	/*
	 * from lib/ratelimit.c:
	 * " 0 means callbacks will be suppressed.
	 *   1 means go ahead and do it. "
	 */
	lx_emul_trace(__func__);
	return 1;
}

void register_syscore_ops(struct syscore_ops * ops)
{
	lx_emul_trace(__func__);
}
