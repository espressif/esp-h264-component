# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded_idf import IdfDut


@pytest.mark.esp32p4
def test_hw_roi_encode(dut: IdfDut) -> None:
    dut.expect(r'ROI mode=', timeout=30)
    dut.expect(r'fps=', timeout=30)
    dut.expect_exact('demo finished', timeout=60)
