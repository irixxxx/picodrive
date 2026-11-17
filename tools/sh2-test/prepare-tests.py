#!/usr/bin/env python3
"""Fetch SingleStepTests/sh4 vectors for sh2_test.c.

Upstream stores the vectors in the binary format the harness reads directly,
one file per encoding with 500 tests each, about 92 MB for the whole set.

The vectors are pulled with git instead of the GitHub contents API.  A
shallow, blobless clone keeps just the tree metadata (a few hundred kB); the
selected blobs are then downloaded on demand.  That avoids the API's
1000-entry listing cap and lets a run be pinned to a revision.

usage:
  ./prepare-tests.py              # every vector file
  ./prepare-tests.py --force      # re-fetch existing files
  ./prepare-tests.py --list       # list remote file names
  ./prepare-tests.py --rev=REV    # fetch from REV (default origin/main)

SST_SH4_REV has the same meaning as --rev.
"""

import argparse
import os
import subprocess
import sys
import tarfile

BASE = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.join(BASE, "tests", "sh4", "v1")
CACHE = os.path.join(BASE, "tests", "sh4", ".sst-sh4")
ARCHIVE = os.path.join(BASE, "tests", "sh4", ".archive.tar")
REPO_URL = "https://github.com/SingleStepTests/sh4.git"
UPSTREAM_REF = "origin/main"
DEFAULT_REV = os.environ.get("SST_SH4_REV", UPSTREAM_REF)

# Above this many missing files one archive is cheaper than one git show per
# file: the archive downloads the blob set as a single pack.
BULK_THRESHOLD = 16


def _git(args, check=True):
    return subprocess.run(["git", "-C", CACHE] + list(args),
                          check=check, stdout=subprocess.PIPE)


def ensure_cache(rev):
    """Create or refresh the blobless clone that caches the vector tree."""
    if not os.path.isdir(os.path.join(CACHE, ".git")):
        os.makedirs(os.path.dirname(CACHE), exist_ok=True)
        print("cloning %s into %s" % (REPO_URL, CACHE), file=sys.stderr)
        subprocess.run(["git", "clone", "--quiet", "--depth", "1",
                        "--filter=blob:none", "--no-checkout",
                        REPO_URL, CACHE], check=True)
    else:
        _git(["fetch", "--quiet", "--depth", "1", "origin"])
    if rev != UPSTREAM_REF:
        have = _git(["rev-parse", "--verify", "--quiet", rev + "^{commit}"],
                    check=False)
        if have.returncode != 0:
            _git(["fetch", "--quiet", "--depth", "1", "origin", rev],
                 check=False)


def resolve_rev(rev):
    return _git(["rev-parse", "--short", rev]).stdout.decode().strip()


def list_remote(rev):
    out = _git(["ls-tree", "-r", "--name-only", rev]).stdout.decode()
    return sorted(line for line in out.splitlines()
                  if line.endswith(".json.bin"))


def fetch_one(name, rev):
    """Fetch a single blob; only that blob is downloaded."""
    data = _git(["show", "%s:%s" % (rev, name)]).stdout
    _write(os.path.join(DEST, name), data)


def fetch_bulk(names, rev):
    """Fetch the wanted files from one archive.

    `git archive` downloads the whole blob set in a single pack, which is
    what a first, full fetch wants.
    """
    wanted = set(names)
    subprocess.run(["git", "-C", CACHE, "archive", "--format=tar",
                    "--output=" + ARCHIVE, rev], check=True)
    written = 0
    try:
        with tarfile.open(ARCHIVE, mode="r:") as tar:
            for member in tar.getmembers():
                if not member.isfile() or member.name not in wanted:
                    continue
                _write(os.path.join(DEST, member.name),
                       tar.extractfile(member).read())
                written += 1
    finally:
        if os.path.exists(ARCHIVE):
            os.remove(ARCHIVE)
    return written


def _write(path, data):
    # Write beside the target and rename, so an interrupted run never leaves a
    # truncated file that a later run would treat as complete.
    tmp = path + ".part"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--force", action="store_true", help="re-fetch")
    parser.add_argument("--list", action="store_true",
                        help="list remote file names")
    parser.add_argument("--rev", default=DEFAULT_REV, metavar="REV",
                        help="git revision to fetch (default: %(default)s)")
    args = parser.parse_args()

    os.makedirs(DEST, exist_ok=True)

    ensure_cache(args.rev)
    remote = list_remote(args.rev)
    rev = resolve_rev(args.rev)
    print("upstream sh4 at %s: %u files" % (rev, len(remote)), file=sys.stderr)

    if args.list:
        for name in remote:
            print(name)
        return 0

    todo = [n for n in remote
            if args.force or not os.path.exists(os.path.join(DEST, n))]
    if todo:
        if len(todo) > BULK_THRESHOLD:
            print("fetching %u files at %s (bulk archive)"
                  % (len(todo), rev), file=sys.stderr)
            fetch_bulk(todo, args.rev)
        else:
            for i, name in enumerate(todo, 1):
                fetch_one(name, args.rev)
                if i % 16 == 0 or i == len(todo):
                    print("fetched %u/%u" % (i, len(todo)), file=sys.stderr)
    print("downloaded %u of %u files into %s"
          % (len(todo), len(remote), DEST))
    return 0


if __name__ == "__main__":
    sys.exit(main())
