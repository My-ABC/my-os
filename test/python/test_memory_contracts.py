import unittest

from contract_support import function_body, source


class MemoryContractTests(unittest.TestCase):
    def test_heap_rejects_invalid_frees_and_zero_allocations(self):
        text = source("src/mm/heap.c")
        alloc = function_body(text, "kmalloc")
        free = function_body(text, "kfree")
        self.assertIn("requested == 0U", alloc)
        self.assertIn("HEAP_BLOCK_MAGIC", free)
        self.assertIn("block->free != 0U", free)

    def test_paging_never_grants_user_flag_to_kernel_address(self):
        text = source("src/mm/paging.c")
        body = function_body(text, "paging_map_page")
        self.assertIn("paging_is_user_address(virtual_addr)", body)
        self.assertIn("(flags & PAGE_USER) != 0U", body)
        self.assertIn("return virtual_addr < USER_SPACE_END",
                      function_body(text, "paging_is_user_address"))

    def test_pmm_free_ignores_out_of_range_and_already_free_frames(self):
        body = function_body(source("src/mm/pmm.c"), "pmm_free_page")
        self.assertIn("physical_addr == 0U", body)
        self.assertIn("physical_addr >= PMM_PHYSICAL_CEILING", body)
        self.assertIn("frame < g_pmm.total_frames", body)
        self.assertIn("pmm_is_frame_used(frame)", body)
        self.assertIn("pmm_set_frame(frame, 0U)", body)

    def test_pmm_reads_multiboot_memory_map_fields(self):
        pmm = source("src/mm/pmm.c")
        self.assertIn("#define MULTIBOOT_INFO_MMAP_LENGTH_INDEX 11U", pmm)
        self.assertIn("#define MULTIBOOT_INFO_MMAP_ADDR_INDEX 12U", pmm)
        mmap = function_body(pmm, "pmm_walk_mmap")
        self.assertIn("info[MULTIBOOT_INFO_MMAP_ADDR_INDEX]", mmap)
        self.assertIn("info[MULTIBOOT_INFO_MMAP_LENGTH_INDEX]", mmap)
        self.assertIn("sizeof(uint32_t) * MULTIBOOT_INFO_MMAP_END_INDEX", pmm)

    def test_pae_64_bit_entries_cr3_and_64_gib_ceiling(self):
        paging = source("src/mm/paging.c")
        header = source("include/mm/paging.h")
        enable = source("boot/paging.asm")
        pmm = source("include/mm/pmm.h")
        boot = source("boot/boot.asm")
        self.assertIn("uint64_t value;", header)
        self.assertIn("0x0000000FFFFFF000ULL", paging)
        self.assertIn("PMM_PHYSICAL_CEILING", pmm)
        self.assertIn("1ULL << PMM_PHYSICAL_BITS", pmm)
        self.assertIn("36-bit PAE paging", paging)
        self.assertIn("paging_map_physical", paging)
        map_physical = function_body(paging, "paging_map_physical")
        self.assertIn("paging_disable_interrupts()", map_physical)
        unmap_physical = function_body(paging, "paging_unmap_physical")
        self.assertIn("paging_restore_interrupts(kmap_saved_eflags)", unmap_physical)
        pmm_source = source("src/mm/pmm.c")
        self.assertIn("((uint64_t)entry->addr_high << 32)", pmm_source)
        self.assertIn("((uint64_t)entry->len_high << 32)", pmm_source)
        self.assertIn("pmm_set_range(base, length", pmm_source)
        self.assertIn("or edx, 0x20", enable)
        self.assertIn("mov cr3, eax", enable)
        self.assertIn("early_pae_pdpt", boot)
        self.assertIn("test edx, 1 << 6", boot)
        self.assertIn("mov ebx, [esp]", boot)
        self.assertIn("PMM_LEGACY_PHYSICAL_CEILING", pmm_source)
        self.assertIn("end > PMM_LEGACY_PHYSICAL_CEILING", pmm_source)

    def test_syscall_dispatch_and_user_heap_bounds(self):
        text = source("src/os/syscall.c")
        header = source("include/mm/paging.h")
        self.assertIn("#define USER_HEAP_END   0x40000000U", header)
        self.assertIn("address + pages * PAGE_SIZE < address", text)
        body = function_body(text, "syscall_handler")
        for call in ("SYS_IO", "SYS_PROCESS", "SYS_MEMORY", "SYS_POWER"):
            self.assertIn("case " + call, body)


if __name__ == "__main__":
    unittest.main(verbosity=2)
