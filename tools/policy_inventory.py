#!/usr/bin/env python3
"""Inspect STN-LABZ controlled-document archives without assigning authority.

[AI:GPT-6 | 2026-11-04 05:17:10 UTC]
[AI:GPT-6 | 2026-11-04 05:21:55 UTC] Add read-only full-text catalog export.
"""
import argparse
import hashlib
import json
import re
import zipfile
from pathlib import PurePosixPath

FIELD = re.compile(r"^\s*(Root Document ID|Revision ID|Previous Revision|Status|Scope|sha256)\s*:\s*(.*?)\s*$", re.I)
DOC_ID = re.compile(r"^\d{8}\.\d+(?:\.R\d+)?$")
FILENAME_ID = re.compile(r"^(\d{8}\.\d+)(?:\.(R\d+))?[_ .-]")
HEADING_ID = re.compile(r"^\s*(\d{8}\.\d+(?:\.R\d+)?)\b")


def inspect(name, content):
    """Represent evidence and discrepancies; never infer governing authority."""
    text = content.decode("utf-8-sig", errors="replace")
    fields = {}
    for line in text.splitlines()[:60]:
        match = FIELD.match(line)
        if match and match.group(1).lower() not in fields:
            fields[match.group(1).lower()] = match.group(2).strip()
    file_id = FILENAME_ID.match(PurePosixPath(name).name)
    heading = next((HEADING_ID.match(line) for line in text.splitlines()[:10]
                    if HEADING_ID.match(line)), None)
    root = fields.get("root document id", "")
    revision = fields.get("revision id", "")
    filename_root = file_id.group(1) if file_id else ""
    filename_revision = file_id.group(2) if file_id else None
    issues = []
    if not DOC_ID.fullmatch(root) or ".R" in root:
        issues.append("invalid_or_missing_root_id")
    if filename_root and filename_root != root:
        issues.append("filename_root_mismatch")
    if filename_revision and revision != f"{root}.{filename_revision}":
        issues.append("filename_revision_mismatch")
    if not filename_revision and revision not in ("NONE", "R0", f"{root}.R0"):
        issues.append("unexpected_revision_for_root_filename")
    if heading and heading.group(1).split(".R")[0] != root:
        issues.append("heading_root_mismatch")
    if heading and filename_revision and heading.group(1) != f"{root}.{filename_revision}":
        issues.append("heading_revision_mismatch")
    return {
        "file": name, "root_document_id": root, "revision_id": revision,
        "previous_revision": fields.get("previous revision", ""),
        "status_declared": fields.get("status", ""),
        "scope_declared": fields.get("scope", ""),
        "sha256_declared": fields.get("sha256", ""),
        "sha256_file_bytes": hashlib.sha256(content).hexdigest(),
        "issues": issues,
    }


def inventory(archive, include_text=False):
    documents = []
    with zipfile.ZipFile(archive) as bundle:
        for name in sorted(bundle.namelist()):
            if name.endswith("/"):
                continue
            if PurePosixPath(name).suffix.lower() != ".txt":
                raise ValueError(f"Non-text archive entry: {name}")
            if name.startswith("/") or ".." in PurePosixPath(name).parts:
                raise ValueError(f"Unsafe archive path: {name}")
            content = bundle.read(name)
            entry = inspect(name, content)
            if include_text:
                entry["text"] = content.decode("utf-8-sig", errors="replace")
            documents.append(entry)
    return {"archive": PurePosixPath(archive).name,
            "documents": documents,
            "document_count": len(documents),
            "root_family_count": len(set(d["root_document_id"] for d in documents)),
            "issue_count": sum(len(d["issues"]) for d in documents),
            "note": "Inventory only; no revision selection, policy approval, permission, or operational authority inferred."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", help="Local zip containing controlled TXT documents")
    parser.add_argument("--include-text", action="store_true",
                        help="Include full original document text in JSON for read-only catalog use")
    args = parser.parse_args()
    print(json.dumps(inventory(args.archive, include_text=args.include_text),
                     indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
# [End AI:GPT-6]
