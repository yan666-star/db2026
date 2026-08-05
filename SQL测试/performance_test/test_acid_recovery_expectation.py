#!/usr/bin/env python3
"""Regression tests for ACID crash-recovery expectations."""

import unittest

from run_acid_tests import expected_abort_recovery_state


class AbortRecoveryExpectationTests(unittest.TestCase):
    def test_preserves_preexisting_committed_value(self):
        baseline = {"updated": 89, "deleted": 1, "followup": 0}

        expected = expected_abort_recovery_state(
            baseline, expect_followup=True)

        self.assertEqual(expected, {
            "updated": 89,
            "deleted": 1,
            "followup": 1,
        })
        self.assertEqual(baseline["followup"], 0)


if __name__ == "__main__":
    unittest.main()
