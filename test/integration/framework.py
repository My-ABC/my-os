"""Minimal unittest-style helpers for QEMU integration checks.

A check can be either a plain function `check(session)` or a
`QemuTestCase` subclass whose `test_*` methods run in sorted order
against the shared session. Test methods use the assert* helpers
instead of calling session.pass_check directly.
"""
import re

from common import IntegrationFailure


class QemuTestCase:
    def __init__(self, session):
        self.session = session

    def setUp(self):
        pass

    def tearDown(self):
        pass

    def fail(self, message="assertion failed"):
        raise IntegrationFailure(message or "assertion failed")

    def assertTrue(self, expr, msg=""):
        if not expr:
            self.fail(msg or f"assertTrue failed: {expr!r}")

    def assertFalse(self, expr, msg=""):
        if expr:
            self.fail(msg or f"assertFalse failed: {expr!r}")

    def assertEqual(self, first, second, msg=""):
        if first != second:
            self.fail(msg or f"{first!r} != {second!r}")

    def assertNotEqual(self, first, second, msg=""):
        if first == second:
            self.fail(msg or f"{first!r} == {second!r}")

    def assertIn(self, needle, haystack, msg=""):
        if needle not in haystack:
            self.fail(msg or f"{needle!r} not found in {haystack!r}")

    def assertNotIn(self, needle, haystack, msg=""):
        if needle in haystack:
            self.fail(msg or f"{needle!r} unexpectedly found")

    def assertRegex(self, text, pattern, msg=""):
        if re.search(pattern, text) is None:
            self.fail(msg or f"pattern {pattern!r} not found in {text!r}")

    def assertIsNone(self, value, msg=""):
        if value is not None:
            self.fail(msg or f"expected None, got {value!r}")

    def assertIsNotNone(self, value, msg=""):
        if value is None:
            self.fail(msg or "expected a value, got None")
