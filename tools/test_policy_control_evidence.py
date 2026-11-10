#!/usr/bin/env python3
"""Test the control evidence handoff without granting policy permissions.

[AI:GPT-6 | 2026-11-04 05:33:00 UTC]
[AI:GPT-6 | 2026-11-04 05:35:20 UTC] Regress whole-term control evidence selection.
"""
import unittest

from policy_control_evidence import control_evidence


CATALOG = {
    "root_family_count": 2,
    "documents": [
        {"file": "authority.txt", "root_document_id": "20260729.0",
         "revision_id": "NONE", "status_declared": "Approved",
         "sha256_file_bytes": "a" * 64, "issues": [],
         "text": "Operational authority applies to the assigned mission.\n"},
        {"file": "operations.txt", "root_document_id": "20260729.3",
         "revision_id": "NONE", "status_declared": "Approved",
         "sha256_file_bytes": "b" * 64, "issues": [],
         "text": "Digit may need an authorized restart.\n"},
    ],
}


class ControlEvidenceTests(unittest.TestCase):
    def test_packet_covers_entire_catalog(self):
        result = control_evidence(CATALOG, "restart", "digit")
        self.assertEqual(result["corpus_documents_inspected"], 2)
        self.assertEqual(result["corpus_root_families_reported"], 2)
        self.assertEqual(result["searches"]["restart"]["total_matches"], 1)
        self.assertEqual(result["searches"]["digit"]["total_matches"], 1)

    def test_source_reference_is_preserved(self):
        result = control_evidence(CATALOG, "restart", "digit")
        match = result["searches"]["restart"]["matches"][0]
        self.assertEqual(match["root_document_id"], "20260729.3")
        self.assertEqual(match["line"], 1)
        self.assertEqual(match["sha256_file_bytes"], "b" * 64)

    def test_no_permission_inferred(self):
        result = control_evidence(CATALOG, "restart", "digit")
        self.assertEqual(result["decision"], "NOT_EVALUATED")
        self.assertEqual(result["execution"], "NOT_REQUESTED")
        self.assertEqual(set(result["governing_facts"].values()), {"UNKNOWN"})

    def test_no_match_does_not_grant_permission(self):
        result = control_evidence(CATALOG, "deploy", "unknown")
        self.assertEqual(result["searches"]["deploy"]["total_matches"], 0)
        self.assertEqual(result["decision"], "NOT_EVALUATED")

    def test_whole_term_excludes_digital_but_preserves_digit(self):
        catalog = {"documents": [
            {"file": "noise.txt", "text": "digital documentation and restartable services"},
            {"file": "target.txt", "text": "Restart Digit. DIGIT-digit is mentioned."},
        ]}
        result = control_evidence(catalog, "restart", "digit")
        self.assertEqual(result["searches"]["restart"]["total_matches"], 1)
        self.assertEqual(result["searches"]["digit"]["total_matches"], 1)
        self.assertEqual(result["searches"]["digit"]["match_mode"], "whole_term")
        self.assertEqual(result["decision"], "NOT_EVALUATED")

    def test_invalid_inputs_fail(self):
        with self.assertRaises(ValueError):
            control_evidence(CATALOG, "", "digit")
        with self.assertRaises(ValueError):
            control_evidence(CATALOG, "restart digit", "digit")
        with self.assertRaises(ValueError):
            control_evidence({"documents": []}, "restart", "digit")
        with self.assertRaises(ValueError):
            control_evidence(CATALOG, "restart", "digit", 0)


if __name__ == "__main__":
    unittest.main()
# [End AI:GPT-6]
