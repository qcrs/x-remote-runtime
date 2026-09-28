#!/usr/bin/env python3
"""Fail when documentation points at missing or placeholder evidence paths."""
import re
import sys
from pathlib import Path

EVIDENCE_RE = re.compile(r"(?<![A-Za-z0-9_])evidence/[A-Za-z0-9._/-]+")


def main():
    root = Path(sys.argv[1]) if len(sys.argv) == 2 else Path(__file__).resolve().parents[1]
    errors = []
    for doc in sorted((root / "docs").rglob("*.md")):
        text = doc.read_text()
        for raw in EVIDENCE_RE.findall(text):
            path = raw.rstrip(".,;:`)")
            if "XXXXXX" in path or "PLACEHOLDER" in path:
                errors.append(f"{doc}: placeholder evidence path {path}")
            elif not (root / path).exists():
                errors.append(f"{doc}: missing evidence path {path}")
    for evidence_root in (root / "evidence/m3/s8", root / "evidence/m4"):
        if evidence_root.exists():
            for artifact in evidence_root.rglob("*"):
                if artifact.is_file() and artifact.name in {"generated.h", "generated-test.c"}:
                    errors.append(f"new evidence contains copied generated artifact {artifact}")
    if errors:
        for error in errors:
            print(f"EVIDENCE_LINK_ERROR={error}", file=sys.stderr)
        raise SystemExit(1)
    print("EVIDENCE_LINKS=PASS")


if __name__ == "__main__":
    main()
