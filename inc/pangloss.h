#ifndef PANGLOSS_H
#define PANGLOSS_H
#pragma once

#include <stdint.h>


#define WORD_SIZE_OFFSET 2
#define PAGE_SIZE_OFFSET 12

// ====================== 定义 l1d cache 预取的相关数据结构 ======================

#define L1D_PREFETCH_DEGREE 36

// l1d Delta Cache 相关数据
// l1d Delta Cache 大小为 1024 sets * 16 ways
#define L1D_DELTA_CACHE_SETS 1024
#define L1D_DELTA_CACHE_WAYS 16
// LFU 计数的最大值 由于 LFU 共8位 所以取值 128
#define L1D_DELTA_CACHE_MAX_LFU 128
// l1d Delta Cache 表项
struct L1DDeltaCacheEntry {
    int next_delta;
    int LFU_count; 
};
// l1d Delta Cache
static L1DDeltaCacheEntry L1D_Delta_Cache[L1D_DELTA_CACHE_SETS][L1D_DELTA_CACHE_WAYS];

// l1d Page Cache 相关数据
// l1d Page Cache 大小为 256 sets * 12 ways
#define L1D_PAGE_CACHE_SETS 256
#define L1D_PAGE_CACHE_WAYS 12
// page tag 位宽为10
#define L1D_PAGE_CACHE_TAGE_BITS 10
// l1d Page Cache 表项
struct L1DPageCacheEntry {
    int page_tag;
    int last_delta;
    int last_offset;
    int NRU_bit; 
};
// l1d Page Cache
static L1DPageCacheEntry L1D_Page_Cache[L1D_PAGE_CACHE_SETS][L1D_PAGE_CACHE_WAYS];

/*
* 根据给定的page 返回 page tag
*/
static int get_l1d_page_tag(uint64_t page) {
    // 取高位
    uint64_t high_bits = page / L1D_PAGE_CACHE_SETS;
    // 取高位中的低10位
    int page_tag = high_bits & ((1 << L1D_PAGE_CACHE_TAGE_BITS) - 1);
    return page_tag;
}

/*
* 更新 l1d cache
* 增加了一个 delta transition (delta_from -> delta_to)
*/
static void update_l1d_delta_cache(int delta_from, int delta_to) {
	// 首先检查 delta_to 是否在 Delta Cache 中命中、
	// 此时同时检查 LFU 最小的 way
	int lfu_way = 0;
	int min_uses = 1e9;
	for (int i = 0; i < L1D_DELTA_CACHE_WAYS; i++) {
		if (L1D_Delta_Cache[delta_from][i].next_delta == delta_to) {
			L1D_Delta_Cache[delta_from][i].LFU_count++;
			// 如果发生了溢出 则整体折半 
			if (L1D_Delta_Cache[delta_from][i].LFU_count == L1D_DELTA_CACHE_MAX_LFU) {
				for (int j = 0; j < L1D_DELTA_CACHE_WAYS; j++) {
					L1D_Delta_Cache[delta_from][j].LFU_count /= 2;
				}
			}		
			return;
		}
		if(L1D_Delta_Cache[delta_from][i].LFU_count < min_uses) {
			min_uses = L1D_Delta_Cache[delta_from][i].LFU_count;
			lfu_way = i;
		}
	}
	
	// 如果这个 delta transition 不在 Delta Cache 中
	// 则需要根据 LFU 逐出一项 并插入新的 delta transition
	L1D_Delta_Cache[delta_from][lfu_way].next_delta = delta_to;
	L1D_Delta_Cache[delta_from][lfu_way].LFU_count = 1; 
}


/*
* 根据当前的 delta 确定下一次最可能选择的 next_delta
*/
static int get_l1d_next_best_transition (int delta) {
	// 计算在当前 set 中 LFU_count 的总和 进而计算概率
	// 并且找到最大的 LFU 值和对应的 way
	int set_LFU_sum = 0;
	int max_LFU = -1;
	int max_LFU_way = 0;
	for(int j = 0; j < L1D_DELTA_CACHE_WAYS; j++) {
		int lfu_count = L1D_Delta_Cache[delta][j].LFU_count;
		set_LFU_sum += lfu_count;
		if(lfu_count > max_LFU) {
			max_LFU = lfu_count;
			max_LFU_way = j;
		}
	}
	// 如果最大概率低于 1/3 则返回无效值 -1
	if(L1D_Delta_Cache[delta][max_LFU_way].LFU_count * 3 < set_LFU_sum) {
		return -1;
	}
	return L1D_Delta_Cache[delta][max_LFU_way].next_delta;
}

// ====================== 定义 l2c cache 预取的相关数据结构 ======================

// l2c Delta Cache 相关数据
// l2c Delta Cache 大小为 128 sets * 16 ways
#define L2C_DELTA_CACHE_SETS 128
#define L2C_DELTA_CACHE_WAYS 16
// LFU 计数的最大值 由于 LFU 共8位 所以取值 256
#define L2C_DELTA_CACHE_MAX_LFU 256
// l2c Delta Cache 表项
struct L2CDeltaCacheEntry {
    int next_delta;
    int LFU_count; 
};
// l2c Delta Cache
static L2CDeltaCacheEntry L2C_Delta_Cache[L2C_DELTA_CACHE_SETS][L2C_DELTA_CACHE_WAYS];

// l2c Page Cache 相关数据
// l2c Page Cache 大小为 256 sets * 12 ways
#define L2C_PAGE_CACHE_SETS 256
#define L2C_PAGE_CACHE_WAYS 12
// page tag 位宽为10
#define L2C_PAGE_CACHE_TAGE_BITS 10
// l2c Page Cache 表项
struct L2CPageCacheEntry {
    int page_tag;
    int last_delta;
    int last_offset;
    int NRU_bit; 
};
// l2c Page Cache
static L2CPageCacheEntry L2C_Page_Cache[L2C_PAGE_CACHE_SETS][L2C_PAGE_CACHE_WAYS];

/*
* 根据给定的page 返回 page tag
*/
static int get_l2c_page_tag(uint64_t page) {
    // 取高位
    uint64_t high_bits = page / L2C_PAGE_CACHE_SETS;
    // 取高位中的低10位
    int page_tag = high_bits & ((1 << L2C_PAGE_CACHE_TAGE_BITS) - 1);
    return page_tag;
}

/*
* 更新 l2c cache
* 增加了一个 delta transition (delta_from -> delta_to)
*/
static void update_l2c_delta_cache(int delta_from, int delta_to) {
	// 首先检查 delta_to 是否在 Delta Cache 中命中、
	// 此时同时检查 LFU 最小的 way
	int lfu_way = 0;
	int min_uses = 1e9;
	for (int i = 0; i < L2C_DELTA_CACHE_WAYS; i++) {
		if (L2C_Delta_Cache[delta_from][i].next_delta == delta_to) {
			L2C_Delta_Cache[delta_from][i].LFU_count++;
			// 如果发生了溢出 则整体折半 
			if (L2C_Delta_Cache[delta_from][i].LFU_count == L2C_DELTA_CACHE_MAX_LFU) {
				for (int j = 0; j < L2C_DELTA_CACHE_WAYS; j++) {
					L2C_Delta_Cache[delta_from][j].LFU_count /= 2;
				}
			}		
			return;
		}
		if(L2C_Delta_Cache[delta_from][i].LFU_count < min_uses) {
			min_uses = L2C_Delta_Cache[delta_from][i].LFU_count;
			lfu_way = i;
		}
	}
	
	// 如果这个 delta transition 不在 Delta Cache 中
	// 则需要根据 LFU 逐出一项 并插入新的 delta transition
	L2C_Delta_Cache[delta_from][lfu_way].next_delta = delta_to;
	L2C_Delta_Cache[delta_from][lfu_way].LFU_count = 1; 
}


/*
* 根据当前的 delta 确定下一次最可能选择的 next_delta
*/
static int get_l2c_next_best_transition (int delta) {
	// 计算在当前 set 中 LFU_count 的总和 进而计算概率
	// 并且找到最大的 LFU 值和对应的 way
	int set_LFU_sum = 0;
	int max_LFU = -1;
	int max_LFU_way = 0;
	for(int j = 0; j < L2C_DELTA_CACHE_WAYS; j++) {
		int lfu_count = L2C_Delta_Cache[delta][j].LFU_count;
		set_LFU_sum += lfu_count;
		if(lfu_count > max_LFU) {
			max_LFU = lfu_count;
			max_LFU_way = j;
		}
	}
	// 如果最大概率低于 1/3 则返回无效值 -1
	if(L2C_Delta_Cache[delta][max_LFU_way].LFU_count * 3 < set_LFU_sum) {
		return -1;
	}
	return L2C_Delta_Cache[delta][max_LFU_way].next_delta;
}

#endif // PANGLOSS_H