/*   
	Custom IOS Module (EHCI)

	Copyright (C) 2008 neimod.
	Copyright (C) 2009 kwiirk.
	Copyright (C) 2009 Hermes.
	Copyright (C) 2009 Waninkoko.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <byteswap.h>
#include <pci/io.h>
#include <ppc/timebase.h>
#include <ppc/cache.h>
#include <xetypes.h>
#include <debug.h>

#include "ehci_types.h"
#include "ehci.h"

//#define DEBUG
//#define MEM_PRINT

/* Macros */
#ifdef DEBUG
	#define debug_printf(a...)	printf(a)
#else
	#define debug_printf(a...)
#endif

#define print_hex_dump_bytes(a, b, c, d)

#define ehci_dbg(a...)		debug_printf(a)
#define printk(a...)		debug_printf(a)
#define BUG()				while(1)
#define BUG_ON(a)			while(1)
#define cpu_to_le32(a)		bswap_32(a)
#define le32_to_cpu(a)		bswap_32(a)
#define cpu_to_le16(a)		bswap_16(a)
#define le16_to_cpu(a)		bswap_16(a)
#define cpu_to_be32(a)		(a)
#define be32_to_cpu(a)		(a)
#define ehci_readl(a)		read32((u32)a)
#define ehci_writel(v,a)	write32((u32)a,v)
#define get_timer()  (mftb()/(PPC_TIMEBASE_FREQ/1000000LL))

#define EHCI_HCD_COUNT 2

/* EHCI structure */
struct ehci_hcd ehci_hcds[EHCI_HCD_COUNT];

#include "ehci.c"

static s32 EHCI_do_one(u32 idx,u32 addr)
{
	s32 ret;
	struct ehci_hcd *ehci=&ehci_hcds[idx];
	
	/* EHCI addresses */
	ehci->caps = (void *)addr;
	ehci->regs = (void *)(addr + HC_LENGTH(ehci_readl(&ehci->caps->hc_capbase)));	
	
	printf("Initialising EHCI bus %d at %x\n",idx,addr);
	
	/* Setup EHCI */
	ehci->hcs_params = ehci_readl(&ehci->caps->hcs_params);
    
	ehci_dbg("ehci->hcs_params %x\n",ehci->hcs_params);
	
	ehci->num_port   = HCS_N_PORTS(ehci->hcs_params);
	ehci->bus_id = idx;
	
	/* Initialize EHCI */ 
	ret = ehci_init(ehci,1);
	if (ret)
		return ret;

	ehci_discover(ehci);
	
	return 0;
}


s32 EHCI_Init(void)
{
	s32 ret;
	
	ret=EHCI_do_one(0,0xea003000);
	if (ret)
		return ret; 
	
	ret=EHCI_do_one(1,0xea005000);
	return ret;
}