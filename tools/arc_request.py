#!/usr/bin/env python3
"""File a request for an ARC change as a GitHub issue (0.10.13).

    python3 <ARC>/tools/arc_request.py request.md --game "Vector Trader"
    python3 <ARC>/tools/arc_request.py --list

The request is a markdown file in the docs/REQUESTS.md template (its arc-check block included). It becomes an issue
on the ARC repository with the `request` label, titled "[Game] <the file's first heading>", so ARC's maintainer can
read it while this machine is off. The issue is closed when the release that does it is merged, with the done-note
as a comment.

It uses `gh api` (the REST API: GraphQL is not available everywhere), so `gh` must be signed in with access to the
repository. When it is not, or the call fails, the request is saved in <ARC>/incoming/ instead, as before, and the
script says so. Standard library only.

Options:
  --game NAME     the game asking (put in the title; default: the nearest folder above the file with a build script)
  --title TEXT    the title (default: the file's first "# " heading, without "Request from ...:")
  --repo O/R      the repository (default: $ARC_REPO or Arowx/arc)
  --file-only     skip GitHub, save in incoming/
  --dry-run       print what would be sent and change nothing
  --list          list the open requests (number, title, link)
"""
import argparse, json, os, re, shutil, subprocess, sys

ARC_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LABEL = "request"


def gh_api(args, payload=None):
    """Run `gh api ARGS` (payload as JSON on stdin). Returns (ok, parsed JSON or error text)."""
    if not shutil.which("gh"):
        return False, "gh is not installed (https://cli.github.com)"
    cmd = ["gh", "api"] + args + (["--input", "-"] if payload is not None else [])
    try:
        p = subprocess.run(cmd, input=json.dumps(payload) if payload is not None else None, capture_output=True,
                           text=True, encoding="utf-8", timeout=60)
    except (OSError, subprocess.TimeoutExpired) as e:
        return False, str(e)
    if p.returncode != 0:
        return False, (p.stderr or p.stdout).strip()
    try:
        return True, json.loads(p.stdout) if p.stdout.strip() else None
    except ValueError:
        return True, p.stdout


def title_of(text, game):
    for line in text.splitlines():
        if line.startswith("# "):
            t = line[2:].strip()
            t = re.sub(r"^Request from [^:]+:\s*", "", t)   # the template's own prefix: the game goes in brackets instead
            return t
    return "request"


def game_name(path):
    """The game's folder: the nearest one above the request with a build script, else the request's own folder."""
    d = os.path.dirname(os.path.abspath(path))
    while True:
        if os.path.exists(os.path.join(d, "build.bat")) or os.path.exists(os.path.join(d, "build.sh")):
            return os.path.basename(d)
        up = os.path.dirname(d)
        if up == d:
            return os.path.basename(os.path.dirname(os.path.abspath(path))) or "game"
        d = up


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")[:60] or "request"


def save_file(text, game, title, why):
    folder = os.path.join(ARC_DIR, "incoming")
    os.makedirs(folder, exist_ok=True)
    base = slug(game) + "-" + slug(title)
    path = os.path.join(folder, base + ".md")
    n = 2
    while os.path.exists(path):
        path = os.path.join(folder, "%s-%d.md" % (base, n)); n += 1
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text if text.endswith("\n") else text + "\n")
    print("ARC request saved %s (%s)" % (path, why))
    return 0


def main():
    ap = argparse.ArgumentParser(description="File a request for an ARC change as a GitHub issue.")
    ap.add_argument("file", nargs="?")
    ap.add_argument("--game"); ap.add_argument("--title")
    ap.add_argument("--repo", default=os.environ.get("ARC_REPO", "Arowx/arc"))
    ap.add_argument("--file-only", action="store_true"); ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()

    if a.list:
        ok, res = gh_api(["repos/%s/issues?labels=%s&state=open&per_page=100" % (a.repo, LABEL)])
        if not ok:
            print("ARC error cannot list requests: %s" % res); return 1
        for i in res:
            if "pull_request" not in i:
                print("#%d  %s  %s" % (i["number"], i["title"], i["html_url"]))
        return 0

    if not a.file:
        ap.error("give a request file (or --list)")
    with open(a.file, encoding="utf-8") as f:
        text = f.read()
    if "```arc-check" not in text:
        print("ARC hint request has no ```arc-check block: add one (docs/REQUESTS.md), so done means the same to both sides")
    game = a.game or game_name(a.file)
    title = a.title or title_of(text, game)
    issue = {"title": "[%s] %s" % (game, title), "labels": [LABEL],
             "body": text.rstrip("\n") + "\n\n---\n*Filed by `tools/arc_request.py` for %s.*\n" % game}

    if a.dry_run:
        print("ARC request dry run: POST repos/%s/issues" % a.repo)
        print(json.dumps(issue, indent=2)); return 0
    if a.file_only:
        return save_file(text, game, title, "--file-only")
    ok, res = gh_api(["repos/%s/issues" % a.repo], issue)
    if not ok:
        return save_file(text, game, title, "GitHub failed: %s" % res.splitlines()[-1] if res else "GitHub failed")
    print("ARC request filed #%d %s" % (res["number"], res["html_url"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
