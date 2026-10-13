#!/usr/bin/env python3
"""Audit M9 against its frozen public baseline and actual validation evidence."""
import argparse
import csv
import io
import json
from collections import Counter
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--require-clean', action='store_true', help='Also verify all deliverables are committed and the worktree is clean')
args = p.parse_args()
out = root / 'evidence/m9/partial-semantics-20261013'
base = (out / 'BASE_HEAD.txt').read_text().splitlines()[0]
baseline = list(csv.DictReader(io.StringIO(subprocess.check_output(
    ['git', 'show', base + ':compat/cuda-api-ledger.csv'], cwd=root, text=True))))
current = list(csv.DictReader((root / 'compat/cuda-api-ledger.csv').open()))
old = {r['api_name']: r for r in baseline}
new = {r['api_name']: r for r in current}
assert old.keys() == new.keys()
partials = [r for r in baseline if r['target_surface'] == 'PUBLIC_RUNTIME_API'
            and r['current_status'] == 'PARTIAL_IMPLEMENTED']
assert len(partials) == 75
frozen = json.loads((out / 'baseline-inventory.json').read_text())
assert frozen['BASE_HEAD'] == base
assert frozen['PARTIAL_IMPLEMENTED'] == [
    {'api': r['api_name'], 'family': r['family'], 'notes': r['notes']} for r in partials]
reviews = list(csv.DictReader((out / 'partial-review.csv').open()))
assert len(reviews) == 75 and {r['api'] for r in reviews} == {r['api_name'] for r in partials}
classes = {'IMPLEMENTATION_GAP', 'BACKEND_LIMIT', 'REMOTE_ARCH_LIMIT',
           'RELEASE_SCOPE_LIMIT', 'INTENTIONAL_BOUNDED_MODEL'}
gap_apis = {'cudaGetDriverEntryPoint', 'cudaLibraryLoadData', 'cudaLibraryLoadFromFile',
            'cudaLibraryEnumerateKernels', 'cudaSetDeviceFlags'}
assert {r['api'] for r in reviews if r['classification'] == 'IMPLEMENTATION_GAP'} == gap_apis
for r in reviews:
    api = r['api']
    assert r['classification'] in classes and r['remaining_class'] in classes - {'IMPLEMENTATION_GAP'}
    assert r['remaining_reason'] and r['source'] and r['evidence']
    assert r['baseline_status'] == old[api]['current_status']
    assert r['final_status'] == new[api]['current_status']
    assert new[api]['family'] == r['family']
    assert 'M9 reason: ' + r['remaining_class'] + '.' in new[api]['notes']
    for path in r['evidence'].split(';'):
        assert (root / path.strip()).exists(), path
    if api in gap_apis:
        assert not r['completed_semantics'].startswith('No practical')
        assert r['completed_semantics'] in new[api]['notes']
        assert r['remaining_reason'] in new[api]['notes']
for api in old.keys() - {r['api_name'] for r in partials}:
    assert old[api] == new[api], api

public = [r for r in current if r['target_surface'] == 'PUBLIC_RUNTIME_API']
counts = Counter(r['current_status'] for r in public)
assert len(public) == 266
assert counts == {'IMPLEMENTED': 107, 'PARTIAL_IMPLEMENTED': 75, 'GROUND_TRUTH_BLOCKED': 63,
                  'BACKEND_UNSUPPORTED': 13, 'OUT_OF_SCOPE_CURRENT': 8}
assert not counts['GROUND_TRUTH_REQUIRED']

checks = {
    'build.stdout.log': ['G8C_BUILD=PASS', 'G8C_ABI_SURFACE=PASS', 'M1_S3_PROTOCOL_GOLDEN=PASS',
                         'G8C_LIBRARY_LIBCUDART_DEPENDENCY=ABSENT'],
    'static.stdout.log': ['STATIC_QUALIFICATION=PASS', 'M3_S8_SCHEMA_REPRODUCIBLE=PASS',
                          'M2_S3_DUPLICATE_OPCODE=REJECTED', 'M2_S4_BACKEND_SEAM=PASS'],
    'validation.stdout.log': ['PARTIAL_SEMANTICS_VALIDATION=PASS'],
    'final-contracts.stdout.log': ['API_CONTRACTS=PASS'],
    'final-evidence-links.stdout.log': ['EVIDENCE_LINKS=PASS'],
    'baseline-driver-query.stdout.log': ['DRIVER_QUERY name=cuMemAlloc flags=2 result=801 present=0'],
    'baseline-context-client.stdout.log': ['FLAGS_WITH_GRAPH=0 expected=801', 'FLAGS_AFTER=1'],
    'driver-query-native.stdout.log': ['DRIVER_QUERY name=cuMemAlloc flags=2 result=0 present=1'],
    'driver-entries-native.stdout.log': ['PARTIAL_DRIVER_ENTRIES=PASS', 'NON_STREAM_ENTRY name=cuMemAlloc same=1'],
    'partial_driver_entries-remote.stdout.log': ['PARTIAL_DRIVER_ENTRIES=PASS'],
    'driver_entrypoints-remote.stdout.log': ['DRIVER_ENTRIES_REMOTE=PASS'],
    'partial_context_flags-remote.stdout.log': ['FLAGS_WITH_GRAPH=801 expected=801',
        'FLAGS_WITH_EXEC=801 expected=801', 'PARTIAL_CONTEXT_FLAGS=PASS'],
    'context-transfer-protocol.stdout.log': ['CONTEXT_FLAGS_TRANSFER_OWNER_PROTOCOL=PASS'],
    'library-elf-native.stdout.log': ['MODULE_IMAGE_DATA=42', 'LIBRARY_COUNT result=0 count=2'],
    'library-fatbin-native.stdout.log': ['MODULE_IMAGE_LOAD=0', 'MODULE_IMAGE_DATA=42',
                                       'LIBRARY_PRESERVED_LOAD=0', 'LIBRARY_COUNT result=0 count=2'],
    'library-runtime-native.stdout.log': ['RUNTIME_LIBRARY_ENUM capacity=0 result=400',
        'RUNTIME_LIBRARY_ENUM_NULL_ONE=400', 'RUNTIME_LIBRARY_ENUM_NONNULL_ZERO=0'],
    'library-ptx-native.stdout.log': ['MODULE_IMAGE_LOAD=200', 'LIBRARY_PRESERVED_LOAD=200'],
    'library_expansion-remote.stdout.log': ['LIBRARY_EXPANSION_REMOTE=PASS', 'LIBRARY_DATA from_data=2 value=49 PASS'],
    'library_expansion-fatbin-remote.stdout.log': ['LIBRARY_EXPANSION_REMOTE=PASS', 'LIBRARY_DATA from_data=2 value=49 PASS'],
    'remaining_core-remote.stdout.log': ['REMAINING_CORE_REMOTE=PASS'],
    'library-protocol.stdout.log': ['LIBRARY_EXPANSION_PROTOCOL=PASS'],
    'native.stdout.log': ['STREAM_ATTR id=1 get=0 set=1', 'STREAM_ATTR id=8 get=801 set=801',
        'STREAM_ATTR id=9 get=801 set=801', 'STREAM_ATTR id=10 get=801 set=801',
        'LAUNCH_INVALID_ATTRIBUTE=0', 'LAUNCH_INVALID_CLUSTER=0', 'DIRECT_COOPERATIVE=801',
        'LAUNCH_EVENT_ELAPSED=400', 'DIRECT_EVENT_ELAPSED=0'],
    'function-native.stdout.log': ['FUNCTION_ATTRIBUTE id=8 result=0',
        'FUNCTION_ATTRIBUTE id=9 result=801', 'CU_OCCUPANCY_DISABLE_CACHING_OVERRIDE) result=801'],
}
for name in ['explicit_graph', 'capture_pipeline', 'numerical_pipeline', 'symbol_pipeline']:
    checks['workloads/run/' + name + '.stdout.log'] = [name + '=PASS']
for path, markers in checks.items():
    text = (out / path).read_text()
    for marker in markers:
        assert marker in text, (path, marker)
for path in out.glob('*.exit-code.txt'):
    assert path.read_text().strip() == '0', path
assert not [p for p in out.rglob('*') if p.suffix in {'.c', '.h', '.o', '.so', '.cubin', '.fatbin', '.pyc'}]

# Keep settled Graph/Capture, host and pool implementation/state machines intact.
for path in ['docs/GRAPH_CAPTURE_MODEL.md', 'src/corex_graph_runtime.inc',
             'src/corex_capture_runtime.inc', 'server/corex_graph_runtime.inc',
             'schema/corex_api_schema.json', 'include/internal/corex_protocol.h']:
    assert (root / path).read_bytes() == subprocess.check_output(['git', 'show', base + ':' + path], cwd=root)

document = (root / 'docs/PARTIAL_SEMANTICS_COMPLETION.md').read_text()
assert 'All **75** baseline' in document and 'no PARTIAL → IMPLEMENTED upgrades' in document
for api in gap_apis:
    assert api in document
for kind, count in Counter(r['remaining_class'] for r in reviews).items():
    assert f'| {kind} | {count} |' in document
state = (root / 'docs/PROJECT_STATE.md').read_text()
assert base in state and '107 implemented, 75 partial, 63 blocked, 13 backend unsupported' in state
issues = (root / 'docs/REMOTE_CORRECTNESS_ISSUES.md').read_text()
for issue in ['RC-009', 'RC-010', 'RC-011']:
    assert '## ' + issue in issues
if args.require_clean:
    assert not subprocess.check_output(['git', 'status', '--porcelain'], cwd=root)
    subprocess.run(['git', 'merge-base', '--is-ancestor', base, 'HEAD'], cwd=root, check=True)
    for path in ['docs/PARTIAL_SEMANTICS_COMPLETION.md', 'compat/cuda-api-ledger.csv',
                 'evidence/m9/partial-semantics-20261013/COMMIT.txt']:
        subprocess.run(['git', 'ls-files', '--error-unmatch', path], cwd=root, check=True, stdout=subprocess.DEVNULL)

before = Counter(r['family'] for r in partials)
after = Counter(r['family'] for r in public if r['current_status'] == 'PARTIAL_IMPLEMENTED')
summary = {
    'BASE_HEAD': base, 'BASELINE_PARTIAL': 75, 'FINAL_PARTIAL': sum(after.values()),
    'IMPLEMENTATION_GAPS_COMPLETED': sorted(gap_apis),
    'UPGRADED_TO_IMPLEMENTED': [r['api_name'] for r in partials if new[r['api_name']]['current_status'] == 'IMPLEMENTED'],
    'INITIAL_CLASSES': dict(Counter(r['classification'] for r in reviews)),
    'REMAINING_CLASSES': dict(Counter(r['remaining_class'] for r in reviews)),
    'PUBLIC_STATUS_COUNTS': dict(counts),
    'PARTIAL_DELTA_BY_FAMILY': {f: {'baseline': before[f], 'final': after[f], 'delta': after[f] - before[f]}
                                for f in sorted(before.keys() | after.keys())},
    'VALIDATION': 'PASS', 'AUDIT': 'PASS',
}
(out / 'FINAL_AUDIT.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
print('PARTIAL_SEMANTICS_COMPLETION_AUDIT=PASS')
if args.require_clean:
    print('COMMITTED_EVIDENCE_AND_CLEAN_WORKTREE=PASS HEAD=' + subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip())
