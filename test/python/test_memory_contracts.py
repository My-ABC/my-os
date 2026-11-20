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
        self.assertIn("if (addr == 0U)", body)
        self.assertIn("frame >= g_pmm.total_frames", body)
        self.assertIn("if (pmm_is_frame_used(frame))", body)

    def test_syscall_dispatch_and_user_heap_bounds(self):
        text = source("src/os/syscall.c")
        self.assertIn("#define USER_HEAP_END   0x40000000U", text)
        self.assertIn("address + pages * PAGE_SIZE < address", text)
        body = function_body(text, "syscall_handler")
        for call in ("SYS_WRITE", "SYS_GETPID", "SYS_MEMORY", "SYS_POWER"):
            self.assertIn("case " + call, body)


if __name__ == "__main__":
    unittest.main(verbosity=2)
