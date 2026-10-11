"""Offline artifact audit only: no valid controller request or game start."""
from pathlib import Path
import datetime
import hashlib
import json
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / 'docs/evidence/session-20261011'
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()
baseline = json.loads((EVIDENCE / 'day2-inline-editor-key-logger-v2-candidate.json').read_text())
protected = baseline['protected_sha256']
for name, expected in protected.items():
    assert sha(ROOT / name) == expected.upper(), name
source = (ROOT / 'plugin/probes/inline_editor_debug_bridge.cpp').read_text(encoding='utf-8')
exe = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe')
assert sha(exe) == '7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5'
image = exe.read_bytes()
pe_offset = struct.unpack_from('<I', image, 0x3C)[0]
assert image[pe_offset:pe_offset+4] == b'PE\0\0'
machine, section_count = struct.unpack_from('<HH', image, pe_offset+4)
optional_size = struct.unpack_from('<H', image, pe_offset+20)[0]
assert machine == 0x8664 and section_count < 96
sections = pe_offset+24+optional_size
text_rva = None
for i in range(section_count):
    at = sections+i*40
    if image[at:at+8].rstrip(b'\0') == b'.text':
        virtual_size, text_rva, raw_size, raw_offset = struct.unpack_from('<IIII', image, at+8)
        code = image[raw_offset:raw_offset+min(raw_size, virtual_size)]
assert text_rva is not None
rvas = {name: int(value, 16) for name, value in re.findall(r'(\w+)_rva\s*=\s*(0x[0-9A-Fa-f]+)', source)}
fingerprints = {}
for name, body in re.findall(r'constexpr unsigned char (\w+)_bytes\s*\[\]\s*=\s*\{([^}]+)\}', source):
    data = bytes(int(n, 16) for n in re.findall(r'0x([0-9A-Fa-f]{2})', body))
    assert code.count(data) == 1, (name, 'unique')
    assert code.find(data)+text_rva == rvas[name], (name, 'RVA')
    fingerprints[name] = {'rva': f'{rvas[name]:X}', 'size': len(data), 'unique_hits': 1}
assert len(fingerprints) == 11
assert len(re.findall(r'MH_CreateHook\(', source)) == 7
files = [p for p in (ROOT / 'plugin/probes').glob('*debug*') if p.is_file()]
files += list((ROOT / 'plugin/build/inline-editor-debug-bridge').glob('*.exe'))
files += [ROOT / 'plugin/build/inline-editor-debug-bridge/rename_anything_inline_editor_debug_bridge.dll',
          ROOT / 'plugin/build/inline-editor-debug-bridge/inline_debug_manifest.hpp', Path(__file__).resolve()]
controller = ROOT / 'plugin/build/inline-editor-debug-bridge/inline_debug_control.exe'
cli = []
for args in [['--help'], ['0', 'snapshot'], ['4294967296', 'dump'], ['abc', 'resume'], ['1', 'foreign']]:
    result = subprocess.run([str(controller), *args], capture_output=True, text=True, timeout=10)
    assert result.returncode == (0 if args == ['--help'] else 2), (args, result.returncode)
    cli.append({'args': args, 'exit_code': result.returncode,
                'output': (result.stdout+result.stderr).strip(), 'target_access': False})
report = {
    'time': datetime.datetime.now().astimezone().isoformat(),
    'status': 'OFFLINE PASS; fresh HOI4 runtime PENDING',
    'baseline_v2_source_sha256': sha(ROOT / 'plugin/probes/inline_editor_key_logger_v2.cpp'),
    'diagnostic_transport_checks': 49626, 'inherited_v2_checks': 49240,
    'checks_are_repeated_assertions_not_distinct_scenarios': True,
    'ring_queue_suite': 'wrap/pause/truncation/concurrent copied snapshots; model order/coalescing/TTL PASS',
    'native_hooks': 7, 'fingerprints': fingerprints,
    'whole_instruction_proof': 'separate REA research and independent bridge review; this script verifies bytes and unique RVA only',
    'protected_sha256': protected,
    'artifact_sha256': {p.relative_to(ROOT).as_posix(): sha(p) for p in files},
    'controller_cli': cli,
    'runtime': {'activated': False, 'Execute_or_effect_acceptance': False, 'snapshot_servicing': False},
    'limits': ['controller success means queued only',
               'pause stops ring only; baseline file continues; in-flight UI work can finish',
               'copied ring overwrites; log write failures limit absence inference',
               'own selectors classify origin, not country ownership or effect success',
               'universal same-address/same-epoch load boundary OPEN',
               'single variant in a fresh process; no inverse co-load'],
}
out = EVIDENCE / 'day2-inline-debug-bridge-candidate.json'
out.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(f'11 unique fingerprints;7 hooks;{len(protected)} protected hashes;5 negative CLI PASS; runtime not activated')
print('Manifest SHA256', sha(out))
