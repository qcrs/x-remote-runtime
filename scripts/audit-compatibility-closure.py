#!/usr/bin/env python3
"""Compare the frozen baseline, per-API reviews and current closure evidence."""
import csv
import io
import json
from collections import Counter
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'evidence/m8/compatibility-closure-20261013'
base = (out / 'BASE_HEAD.txt').read_text().strip()
baseline = list(csv.DictReader(io.StringIO(subprocess.check_output(
    ['git', 'show', base + ':compat/cuda-api-ledger.csv'], cwd=root, text=True))))
current = list(csv.DictReader((root / 'compat/cuda-api-ledger.csv').open()))
old = {r['api_name']: r for r in baseline}
new = {r['api_name']: r for r in current}
assert old.keys() == new.keys()
frozen = json.loads((out / 'baseline-inventory.json').read_text())
for status, expected_count in [('PARTIAL_IMPLEMENTED', 77), ('GROUND_TRUTH_REQUIRED', 35),
                               ('GROUND_TRUTH_BLOCKED', 35), ('BACKEND_UNSUPPORTED', 7)]:
    expected = [{'api': r['api_name'], 'family': r['family'], 'notes': r['notes']} for r in baseline
                if r['target_surface'] == 'PUBLIC_RUNTIME_API' and r['current_status'] == status]
    assert frozen[status] == expected and len(expected) == expected_count

partials = list(csv.DictReader((out / 'partial-review.csv').open()))
terminals = list(csv.DictReader((out / 'terminal-review.csv').open()))
assert {r['api'] for r in partials} == {r['api'] for r in frozen['PARTIAL_IMPLEMENTED']}
assert len(partials) == 77 and len(terminals) == 77
assert {r['api'] for r in terminals} == {r['api'] for status in
    ['GROUND_TRUTH_REQUIRED', 'GROUND_TRUTH_BLOCKED', 'BACKEND_UNSUPPORTED'] for r in frozen[status]}
for review in partials + terminals:
    assert review['final_status'] == new[review['api']]['current_status']
    assert review.get('reason') and (review.get('source') or review.get('evidence'))
    if review in partials:
        assert review['remaining_semantics'] == new[review['api']]['notes']

public = [r for r in current if r['target_surface'] == 'PUBLIC_RUNTIME_API']
terminal = {'IMPLEMENTED', 'PARTIAL_IMPLEMENTED', 'GROUND_TRUTH_BLOCKED',
            'BACKEND_UNSUPPORTED', 'OUT_OF_SCOPE_CURRENT'}
assert len(public) == 266 and len(current) == 276
assert all(r['current_status'] in terminal for r in public)
for r in public:
    if r['current_status'] in {'PARTIAL_IMPLEMENTED', 'GROUND_TRUTH_BLOCKED', 'BACKEND_UNSUPPORTED'}:
        assert r['notes'] and 'Census-derived declaration only' not in r['notes']
    if old[r['api_name']]['current_status'] == 'GROUND_TRUTH_REQUIRED':
        assert 'Evidence:' in r['notes']
    if old[r['api_name']]['current_status'] == 'IMPLEMENTED':
        assert r['current_status'] == 'IMPLEMENTED'

checks = {
    'build.stdout.log': ['G8C_BUILD=PASS', 'G8C_ABI_SURFACE=PASS', 'M1_S3_PROTOCOL_GOLDEN=PASS'],
    'static.stdout.log': ['STATIC_QUALIFICATION=PASS', 'M3_S8_SCHEMA_REPRODUCIBLE=PASS',
                          'M2_S3_DUPLICATE_OPCODE=REJECTED', 'M2_S4_BACKEND_SEAM=PASS'],
    'peer-ipc.stdout.log': ['PHYSICAL_DEVICE_COUNT=1', 'IPC_MEMORY_DATA=PASS',
                           'cuIpcOpenEventHandle(&event, e.ev) result=0'],
    'legacy.stdout.log': ['LEGACY_REFERENCE_PROBE=COMPLETE', 'result=801', 'result=31'],
    'legacy-registration-symbols.stdout.log': ['__cudaRegisterSurface', '__cudaRegisterTexture'],
    'legacy-fetch-compile.stderr.log': ["no matching function for call to 'tex1Dfetch'"],
    'closure-remote.stdout.log': ['UNALIGNED_SHARED_PAGE_REGISTRATION_AND_RETIREMENT=PASS',
                                 'ERROR_ENUM_NAMES_AND_NATIVE_DESCRIPTIONS=PASS count=112',
                                 'DEVICE_SELECTION_SELF_PEER=PASS', 'COMPATIBILITY_CLOSURE_REMOTE=PASS'],
    'function-remote.stdout.log': ['FUNCTION_INFO_REMOTE=PASS', 'maxDynamicSharedSizeBytes=131072'],
    'function-protocol.stdout.log': ['FUNCTION_INFO_OLD_WIRE_EXTENDED_WIRE_AND_STALE_OWNER=PASS'],
    'host-stream-protocol.stdout.log': ['COMPAT_CONTROL_GOLDEN_WIRE_AND_NEGATIVES=PASS'],
    'compat_expansion-remote.stdout.log': ['REMOTE_COMPAT_EXPANSION=PASS'],
    'legacy_control-remote.stdout.log': ['LEGACY_CONTROL_REMOTE=PASS'],
    'workloads/host_interaction.stdout.log': ['host_interaction=PASS'],
    'workloads/numerical_pipeline.stdout.log': ['numerical_pipeline=PASS'],
}
for path, markers in checks.items():
    text = (out / path).read_text()
    for marker in markers:
        assert marker in text, (path, marker)
for p in out.glob('*.exit-code.txt'):
    # Only the deliberately unsupported legacy texture fetch compile may fail.
    assert p.read_text().strip() == ('1' if p.name == 'legacy-fetch-compile.exit-code.txt' else '0'), p
assert len(list(csv.DictReader((out / 'error-enum-native.tsv').open(), delimiter='\t'))) == 112
assert not [p for p in out.rglob('*') if p.suffix in {'.c', '.h', '.o', '.so', '.cubin', '.pyc'}]

# Unsupported status requires the measured primitive on a real owner, or a
# measured failing allocation prerequisite; mere declaration/symbol presence
# cannot prove this status.
for marker in ['cuArrayCreate(&a, &d) result=801', 'cuArray3DCreate(&a, &d3) result=801',
               'cuMipmappedArrayCreate(&mip, &d3, 3) result=801',
               'cuMemAllocManaged(&managed, 4096, CU_MEM_ATTACH_GLOBAL) result=801',
               'cuTexObjectCreate(&t, &res, &tex, NULL) result=801']:
    assert marker in (out / 'array-managed-driver.stdout.log').read_text()
for marker in ['cudaGetTextureReference(&texture_ref, &closure_texture) result=31',
               'cudaGetSurfaceReference(&surface_ref, &closure_surface) result=801',
               'cudaBindTexture(&offset, &closure_texture, p, &desc, 4096) result=801',
               'cudaBindTexture2D(&offset, &closure_texture_2d, p, &desc, 32, 2, 128) result=801',
               'cudaGetTextureAlignmentOffset(&offset, &closure_texture) result=31',
               'cudaUnbindTexture(&closure_texture) result=31']:
    assert marker in (out / 'legacy.stdout.log').read_text()
assert 'cuKernelSetAttribute(CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES,0,kernel,device)=801' in (
    root / 'evidence/m4/library-expansion-20261012/native-unload-diagnostic.stdout.log').read_text()
assert 'cuLaunchCooperativeKernel(func,1,1,1,1,1,1,0,NULL,args)=801' in (
    root / 'evidence/m4/continued-expansion-20261012/remaining-execution-probe.stdout.log').read_text()
for path in ['docs/API_COMPATIBILITY_CLOSURE.md', 'docs/PROJECT_STATE.md', 'docs/RELEASE_READINESS.md']:
    assert (root / path).exists()

upgrades = [r['api_name'] for r in public if old[r['api_name']]['current_status'] == 'PARTIAL_IMPLEMENTED'
            and r['current_status'] == 'IMPLEMENTED']
required = {r['api']: new[r['api']]['current_status'] for r in frozen['GROUND_TRUTH_REQUIRED']}
supported = [n for n in required if required[n] in {'IMPLEMENTED', 'PARTIAL_IMPLEMENTED'}]
report = {'base_head': base, 'baseline_partial_reviewed': len(partials),
          'partial_to_implemented': upgrades, 'required_to_terminal': required,
          'new_supported': supported, 'public_counts': dict(Counter(r['current_status'] for r in public)),
          'remaining_partial_by_family': dict(Counter(r['family'] for r in public
                                                     if r['current_status'] == 'PARTIAL_IMPLEMENTED')),
          'remaining_blocked_by_family': dict(Counter(r['family'] for r in public
                                                     if r['current_status'] == 'GROUND_TRUTH_BLOCKED')),
          'remaining_unsupported_by_family': dict(Counter(r['family'] for r in public
                                                         if r['current_status'] == 'BACKEND_UNSUPPORTED'))}
(out / 'FINAL_LEDGER_COMPARISON.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
print('COMPATIBILITY_CLOSURE_AUDIT=PASS')
