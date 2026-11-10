#!/usr/bin/env python3
"""Export a private, read-only line index for the Rictus evidence service.

[AI:GPT-6 | 2026-11-04 05:40:09 UTC]
"""
import argparse
import json
import re
import sys

IDENTIFIER = re.compile(r"^\d{8}\.\d+$")
# [AI:GPT-6 | 2026-11-04 05:50:24 UTC]
FILE_ROOT = re.compile(r"^(\d{8}\.\d+)(?:\.R\d+)?[_ .-]")
# [End AI:GPT-6]


def export_index(catalog):
    """Keep all revisions, including R0; do not select governing documents."""
    if not isinstance(catalog, dict) or not isinstance(catalog.get("documents"), list):
        raise ValueError("Full-text policy catalog required")
    rows = []
    for doc in catalog["documents"]:
        if not isinstance(doc, dict) or not isinstance(doc.get("text"), str):
            raise ValueError("Full-text policy catalog required")
        root = doc.get("root_document_id")
        # [AI:GPT-6 | 2026-11-04 05:50:24 UTC]
        if not isinstance(root, str) or not IDENTIFIER.fullmatch(root):
            filename = doc.get("file", "")
            match = FILE_ROOT.match(filename.rsplit("/", 1)[-1]) if isinstance(filename, str) else None
            if not match:
                raise ValueError("Invalid document root ID and no identifiable filename root")
            root = match.group(1)
        # [End AI:GPT-6]
        revision = doc.get("revision_id", "NONE")
        # [AI:GPT-6 | 2026-11-04 05:51:17 UTC]
        source = root if revision in ("", None, "NONE", "R0", root + ".R0") else revision
        # [End AI:GPT-6]
        digest = doc.get("sha256_file_bytes", "")
        if not isinstance(source, str) or len(source) >= 64 or not isinstance(digest, str) or len(digest) != 64:
            raise ValueError("Invalid source identity or hash")
        for number, line in enumerate(doc["text"].splitlines(), start=1):
            clean = line.replace("\t", " ").replace("\r", " ")
            if len((source + "\t" + str(number) + "\t" + digest + "\t" + clean + "\n").encode("utf-8")) >= 4096:
                raise ValueError(f"Index line too long: {root}:{number}")
            rows.append(f"{source}\t{number}\t{digest}\t{clean}\n")
    return "".join(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", help="Full-text policy catalog JSON")
    args = parser.parse_args()
    try:
        with open(args.catalog, encoding="utf-8") as stream:
            index = export_index(json.load(stream))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    sys.stdout.write(index)


if __name__ == "__main__":
    main()
# [End AI:GPT-6]
