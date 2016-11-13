#include <stdio.h>
#include <vlink_channel.h>
#include <vlink_queue.h>
#include <agent.h>
#include <inttypes.h>
#include <dpdk-mapping.h>
#define TEST_BURST_SIZE 64
uint64_t shm_base;
struct dpdk_physeg_str * p2v_hashtbl;
struct dpdk_physeg_str * v2p_hashtbl;
	

struct dpdk_buf{
	union{
		unsigned char dummy0[128];		
		struct {			
			unsigned char dummy1[0];			
			void * buf_addr;/*offset is 0*/		
		};
		struct{			
			unsigned char dummy2[16];			
			uint16_t buf_len;			
			uint16_t data_off;		
		};		
		struct {			
			unsigned char dummy3[36];			
			uint32_t pkt_len;			
			uint16_t data_len;		
		};	
	};
}__attribute__((packed));

void emulate_channel(int ctrl_channel,int data_channel_index,char* vm_domain_name,int max_channel)
{
	struct queue_element qe_rx[TEST_BURST_SIZE];
	int nr_dequeued_rx;
	int nr_enqueued_tx;
	int cnt_total_rx=0;
	struct dpdk_buf *buf;
	int idx;
	int ipt_idx;
	unsigned char * pkt;
	#if 0
	uint64_t vm_base=map_shared_memory(vm_domain_name,max_channel*CHANNEL_SIZE);
	#else
	unsigned long nodemask=0x2;
	uint64_t vm_base=map_shared_memory_from_sockets(vm_domain_name,max_channel*CHANNEL_SIZE,&nodemask);
	#endif
	
	struct ctrl_channel *ctrl=PTR_OFFSET_BY(struct ctrl_channel*,vm_base,channel_base_offset(ctrl_channel));
	struct queue_stub *rx_stub=PTR_OFFSET_BY(struct queue_stub*,vm_base,ctrl->channel_records[data_channel_index].offset_to_vm_shm_base+rx_sub_channel_offset());
	struct queue_stub *free_stub=PTR_OFFSET_BY(struct queue_stub*,vm_base,ctrl->channel_records[data_channel_index].offset_to_vm_shm_base+free_sub_channel_offset());
	struct queue_stub *alloc_stub=PTR_OFFSET_BY(struct queue_stub*,vm_base,ctrl->channel_records[data_channel_index].offset_to_vm_shm_base+alloc_sub_channel_offset());
	struct queue_stub *tx_stub=PTR_OFFSET_BY(struct queue_stub*,vm_base,ctrl->channel_records[data_channel_index].offset_to_vm_shm_base+tx_sub_channel_offset());
    printf("data channel offset:%d\n",ctrl->channel_records[data_channel_index].offset_to_vm_shm_base);
	while(1){
		nr_dequeued_rx=dequeue_bulk(rx_stub,qe_rx,TEST_BURST_SIZE);
		cnt_total_rx+=nr_dequeued_rx;


        #if   1
		for(idx=0;idx<nr_dequeued_rx;idx++){
			buf=PTR(struct dpdk_buf*,translate_phy_address(p2v_hashtbl,qe_rx[idx].rte_pkt_offset));
			pkt=PTR_OFFSET_BY(unsigned char*,translate_phy_address(p2v_hashtbl,qe_rx[idx].rte_data_offset),buf->data_off);
			//printf("rx %02x:%02x:%02x:%02x:%02x:%02x %d\n",pkt[0+6-6],pkt[1+6-6],pkt[2+6-6],pkt[3+6-6],pkt[4+6-6],pkt[5+6-6],buf->pkt_len);
			/*for test reason,change one bytes*/
		#if 1
		for(ipt_idx=0;ipt_idx<buf->pkt_len;ipt_idx++)
			pkt[ipt_idx]=pkt[12+ipt_idx-12];
   		#endif
		}
    	#endif
		//if(nr_dequeued_rx)
			//printf("%d %d %d\n",cnt_total_rx,nr_dequeued_rx,rx_stub->ele_num-queue_available_quantum(rx_stub));
		if(nr_dequeued_rx){
			nr_enqueued_tx=enqueue_bulk(tx_stub,qe_rx,nr_dequeued_rx);
			/*if there still are some pkts who remain unsent,send them into free*/
			if((nr_dequeued_rx-nr_enqueued_tx)>0)
				enqueue_bulk(free_stub,&qe_rx[nr_enqueued_tx],nr_dequeued_rx-nr_enqueued_tx);
				
		}
		
	}
	
}
int main(int argc,char**argv)
{
    if(argc!=2)
        return -1;
	struct dpdk_mmap_record*record=alloc_dpdk_mmap_record_array();
	int nr_pages=load_dpdk_memory_metadata(record);
	shm_base=preserve_vm_area(nr_pages,NULL);
	map_vma_to_hugepages(shm_base,record,nr_pages);
	
	p2v_hashtbl=alloc_physeg_hash_table();
	v2p_hashtbl=alloc_physeg_hash_table();
	setup_phy2virt_hash_tbl(p2v_hashtbl,record,nr_pages);        
	setup_virt2phy_hash_tbl(v2p_hashtbl,record,nr_pages);
	
	emulate_channel(0,atoi(argv[1]),"cute-meeeow",20);

	
	
	
	return 0;
}
