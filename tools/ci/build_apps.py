# SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import os
import subprocess
import sys
from pathlib import Path

from idf_build_apps import __version__ as idf_build_apps_version
from idf_build_apps.constants import SUPPORTED_TARGETS

DEFAULT_IGNORE_WARNING_FILEPATH = [os.path.join('tools', 'ci', 'ignore_build_warnings.txt')]
PREVIEW_TARGETS = ['esp32p4']
# EXTRA_PREVIEW_TARGETS: comma-separated list injected by CI matrix (e.g. "esp32s31" for IDF master)
_extra = os.getenv('EXTRA_PREVIEW_TARGETS', '')
if _extra:
    PREVIEW_TARGETS += [t.strip() for t in _extra.split(',') if t.strip()]


def get_mr_files(modified_files: str) -> str:
    if modified_files is None:
        return ''
    return modified_files.replace(' ', ';')


def get_mr_components(modified_files: str) -> str:
    return 'esp_h264'


if __name__ == '__main__':
    modified_files = get_mr_files(os.getenv('MODIFIED_FILES'))
    modified_components = get_mr_components(os.getenv('MODIFIED_FILES'))

    preview_targets = PREVIEW_TARGETS
    root = '.'

    args = [
        'build',
        # Find args
        '-p',
        root,
        '-t',
        'all',
        '--build-dir',
        'build_@t_@w',
        '--build-log',
        'build_log.txt',
        '--recursive',
        '--check-warnings',
        # Build args
        '--collect-size-info',
        'size_info.txt',
        '--keep-going',
        '--copy-sdkconfig',
        '--config',
        'sdkconfig.ci.*=',
        '=default',
        '-v',
        '--modified-components',
        f'{modified_components}',
        '--modified-files',
        f'{modified_files}',
    ]
    if idf_build_apps_version == '1.1.4':
        args += ['--size-file', 'size.json']

    args += ['--default-build-targets'] + SUPPORTED_TARGETS + preview_targets

    args += ['--ignore-warning-file'] + DEFAULT_IGNORE_WARNING_FILEPATH

    manifests = [str(p) for p in Path(root).glob('**/.build-test-rules.yml')]
    if manifests:
        args += ['--manifest-file'] + manifests + ['--manifest-rootpath', root]

    ret = subprocess.run(['idf-build-apps', *args])
    sys.exit(ret.returncode)
