#!/usr/bin/env python3
"""Regression tests for the read-only policy catalog and evidence lookup.

[AI:GPT-6 | 2026-11-04 05:27:00 UTC]
"""
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

from policy_inventory import inventory
from policy_lookup import lookup


class PolicyEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.archive = Path(self.tmp.name) / "policies.zip"
        with zipfile.ZipFile(self.archive, "w") as bundle:
            bundle.writestr("20260729.0_OPERATIONAL_AUTHORITY.txt",
                            "20260729.0.R0 - Operational Authority\n"
                            "Root Document ID: 20260729.0\n"
                            "Revision ID: NONE\nStatus: Approved\n"
                            "Operational authority requires assigned mission.\n")
            bundle.writestr("20260729.0.R1_OPERATIONAL_AUTHORITY.txt",
                            "20260729.0.R1 - Operational Authority\n"
                            "Root Document ID: 20260729.0\n"
                            "Revision ID: 20260729.0.R1\nStatus: Approved\n"
                            "Operational authority requires qualification.\n")

    def test_inventory_preserves_root_and_revision(self):
        result = inventory(self.archive)
        self.assertEqual(result["document_count"], 2)
        self.assertEqual(result["root_family_count"], 1)
        revisions = {doc["revision_id"] for doc in result["documents"]}
        self.assertEqual(revisions, {"NONE", "20260729.0.R1"})

    def test_full_text_is_opt_in(self):
        compact = inventory(self.archive)
        self.assertNotIn("text", compact["documents"][0])
        full = inventory(self.archive, include_text=True)
        self.assertIn("Operational authority", full["documents"][0]["text"])
        self.assertEqual(json.loads(json.dumps(full))["document_count"], 2)

    def test_lookup_preserves_source_evidence(self):
        found = lookup(inventory(self.archive, include_text=True),
                       "operational authority", limit=2)
        self.assertGreater(found["total_matches"], 2)
        self.assertEqual(len(found["matches"]), 2)
        self.assertTrue(found["truncated"])
        self.assertTrue(all(row["line"] > 0 for row in found["matches"]))
        self.assertTrue(all(len(row["sha256_file_bytes"]) == 64
                            for row in found["matches"]))

    def test_lookup_does_not_assign_authority(self):
        found = lookup(inventory(self.archive, include_text=True),
                       "qualification")
        self.assertEqual(found["total_matches"], 1)
        self.assertNotIn("decision", found)
        self.assertNotIn("permission", found)

    def test_reject_invalid_query_and_nontext_catalog(self):
        with self.assertRaises(ValueError):
            lookup(inventory(self.archive), "authority")
        with self.assertRaises(ValueError):
            lookup(inventory(self.archive, include_text=True), " ")
        with self.assertRaises(ValueError):
            lookup(inventory(self.archive, include_text=True), "x", limit=0)

    def test_reject_non_text_archive_member(self):
        archive = Path(self.tmp.name) / "invalid.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("unexpected.bin", b"something")
        with self.assertRaises(ValueError):
            inventory(archive)


if __name__ == "__main__":
    unittest.main()
# [End AI:GPT-6]
