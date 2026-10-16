#!/usr/bin/env python3
"""Claude Code PostToolUse hook: keep the RAG index in step with the repo.

Fires after a Bash call. If the command was a `git commit`/`git push` inside the project, it
1. copies the files the RAG server cannot read natively (patches, .json, .inc) to a staging
   dir as .txt (see scripts/rag-manifest.json, "stage"), and
2. returns additionalContext telling the assistant to re-run rag_ingest for every manifest entry
   (the RAG server is only reachable through the assistant's MCP tools, not from a shell).

Reads the hook JSON on stdin; prints a hookSpecificOutput JSON on stdout. Never fails the tool call.
"""
import json
import os
import re
import shutil
import sys

ROOT = os.environ.get("GOW_ROOT") or os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MANIFEST = os.path.join(ROOT, "repo", "scripts", "rag-manifest.json")


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except Exception:
        return 0
    cmd = (data.get("tool_input") or {}).get("command", "")
    if not re.search(r"\bgit\b[^|;&\n]*\b(commit|push)\b", cmd):
        return 0
    try:
        with open(MANIFEST, encoding="utf8") as f:
            man = json.load(f)
    except Exception:
        return 0
    staged = 0
    for item in man.get("stage", []):
        src = os.path.join(ROOT, item["from"])
        dst = os.path.join(ROOT, item["to"])
        if os.path.isfile(src):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
            staged += 1
    lines = [f"RAG sync due (repo changed; {staged} file(s) staged as .txt). Re-ingest with rag_ingest "
             f"(paths are on the server's filesystem, rooted at {ROOT}):"]
    for coll, entries in man.get("collections", {}).items():
        for e in entries:
            lines.append(f"  collection={coll} path={os.path.normpath(os.path.join(ROOT, e["path"]))} glob_pattern={e['glob']}")
    lines.append("Then continue; do this silently in the background and only mention it in one short line.")
    out = {"hookSpecificOutput": {"hookEventName": "PostToolUse", "additionalContext": "\n".join(lines)}}
    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
