Import('env')
import os
import re

version = os.getenv('FW_VERSION', '0.1.0').lstrip('v')
if not re.fullmatch(r'\d+\.\d+\.\d+', version):
    version = '0.1.0'

build_id = os.getenv('FW_BUILD_ID', f'led-blink-central-test::{version}')

env.Append(CPPDEFINES=[
    ('UEC_FIRMWARE_VERSION', env.StringifyMacro(version)),
    ('UEC_BUILD_ID', env.StringifyMacro(build_id)),
])
