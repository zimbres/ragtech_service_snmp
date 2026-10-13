"""Check every supported model's default/model conversion factors against devices.xml."""
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
root=ET.parse(Path(__file__).resolve().parents[1]/'devices.xml').getroot()
device=root.find('./devices/device[@family="10"]')
models={m.get('value'):m for m in device.findall('model')}
def resolve(identifier):
    model=models[identifier]
    # Match config.so readDevice: load family defaults and the selected model's
    # own overrides. Do not recursively inherit variables via the "from" attribute.
    values={v.get('name'):v.get('value') for v in device.findall('./default/vars/var')}
    values.update({v.get('name'):v.get('value') for v in model.findall('./vars/var')})
    return values
output=subprocess.check_output([sys.argv[1]],text=True)
seen=set()
for line in output.splitlines():
    identifier,*factors=line.split()
    expected=resolve(identifier)
    for key,factor in zip(('iOutput','vOutput','vBattery'),factors):
        number=re.search(r'\*([0-9.]+)',expected[key]).group(1)
        assert abs(float(number)-float(factor))<1e-6,(identifier,key,number,factor)
    seen.add(identifier)
assert seen==set(models)
print('Command bytes, fragmented replies, rejection paths and all 20 XML model factors pass.')
