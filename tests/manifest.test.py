from pathlib import Path
import tempfile,subprocess,json,os,hashlib
source=Path('src/main.cpp').read_text()
script=Path('scripts/publish_manifest.py').resolve()
with tempfile.TemporaryDirectory() as tmp:
 root=Path(tmp);(root/'src').mkdir();(root/'src/main.cpp').write_text(source)
 out=root/'.pio/build/onx2424g013';out.mkdir(parents=True)
 payload=bytes([0xe9])+bytes(range(255));(out/'firmware.bin').write_bytes(payload)
 env=dict(os.environ,GITHUB_REF_NAME='experimental/multichip-v0.8.0')
 subprocess.run(['python3',str(script)],cwd=root,env=env,check=True)
 m=json.loads((root/'firmware/latest.json').read_text()); name='airControl-KNOB-v'+m['version']+'.bin'
 assert m['url'].endswith('/firmware/'+name)
 assert (root/'firmware'/name).read_bytes()==payload
 assert (root/'firmware/firmware.bin').read_bytes()==payload
 assert m['md5']==hashlib.md5(payload).hexdigest() and m['size']==len(payload)
print('Versioned firmware filename, URL, payload and manifest checksum passed')
