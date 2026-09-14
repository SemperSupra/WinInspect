#!/usr/bin/env python3
"""Run a bounded, public-safe WinInspect concept sweep against local SearXNG.

Only aggregate retrieval coverage is persisted. Raw result URLs, titles and snippets are
used only in-memory to count stable result objects and engines, then discarded.
Search membership remains neutral retrieval evidence and is not semantic authority.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import time
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


def fetch_json(url: str, timeout: int = 20) -> tuple[int, dict[str, Any]]:
    req = urllib.request.Request(url, headers={"User-Agent": "WinInspect-Concept-Sweep/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return response.status, json.loads(response.read().decode("utf-8"))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--base-url", default="http://127.0.0.1:8888")
    ap.add_argument("--queries", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--delay", type=float, default=0.35)
    args = ap.parse_args()

    fixture_path = Path(args.queries)
    fixture_bytes = fixture_path.read_bytes()
    fixture = json.loads(fixture_bytes)

    rows: list[dict[str, Any]] = []
    all_engines: set[str] = set()
    cluster_counts: collections.Counter[str] = collections.Counter()
    language_counts: collections.Counter[str] = collections.Counter()
    succeeded = failed = 0

    for item in fixture["queries"]:
        params = {
            "q": item["query"],
            "format": "json",
            "pageno": 1,
            "language": item["language"],
        }
        url = args.base_url.rstrip("/") + "/search?" + urllib.parse.urlencode(params)
        row: dict[str, Any] = {
            "id": item["id"],
            "cluster": item["cluster"],
            "language": item["language"],
        }
        try:
            status, payload = fetch_json(url)
            raw = payload.get("results", [])
            stable = [
                result for result in raw
                if isinstance(result, dict) and isinstance(result.get("url"), str) and result.get("url")
            ] if isinstance(raw, list) else []
            engines = sorted({
                str(engine)
                for result in stable
                for engine in (
                    result.get("engines") if isinstance(result.get("engines"), list)
                    else [result.get("engine")]
                )
                if engine
            })
            all_engines.update(engines)
            row.update({
                "status": "succeeded" if status == 200 else "failed",
                "http_status": status,
                "result_count": len(stable),
                "engines": engines,
            })
            if status == 200:
                succeeded += 1
                cluster_counts[item["cluster"]] += len(stable)
                language_counts[item["language"]] += len(stable)
            else:
                failed += 1
        except Exception as exc:
            failed += 1
            row.update({
                "status": "failed",
                "error_class": type(exc).__name__,
                "result_count": 0,
                "engines": [],
            })
        rows.append(row)
        time.sleep(args.delay)

    output = {
        "schema": "wininspect.concept-sweep.searxng-result.v1",
        "authority_effect": "none",
        "evidence_semantics": {
            "role": "retrieval_coverage_only",
            "search_membership_is_semantic_truth": False,
            "negative_result_is_concept_absence": False,
        },
        "fixture": {
            "schema": fixture.get("schema"),
            "sha256": hashlib.sha256(fixture_bytes).hexdigest(),
            "query_count": len(fixture["queries"]),
            "query_class": "fixed_public_research_fixture",
        },
        "request_counts": {
            "planned": len(fixture["queries"]),
            "succeeded": succeeded,
            "failed": failed,
        },
        "coverage": {
            "engines": sorted(all_engines),
            "by_cluster_result_count": dict(sorted(cluster_counts.items())),
            "by_language_result_count": dict(sorted(language_counts.items())),
        },
        "requests": rows,
        "privacy": {
            "authentication_used": False,
            "user_query_history_persisted": False,
            "raw_result_urls_persisted": False,
            "raw_result_titles_persisted": False,
            "raw_result_snippets_persisted": False,
            "fixed_public_research_queries_are_versioned_in_fixture": True,
        },
    }

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "status": "PASS" if failed == 0 else ("PARTIAL" if succeeded else "BLOCKED"),
        "planned": len(fixture["queries"]),
        "succeeded": succeeded,
        "failed": failed,
        "engines": sorted(all_engines),
        "clusters": dict(sorted(cluster_counts.items())),
        "languages": dict(sorted(language_counts.items())),
    }, ensure_ascii=False, indent=2))
    return 0 if succeeded > 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
