# SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import re
import sys
from typing import List, Optional

import pytest
from _pytest.nodes import Item
from pytest_embedded.plugin import multi_dut_argument, multi_dut_fixture

# ---------------------------------------------------------------------------
# Workaround: pytest-embedded <= 1.18.2 is incompatible with esptool >= 4.9.
#
# Two issues, both fixed by intercepting esptool.main() before it sees argv:
#   1. '--before default-reset' (hyphen) → '--before default_reset' (underscore)
#   2. '--flash-mode/--flash-size/--flash-freq' are passed BEFORE the
#      'write_flash' sub-command, but esptool 4.9+ only accepts them AFTER it.
# ---------------------------------------------------------------------------
try:
    import esptool as _esptool

    # esptool 4.11 uses underscores for write_flash sub-options
    _FLASH_OPT_MAP = {
        '--flash-mode': '--flash_mode',
        '--flash-size': '--flash_size',
        '--flash-freq': '--flash_freq',
    }
    _original_esptool_main = _esptool.main

    def _patched_esptool_main(argv=None, esp=None):
        if argv is not None:
            argv = list(argv)
            # Fix 1: normalise before-reset spelling (hyphen → underscore)
            argv = ['default_reset' if a == 'default-reset' else a for a in argv]
            # Fix 2: esptool 4.11 requires flash geometry opts to:
            #   a) use underscores: --flash_mode / --flash_size / --flash_freq
            #   b) appear immediately after 'write_flash', before addr/file pairs
            # pytest-embedded 1.18.2 appends them with hyphens at the list end.
            try:
                wf_pos = argv.index('write_flash')
                flash_opts, rest = [], []
                i = wf_pos + 1
                while i < len(argv):
                    if argv[i] in _FLASH_OPT_MAP and i + 1 < len(argv):
                        flash_opts += [_FLASH_OPT_MAP[argv[i]], argv[i + 1]]
                        i += 2
                    else:
                        rest.append(argv[i])
                        i += 1
                # Rebuild: global args + write_flash + flash_opts(renamed) + addr/file pairs
                argv = argv[: wf_pos + 1] + flash_opts + rest
            except ValueError:
                pass
        return _original_esptool_main(argv, esp=esp)

    _esptool.main = _patched_esptool_main
except Exception as _e:  # noqa: BLE001
    logging.warning('esptool compatibility patch failed: %s', _e)
# ---------------------------------------------------------------------------

# Full unity_run_menu suite (many 1..255 HW/SW sweeps) can run far longer than 2 minutes.
PYTEST_CASE_TIMEOUT_SEC = 30 * 60


@pytest.fixture(scope='session', autouse=True)
def idf_version() -> str:
    if os.environ.get('IDF_VERSION'):
        return os.environ.get('IDF_VERSION')
    idf_path = os.environ.get('IDF_PATH')
    if not idf_path:
        logging.warning('Failed to get IDF_VERSION!')
        return ''
    version_path = os.path.join(idf_path, 'tools/cmake/version.cmake')
    regex = re.compile(r'^\s*set\s*\(\s*IDF_VERSION_([A-Z]{5})\s+(\d+)')
    ver = {}
    with open(version_path) as f:
        for line in f:
            m = regex.match(line)
            if m:
                ver[m.group(1)] = m.group(2)
    return '{}.{}'.format(int(ver['MAJOR']), int(ver['MINOR']))


@pytest.fixture
@multi_dut_argument
def config(request: pytest.FixtureRequest) -> str:
    config_marker = list(request.node.iter_markers(name='config'))
    return config_marker[0].args[0] if config_marker else 'default'


@pytest.fixture
@multi_dut_argument
def app_path(request: pytest.FixtureRequest, test_file_path: str) -> str:
    config_marker = list(request.node.iter_markers(name='app_path'))
    if config_marker:
        return config_marker[0].args[0]
    return request.config.getoption('app_path', None) or os.path.dirname(test_file_path)


@pytest.fixture
@multi_dut_fixture
def build_dir(
    app_path: str,
    target: Optional[str],
    config: Optional[str],
    idf_version: str,
) -> Optional[str]:
    """
    Resolve prebuilt CI artifacts under build_<target>_<config>.

    Priority matches idf_build_apps --build-dir build_@t_@w:
      1. <app_path>/<idf_version>/build_<target>_<config>
      2. <app_path>/build_<target>_<config>
      3. <app_path>/build
      4. <app_path>
    """
    assert target
    assert config
    check_dirs = []
    if idf_version:
        check_dirs.append(os.path.join(idf_version, f'build_{target}_{config}'))
        check_dirs.append(os.path.join(idf_version, f'build_{target}'))
    check_dirs.append(f'build_{target}_{config}')
    check_dirs.append('build')
    check_dirs.append('.')
    for check_dir in check_dirs:
        binary_path = os.path.join(app_path, check_dir)
        if os.path.isdir(binary_path):
            logging.info('find valid binary path: %s', binary_path)
            return check_dir
        logging.warning(
            'checking binary path: %s ... missing ... try another place', binary_path)

    logging.error(
        'no build dir for %s; build with idf-build-apps first', app_path)
    sys.exit(1)


def pytest_collection_modifyitems(items: List[Item]) -> None:
    for item in items:
        if 'timeout' not in item.keywords:
            item.add_marker(pytest.mark.timeout(PYTEST_CASE_TIMEOUT_SEC))
