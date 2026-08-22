Import('env')
import os
import re

version = os.getenv('FW_VERSION', '0.5.3').lstrip('v')
if not re.fullmatch(r'\d+\.\d+\.\d+', version):
    version = '0.5.3'

build_id = os.getenv('FW_BUILD_ID', f'led-blink-central-test::{version}')
major, minor, patch = [int(part) for part in version.split('.')]
blink_interval = 250 if (major, minor, patch) >= (0, 2, 0) else 1000

env.Append(CPPDEFINES=[
    ('UEC_FIRMWARE_VERSION', env.StringifyMacro(version)),
    ('UEC_BUILD_ID', env.StringifyMacro(build_id)),
    ('UEC_BLINK_INTERVAL_MS', str(blink_interval)),
])
