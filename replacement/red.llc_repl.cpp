#include "cache.h"
#include "ReD_repl.h"

// 定义 ReD
ReD_Replacement ReD;

// ================================ 定义 SRRIP 相关数据 ================================
#define MAX_RRPV 3
uint32_t rrpv[LLC_SET][LLC_WAY];

void CACHE::llc_initialize_replacement() {
    // 初始化 SRRIP
    for(int i = 0; i < LLC_SET; i++) {
        for(int j = 0; j < LLC_WAY; j++) {
            rrpv[i][j] = MAX_RRPV;
        }
    }
    // 初始化 ReD
    ReD.initialize();
}

uint32_t CACHE::llc_find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type) {
    // 判断是否需要 bypass
    if(ReD.bypass(full_addr, ip, type)) {
        return LLC_WAY;
    }
    // 如果是 WB 操作
    // 或者访问的地址在 ART 中命中
    // 或者访问的地址在 PCRT 中具有较高的 reused
    // 则不需要进行 bypass 正常按照 SRRIP 进行处理即可
    while(true) {
        for(int i = 0; i < LLC_WAY; i++) {
            if(rrpv[set][i] == MAX_RRPV) {
                return i;
            }
        }
        for(int i = 0; i < LLC_WAY; i++) {
            rrpv[set][i]++;
        }
    }
    return 0;
}

void CACHE::llc_update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way, uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit) {
    string TYPE_NAME;
    if (type == LOAD)
        TYPE_NAME = "LOAD";
    else if (type == RFO)
        TYPE_NAME = "RFO";
    else if (type == PREFETCH)
        TYPE_NAME = "PF";
    else if (type == WRITEBACK)
        TYPE_NAME = "WB";
    else
        assert(0);

    if (hit)
        TYPE_NAME += "_HIT";
    else
        TYPE_NAME += "_MISS";

    if ((type == WRITEBACK) && ip)
        assert(0);

    // uncomment this line to see the LLC accesses
    // cout << "CPU: " << cpu << "  LLC " << setw(9) << TYPE_NAME << " set: " << setw(5) << set << " way: " << setw(2) << way;
    // cout << hex << " paddr: " << setw(12) << paddr << " ip: " << setw(8) << ip << " victim_addr: " << victim_addr << dec << endl;

	// Do not update when bypassing
	if (way == 16) return;  

	// Write-backs do not change rrpv
	if (type == WRITEBACK) return; 

	// SRRIP
	if (hit)
		rrpv[set][way] = 0;
	else
		rrpv[set][way] = MAX_RRPV-1;

}

void CACHE::llc_replacement_final_stats(){

}