import unittest

from contract_support import source


class ElfContractTests(unittest.TestCase):
    def test_elf_validation_suite_covers_malformed_images(self):
        test = source("test/c/test_elf.c")
        for case in (
            "rejects null image",
            "rejects truncated header",
            "rejects invalid identification",
            "rejects truncated program-header table",
            "rejects filesz larger than memsz",
            "rejects segment outside image",
            "requires entry in executable segment",
        ):
            with self.subTest(case=case):
                self.assertIn(case, test)


if __name__ == "__main__":
    unittest.main(verbosity=2)
