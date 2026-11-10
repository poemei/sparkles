#!/usr/bin/env python3
"""Produce a read-only policy evidence packet for a proposed control request.

[AI:GPT-6 | 2026-11-04 05:32:00 UTC]
[AI:GPT-6 | 2026-11-04 05:35:20 UTC] Use whole-term evidence search for control inputs.
"""
import argparse
import json
import sys

from policy_lookup import lookup


def control_evidence(catalog, operation, target, limit=25):
    """Search every document; report citations, not authority or applicability."""
    if not isinstance(operation, str) or not operation.strip() or any(c.isspace() for c in operation):
        raise ValueError("Operation must be a single nonempty token")
    if not isinstance(target, str) or not target.strip() or any(c.isspace() for c in target):
        raise ValueError("Target must be a single nonempty token")
    if not isinstance(catalog, dict) or not isinstance(catalog.get("documents"), list):
        raise ValueError("A full-text policy catalog is required")
    docs = catalog["documents"]
    if not docs:
        raise ValueError("Policy catalog is empty")
    if not isinstance(limit, int) or not 1 <= limit <= 200:
        raise ValueError("Limit must be between 1 and 200")
    terms = list(dict.fromkeys((operation.casefold(), target.casefold())))
    queries = {term: lookup(catalog, term, limit=limit, whole_term=True) for term in terms}
    return {
        "operation": operation, "target": target,
        "corpus_documents_inspected": len(docs),
        "corpus_root_families_reported": catalog.get("root_family_count"),
        "searches": queries,
        "governing_facts": {
            "mission_scope": "UNKNOWN",
            "policy_and_directive": "UNKNOWN",
            "instruction": "UNKNOWN",
            "delegated_authority": "UNKNOWN",
        },
        "decision": "NOT_EVALUATED",
        "execution": "NOT_REQUESTED",
        "note": "Candidate evidence only. Literal matches do not establish applicability, approval, or permission. The installed Compliance module remains the decision boundary.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", help="JSON produced by policy_inventory.py --include-text")
    parser.add_argument("operation", help="Proposed operation, e.g. restart")
    parser.add_argument("target", help="Proposed target, e.g. digit")
    parser.add_argument("--limit", type=int, default=25, help="Maximum matches per query (1-200)")
    args = parser.parse_args()
    try:
        with open(args.catalog, encoding="utf-8") as stream:
            catalog = json.load(stream)
        packet = control_evidence(catalog, args.operation, args.target, args.limit)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.error(str(exc))
    json.dump(packet, sys.stdout, indent=2, ensure_ascii=False)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
# [End AI:GPT-6]
