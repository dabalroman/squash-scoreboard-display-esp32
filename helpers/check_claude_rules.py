"""
Check the path-scoped Claude Code rules in .claude/rules/ against the tree.

Usage:
    python helpers/check_claude_rules.py

A rule file loads only when a file matching one of its `paths:` globs is read or
edited, so a glob that matches nothing - after a move or rename - silently stops
the rule from ever loading. Fails when:

    a rule file has no `paths:` frontmatter (it would load in every session)
    a glob matches no file in the repo (tracked or untracked, .gitignore honoured)
    a rule file is missing from the index table in CLAUDE.md, or the index
    names a rule file that does not exist

Exits non-zero on any failure.
"""

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
RULES_DIR = ROOT / ".claude" / "rules"
INDEX_ROW = re.compile(r"^\|\s*`([\w.-]+\.md)`\s*\|", re.MULTILINE)


def repo_files():
    out = subprocess.run(["git", "ls-files", "-co", "--exclude-standard"], cwd=ROOT,
                         capture_output=True, text=True, check=True).stdout
    return [line for line in out.splitlines() if line]


def glob_to_regex(glob):
    """Claude Code rule glob -> regex: `**` spans directories, `*` and `?` do not, {a,b} alternates."""
    out, i = "", 0
    while i < len(glob):
        if glob.startswith("**/", i):
            out, i = out + "(?:.*/)?", i + 3
        elif glob.startswith("**", i):
            out, i = out + ".*", i + 2
        elif glob[i] == "*":
            out, i = out + "[^/]*", i + 1
        elif glob[i] == "?":
            out, i = out + "[^/]", i + 1
        elif glob[i] == "{":
            end = glob.index("}", i)
            out += "(?:" + "|".join(re.escape(a) for a in glob[i + 1:end].split(",")) + ")"
            i = end + 1
        else:
            out, i = out + re.escape(glob[i]), i + 1
    return re.compile(out + r"\Z")


def read_paths(text):
    """The `paths:` value of the frontmatter: a YAML list or a comma-separated string. None if absent."""
    if not text.startswith("---\n"):
        return None
    end = text.find("\n---", 4)
    if end < 0:
        return None
    lines = text[4:end].splitlines()
    for n, line in enumerate(lines):
        if not line.startswith("paths:"):
            continue
        inline = line[len("paths:"):].strip()
        if inline:
            return [p.strip().strip("\"'") for p in inline.split(",") if p.strip()]
        items = []
        for item in lines[n + 1:]:
            if not item.startswith((" ", "\t")):
                break
            item = item.strip()
            if item.startswith("- "):
                items.append(item[2:].strip().strip("\"'"))
        return items
    return None


def main():
    errors = []
    files = repo_files()
    rules = sorted(RULES_DIR.rglob("*.md"))
    if not rules:
        errors.append(f"no rule files in {RULES_DIR.relative_to(ROOT)}")

    for rule in rules:
        name = rule.relative_to(ROOT).as_posix()
        globs = read_paths(rule.read_text(encoding="utf-8"))
        if not globs:
            errors.append(f"{name}: no `paths:` frontmatter")
            continue
        for glob in globs:
            pattern = glob_to_regex(glob)
            hits = sum(1 for f in files if pattern.match(f))
            if hits == 0:
                errors.append(f"{name}: glob {glob!r} matches no file")
        print(f"{name}: {len(globs)} globs")

    indexed = set(INDEX_ROW.findall((ROOT / "CLAUDE.md").read_text(encoding="utf-8")))
    present = {r.relative_to(RULES_DIR).as_posix() for r in rules}
    for missing in sorted(present - indexed):
        errors.append(f"CLAUDE.md: rule index lacks {missing}")
    for stale in sorted(indexed - present):
        errors.append(f"CLAUDE.md: rule index names missing {stale}")

    for e in errors:
        print("FAIL", e)
    print("verdict", "Fail" if errors else "Ok")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
