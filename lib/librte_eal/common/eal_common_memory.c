/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <sys/queue.h>
#include <rte_debug.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_eal_memconfig.h>
#include <rte_log.h>
#include <rte_jhash.h>
#include "eal_private.h"
#include "eal_internal_cfg.h"
#include <translation_tbl.h>
#include <rte_malloc.h>
struct phy2virt_table p2v_tbl;
struct virt2phy_table v2p_tbl;
void * rte_eal_mem_allocator(int len);
void * rte_eal_mem_allocator(int len)
{
	return rte_malloc(NULL,len,64);
}

void rte_eal_mem_deallocator(void* ptr);
void rte_eal_mem_deallocator(void* ptr)
{
	rte_free(ptr);
}
/*
 * Return a pointer to a read-only table of struct rte_physmem_desc
 * elements, containing the layout of all addressable physical
 * memory. The last element of the table contains a NULL address.
 */
int init_translation_tbl(void)
{
	struct dpdk_mmap_record records[1024*8];
	int nr_records=0;
	unsigned idx = 0;	
	int inner_idx=0;
	int rc=0;
	int nr_pages;
	uint64_t virt_base=0;
	uint64_t phy_address;
	uint64_t vir_address;
	const struct rte_mem_config *mcfg;
	mcfg = rte_eal_get_configuration()->mem_config;
	for (idx = 0; idx < RTE_MAX_MEMSEG; idx++) {
		if (mcfg->memseg[idx].addr == NULL)
			break;
		nr_pages=mcfg->memseg[idx].len/HUGEPAGE_2M;
		for(inner_idx=0;inner_idx<nr_pages;inner_idx++){
			phy_address=mcfg->memseg[idx].phys_addr+(inner_idx*HUGEPAGE_2M);
			vir_address=mcfg->memseg[idx].addr_64+(inner_idx*HUGEPAGE_2M);
			if(!virt_base)
				virt_base=vir_address;
			else if(virt_base>vir_address)
				virt_base=vir_address;
		}
		
	}
	for (nr_records=0,idx = 0; idx < RTE_MAX_MEMSEG; idx++) {
		if (mcfg->memseg[idx].addr == NULL)
			break;
		nr_pages=mcfg->memseg[idx].len/HUGEPAGE_2M;
		for(inner_idx=0;inner_idx<nr_pages;inner_idx++){
			phy_address=mcfg->memseg[idx].phys_addr+(inner_idx*HUGEPAGE_2M);
			vir_address=mcfg->memseg[idx].addr_64+(inner_idx*HUGEPAGE_2M);
			records[nr_records].phy_address=phy_address;
			records[nr_records].vir_index=((vir_address-virt_base)/HUGEPAGE_2M);
			nr_records++;
		}
		
	}
	rc=setup_phy2virt_translation_table(&p2v_tbl,
		virt_base,
		records,
		nr_records,
		rte_eal_mem_allocator,
		rte_eal_mem_deallocator);
	if(rc)
		return rc;
	for (nr_records=0,idx = 0; idx < RTE_MAX_MEMSEG; idx++) {
		if (mcfg->memseg[idx].addr == NULL)
			break;
		nr_pages=mcfg->memseg[idx].len/HUGEPAGE_2M;
		for(inner_idx=0;inner_idx<nr_pages;inner_idx++){
			phy_address=mcfg->memseg[idx].phys_addr+(inner_idx*HUGEPAGE_2M);
			vir_address=mcfg->memseg[idx].addr_64+(inner_idx*HUGEPAGE_2M);
			records[nr_records].phy_address=phy_address;
			records[nr_records].vir_address=vir_address;
			nr_records++;
		}
		
	}
	rc=setup_virt2phy_translation_table(&v2p_tbl,
		virt_base,
		records,
		nr_records,
		rte_eal_mem_allocator,
		rte_eal_mem_deallocator);
	if(rc){
		uninitialize_phy2virt_table(&p2v_tbl,rte_eal_mem_deallocator);
		return rc;
	}
	return 0;
}
void * rte_translation_virt2phy(void* ptr)
{
	return (void*)translation_virt2phy(&v2p_tbl,(uint64_t)ptr);
}
void * rte_translation_phy2virt(void *ptr)
{
	return (void*)translation_phy2virt(&p2v_tbl, (uint64_t)ptr);
}
const struct rte_memseg *
rte_eal_get_physmem_layout(void)
{
	return rte_eal_get_configuration()->mem_config->memseg;
}


/* get the total size of memory */
uint64_t
rte_eal_get_physmem_size(void)
{
	const struct rte_mem_config *mcfg;
	unsigned i = 0;
	uint64_t total_len = 0;

	/* get pointer to global configuration */
	mcfg = rte_eal_get_configuration()->mem_config;

	for (i = 0; i < RTE_MAX_MEMSEG; i++) {
		if (mcfg->memseg[i].addr == NULL)
			break;

		total_len += mcfg->memseg[i].len;
	}

	return total_len;
}

/* Dump the physical memory layout on console */
void
rte_dump_physmem_layout(FILE *f)
{
	const struct rte_mem_config *mcfg;
	unsigned i = 0;
	
	/* get pointer to global configuration */
	mcfg = rte_eal_get_configuration()->mem_config;
	for (i = 0; i < RTE_MAX_MEMSEG; i++) {
		if (mcfg->memseg[i].addr == NULL)
			break;
		//add_key_value(mcfg->memseg[i].phys_addr,mcfg->memseg[i].addr);
	/*	printf("hash :0x%x    ",rte_jhash(&mcfg->memseg[i].phys_addr,
			sizeof(mcfg->memseg[i].phys_addr),RTE_JHASH_GOLDEN_RATIO));*/
		fprintf(f, "Segment %u: phys:0x%"PRIx64", len:%zu, "
		       "virt:%p, socket_id:%"PRId32", "
		       "hugepage_sz:%"PRIu64", nchannel:%"PRIx32", "
		       "nrank:%"PRIx32"\n", i,
		       mcfg->memseg[i].phys_addr,
		       mcfg->memseg[i].len,
		       mcfg->memseg[i].addr,
		       mcfg->memseg[i].socket_id,
		       mcfg->memseg[i].hugepage_sz,
		       mcfg->memseg[i].nchannel,
		       mcfg->memseg[i].nrank);
	}
}

/* return the number of memory channels */
unsigned rte_memory_get_nchannel(void)
{
	return rte_eal_get_configuration()->mem_config->nchannel;
}

/* return the number of memory rank */
unsigned rte_memory_get_nrank(void)
{
	return rte_eal_get_configuration()->mem_config->nrank;
}

static int
rte_eal_memdevice_init(void)
{
	struct rte_config *config;

	if (rte_eal_process_type() == RTE_PROC_SECONDARY)
		return 0;

	config = rte_eal_get_configuration();
	config->mem_config->nchannel = internal_config.force_nchannel;
	config->mem_config->nrank = internal_config.force_nrank;

	return 0;
}

/* init memory subsystem */
int
rte_eal_memory_init(int init_flag)
{
	RTE_LOG(DEBUG, EAL, "Setting up physically contiguous memory...\n");
	
	const int retval = rte_eal_process_type() == RTE_PROC_PRIMARY ?
			(init_flag?rte_eal_hugepage_init():rte_eal_hugepage_init_from_metadata()) :
			rte_eal_hugepage_attach();

			
	if (retval < 0)
		return -1;

	if (internal_config.no_shconf == 0 && rte_eal_memdevice_init() < 0)
		return -1;
	
	return 0;
}
