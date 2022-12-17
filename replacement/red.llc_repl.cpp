#include "cache.h"

#define LLC_BLOCK_OFFSET 6
#define LLC_WORD_OFFSET 2

// ================================ 定义 ART 相关数据 ================================
#define ART_SETS 512
#define ART_WAYS 16
#define ART_PAT_BITS 11
#define ART_SECTOR_BLOCKS 4
#define ART_SET_FIFO_BITS 4

struct ART_Entry {
    uint16_t pat;
    bool valid[ART_SECTOR_BLOCKS];
};

struct ART_Set {
    struct ART_Entry entrys[ART_WAYS];
    uint8_t fifo_bits;
};

struct ART_Set ART[ART_SETS];

// ================================ 定义 ART Sampled set 相关数据 ================================
#define ART_SAMPLED_SET_SETS 128
#define ART_SAMPLED_SET_WAYS 16
#define ART_SAMPLED_SET_BLOCKS 4
#define ART_SAMPLED_SET_PC_BITS 8

struct ART_SAMPLED_SET_Entry {
    uint8_t pc_indexes[ART_SAMPLED_SET_BLOCKS];
};

struct ART_SAMPLED_SET_Entry ART_SAMPLED_SET[ART_SAMPLED_SET_SETS][ART_SAMPLED_SET_WAYS];

// ================================ 定义 PCRT 相关数据 ================================
#define PCRT_SIZE 256

struct PCRT_Entry {
    uint16_t not_reused;
    uint16_t reused;
};

struct PCRT_Entry PCRT[PCRT_SIZE];

// ================================ 定义 SRRIP 相关数据 ================================
#define MAX_RRPV 3
uint32_t rrpv[LLC_SET][LLC_WAY];

int misses = 0;

bool ART_find_block(uint64_t pc, uint64_t block) {
    misses++;

    uint64_t set_index = (block / ART_SECTOR_BLOCKS) % ART_SETS;
    uint16_t pat = (block / (ART_SECTOR_BLOCKS * ART_SETS)) % ART_SETS;
    uint16_t sector_index = block % ART_SECTOR_BLOCKS;

    for(int i = 0; i < ART_WAYS; i++) {
        if(ART[set_index].entrys[i].pat == pat
            && ART[set_index].entrys[i].valid[sector_index] == 1) {
            if(set_index % ART_SECTOR_BLOCKS == 0) {
                uint64_t pc_index = ART_SAMPLED_SET[set_index / ART_SECTOR_BLOCKS][i].pc_indexes[sector_index];
                PCRT[pc_index].reused++;
                // 如果发生溢出 则折半
                if(PCRT[pc_index].reused > 1023) {
                    PCRT[pc_index].reused /= 2;
                    PCRT[pc_index].not_reused /= 2;
                }
                // 使得对应的 valid 位无效
                ART[set_index].entrys[i].valid[sector_index] = 0;
            }
            return 1;
        }
    }
    return 0;
}

void ART_add_block(uint64_t pc, uint64_t block) {
    uint64_t set_index = (block / ART_SECTOR_BLOCKS) % ART_SETS;
    uint16_t pat = (block / (ART_SECTOR_BLOCKS * ART_SETS)) % ART_SETS;
    uint16_t sector_index = block % ART_SECTOR_BLOCKS;
    uint64_t pc_index = (pc >> LLC_WORD_OFFSET) % (1 << ART_SAMPLED_SET_PC_BITS);

    // 查询在 ART 中是否命中
    int where;
    int way;
    for(way = 0; way < ART_WAYS; way++) {
        if(ART[set_index].entrys[way].pat == pat) {
            break;
        }
    }
    // 如果命中
    if(way != ART_WAYS) {
        // 有效位置为 1
        ART[set_index].entrys[way].valid[sector_index] = 1;
        if(set_index % ART_SECTOR_BLOCKS == 0) {
            ART_SAMPLED_SET[set_index / ART_SECTOR_BLOCKS][way].pc_indexes[sector_index] = pc_index;
        }
    }
    // 如果没有命中
    else {
        where = ART[set_index].fifo_bits;
        if(set_index % ART_SECTOR_BLOCKS == 0) {
            for(int j = 0; j < ART_SECTOR_BLOCKS; j++) {
                uint64_t evict_pc_index = ART_SAMPLED_SET[set_index / ART_SECTOR_BLOCKS][where].pc_indexes[j];
                PCRT[evict_pc_index].not_reused++;
                if(PCRT[pc_index].not_reused > 1023) {
                    PCRT[pc_index].reused /= 2;
                    PCRT[pc_index].not_reused /= 2;
                }
            }
        }
        // 进行替换
        ART[set_index].entrys[where].pat = pat;
        for(int j = 0; j < ART_SECTOR_BLOCKS; j++) {
            if(j == sector_index) {
                ART[set_index].entrys[where].valid[j] = 1;
            }
            else {
                ART[set_index].entrys[where].valid[j] = 0;
            }
        }
        if(set_index % ART_SECTOR_BLOCKS == 0) {
            ART_SAMPLED_SET[set_index / ART_SECTOR_BLOCKS][where].pc_indexes[sector_index] = pc_index;
        }
        ART[set_index].fifo_bits++;
        if(ART[set_index].fifo_bits == ART_WAYS) {
            ART[set_index].fifo_bits = 0;
        }
    }
}

void CACHE::llc_initialize_replacement() {
    // 初始化 ART
    for(int i = 0; i < ART_SETS; i++) {
        ART[i].fifo_bits = 0;
        for(int j = 0; j < ART_WAYS; j++) {
            ART[i].entrys[j].pat = 0;
            for(int k = 0; k < ART_SECTOR_BLOCKS; k++) {
                ART[i].entrys[j].valid[k] = 0;
            }
        }
    }
    // 初始化 ART Sampled set
    for(int i = 0; i < ART_SAMPLED_SET_SETS; i++) {
        for(int j = 0; j < ART_SAMPLED_SET_WAYS; j++) {
            for(int k = 0; k < ART_SAMPLED_SET_BLOCKS; k++) {
                ART_SAMPLED_SET[i][j].pc_indexes[k] = 0;
            }
        }
    }
    // 初始化 PCRT
    for(int i = 0; i < PCRT_SIZE; i++) {
        PCRT[i].reused = 3;
        PCRT[i].not_reused = 0;
    }
    // 初始化 SRRIP
    for(int i = 0; i < LLC_SET; i++) {
        for(int j = 0; j < LLC_WAY; j++) {
            rrpv[i][j] = MAX_RRPV;
        }
    }

    misses = 0;
}

uint32_t CACHE::llc_find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type) {
    uint64_t block = full_addr >> LLC_BLOCK_OFFSET;
    uint64_t pc_index = (ip >> LLC_WORD_OFFSET) % (1 << ART_SAMPLED_SET_PC_BITS);
    if(type == LOAD || type == RFO || type == PREFETCH) {
        // 如果在 ART 中不能找到对应的 block
        if(!ART_find_block(ip, block)) {
            if(PCRT[pc_index].reused * 64 > PCRT[pc_index].not_reused
                && PCRT[pc_index].reused * 3 < PCRT[pc_index].not_reused
                || (misses % 8 == 0)) {
                ART_add_block(ip, block);
            }
            if(PCRT[pc_index].reused * 3 < PCRT[pc_index].not_reused) {
                // 直接 bypass 不需要进行存储
                return LLC_WAY;
            }
        }
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