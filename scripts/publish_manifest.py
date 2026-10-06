import hashlib
import os
import json
import re
import shutil
from pathlib import Path
source = Path('src/main.cpp').read_text()
version = re.search(r'kFirmwareVersion\[\] = "([^"]+)"', source).group(1)
binary = Path('.pio/build/onx2424g013/firmware.bin').read_bytes()
if not binary or binary[0] != 0xe9:
    raise SystemExit('Invalid ESP32 firmware')
Path('firmware').mkdir(exist_ok=True)
shutil.copyfile('.pio/build/onx2424g013/firmware.bin', 'firmware/firmware.bin')
manifest = dict(version=version, board='onx2424g013', size=len(binary), md5=hashlib.md5(binary).hexdigest(), url='https://raw.githubusercontent.com/keshman74/airKNOB/' + os.environ.get('GITHUB_REF_NAME', 'experimental/multichip-v0.8.0') + '/firmware/firmware.bin')
Path('firmware/latest.json').write_text(json.dumps(manifest, indent=2) + '\n')
