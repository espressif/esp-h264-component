# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded_idf import IdfDut


@pytest.mark.esp32p4
def test_hw_dual_enc_ppa_one(dut: IdfDut) -> None:
    dut.expect(r'stream0 \(PPA \d+x\d+\) fps=', timeout=30)
    dut.expect(r'stream1 \(src \d+x\d+\) fps=', timeout=30)
    dut.expect_exact('demo finished', timeout=60)
