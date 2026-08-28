# SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: CC0-1.0

import pytest
from pytest_embedded_idf import IdfDut

@pytest.mark.esp32s3
def test_h264_hw(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(timeout=20 * 60)

@pytest.mark.esp32s31
@pytest.mark.parametrize('target', ['esp32s31'], indirect=True)
@pytest.mark.parametrize('config', ['s31_asm', 's31_esp_c'], indirect=True)
def test_h264_hw_esp32s31(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(timeout=20 * 60)


@pytest.mark.esp32p4
@pytest.mark.parametrize('target', ['esp32p4'], indirect=True)
@pytest.mark.parametrize('config', ['p4_v3'], indirect=True)
def test_h264_hw_esp32p4_v3(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(timeout=20 * 60)


@pytest.mark.esp32p4
@pytest.mark.flash_encryption
@pytest.mark.parametrize('target', ['esp32p4'], indirect=True)
@pytest.mark.parametrize('config', ['flash_enc'], indirect=True)
def test_h264_hw_flash_encryption(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(group='flash_encryption', timeout=5 * 60)
