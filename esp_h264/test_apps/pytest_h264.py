# SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: CC0-1.0

import pytest
from pytest_embedded_idf import IdfDut

@pytest.mark.esp32s3
def test_h264_hw(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(timeout=20 * 60)

@pytest.mark.esp32s31
@pytest.mark.esp32p4
@pytest.mark.parametrize('target', ['esp32s31', 'esp32p4'], indirect=True)
def test_h264_hw_v61(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(timeout=20 * 60)
