#ifndef _PAGING_H
#define _PAGING_H

#include "stdint.h"

void paging_init(void);
void paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags);
void paging_unmap_page(uint32_t virt);
uint32_t paging_get_phys(uint32_t virt);

// 供汇编调用的缺页异常处理
void page_fault_handler(void);

#endif