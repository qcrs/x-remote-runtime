#!/usr/bin/env python3
from pathlib import Path

p = Path("verify_g8c_scope.py")
text = p.read_text()

if "import re\n" not in text:
    text = text.replace("from pathlib import Path\n", "from pathlib import Path\nimport re\n", 1)

old_rule = '''    ("client_no_raw_dto_memcpy_to_prop",
     "memcpy(prop" not in api),
'''
new_rule = '''    ("client_no_raw_dto_memcpy_to_prop",
     re.search(r"\\bmemcpy\\s*\\(\\s*prop\\s*,", api) is None),
'''
if old_rule not in text:
    raise SystemExit("old scope rule not found; verifier may already be patched")
text = text.replace(old_rule, new_rule, 1)

anchor = '''    ("client_vendor_abi_adapter",
     "#include <cuda_runtime_api.h>" in api and
     "sizeof(*prop)" in api),
'''
positive_rule = '''    ("client_fieldwise_prop_population",
     "memset(prop, 0, sizeof(*prop));" in api and
     "memcpy(prop->name, info.name, name_bytes);" in api and
     "prop->totalGlobalMem = (size_t)info.total_global_mem;" in api and
     "prop->warpSize = (int)info.warp_size;" in api and
     "prop->multiProcessorCount = (int)info.multi_processor_count;" in api),
'''
if anchor not in text:
    raise SystemExit("vendor adapter audit marker not found")
text = text.replace(anchor, anchor + positive_rule, 1)

p.write_text(text)
print("G8C_B_V4_SCOPE_PATCH=PASS")
