#!/usr/bin/env python3
"""Regression tests for TPC-C NewOrder stock quantity updates."""

import unittest

from run_benchmark import stock_quantity_delta


class StockQuantityDeltaTests(unittest.TestCase):
    def test_subtracts_when_ten_units_remain(self):
        self.assertEqual(stock_quantity_delta(15, 5), -5)

    def test_wraps_when_subtraction_would_leave_fewer_than_ten(self):
        self.assertEqual(stock_quantity_delta(14, 5), 86)

    def test_low_stock_wrap_stays_in_legal_range(self):
        delta = stock_quantity_delta(10, 10)
        self.assertEqual(10 + delta, 91)


if __name__ == "__main__":
    unittest.main()
