#!/usr/bin/env python3
"""Fetch and pack SingleStepTests/z80 vectors for z80_test.c.

Upstream v1 holds one JSON file per opcode with 1000 tests each.  The harness
reads packed .json.bin files, so this script only needs to run when the test
data is fetched or refreshed.

The vectors are pulled with git instead of the GitHub contents API.  A
shallow, blobless clone keeps just the tree metadata (a few hundred kB); the
selected blobs are then downloaded on demand.  That avoids the API's
1000-entry listing cap, which silently truncated the v1 directory, and it
lets a run be pinned to a revision.

usage:
  ./prepare-tests.py              # representative smoke set
  ./prepare-tests.py --all        # every file in v1
  ./prepare-tests.py --force      # re-fetch and repack existing files
  ./prepare-tests.py --pack-only  # repack already downloaded json files
  ./prepare-tests.py --list       # list remote file names
  ./prepare-tests.py --rev=REV    # fetch from REV (default origin/main)

SST_Z80_REV has the same meaning as --rev.
"""

import argparse
import json
import os
import struct
import subprocess
import sys
import tarfile

BASE = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.join(BASE, "tests", "z80", "v1")
CACHE = os.path.join(BASE, "tests", "z80", ".sst-z80")
ARCHIVE = os.path.join(BASE, "tests", "z80", ".archive.tar")
REPO_URL = "https://github.com/SingleStepTests/z80.git"
UPSTREAM_REF = "origin/main"
DEFAULT_REV = os.environ.get("SST_Z80_REV", UPSTREAM_REF)

# Representative subset: base, CB and DD opcodes, memory/branch/stack forms,
# HALT, port I/O, the DI/EI pair and the ED 40 port form.
SMOKE = [
    "00.json", "04.json", "09.json", "10.json", "18.json", "20.json",
    "22.json", "27.json", "2a.json", "31.json", "36.json", "3a.json",
    "3e.json", "44.json", "70.json", "76.json", "7e.json", "80.json",
    "86.json", "a0.json", "b0.json", "c0.json", "c1.json", "c3.json",
    "c5.json", "c6.json", "c9.json", "cd.json", "d3.json", "db.json",
    "e3.json", "e9.json", "eb.json", "f3.json", "fb.json",
    "cb 00.json", "cb 46.json", "cb 7e.json", "cb c0.json",
    "dd 09.json", "dd 21.json", "dd 36.json", "dd 7e.json",
    "dd e1.json", "dd e5.json", "dd cb __ 06.json",
    "ed 40.json",
]

# Must match z80_test.c.
MAGIC_FILE = 0x5A383054
MAGIC_TEST = 0xABC12368
MAGIC_NAME = 0x89ABCDEF
MAGIC_STATE = 0x01234567
MAGIC_BUS = 0x456789AB


def _git(args, check=True):
    return subprocess.run(["git", "-C", CACHE] + list(args),
                          check=check, stdout=subprocess.PIPE)


def ensure_cache(rev):
    """Create or refresh the blobless clone that caches the v1 tree."""
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
    out = _git(["ls-tree", "-r", "--name-only", rev, "v1"]).stdout.decode()
    return sorted(line[len("v1/"):] for line in out.splitlines()
                  if line.startswith("v1/") and line.endswith(".json"))


def fetch_one(name, rev):
    """Fetch a single blob; only that blob is downloaded."""
    data = _git(["show", "%s:v1/%s" % (rev, name)]).stdout
    _write(os.path.join(DEST, name), data)


def fetch_bulk(names, rev):
    """Fetch all of v1 in one archive; git downloads the whole blob set.

    This is what `--all` wants, but it is deliberately not used for the small
    smoke set, where a per-file `git show` downloads far less.
    """
    wanted = set(names)
    subprocess.run(["git", "-C", CACHE, "archive", "--format=tar",
                    "--output=" + ARCHIVE, rev, "v1"], check=True)
    written = 0
    try:
        with tarfile.open(ARCHIVE, mode="r:") as tar:
            for member in tar.getmembers():
                if not member.isfile():
                    continue
                parts = member.name.split("/", 1)
                if len(parts) != 2 or parts[1] not in wanted:
                    continue
                _write(os.path.join(DEST, parts[1]),
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


def _word(state, key):
    return int(state.get(key, 0)) & 0xFFFF


def _state_block(state):
    af = ((int(state.get("a", 0)) & 0xFF) << 8) | (int(state.get("f", 0)) & 0xFF)
    bc = ((int(state.get("b", 0)) & 0xFF) << 8) | (int(state.get("c", 0)) & 0xFF)
    de = ((int(state.get("d", 0)) & 0xFF) << 8) | (int(state.get("e", 0)) & 0xFF)
    hl = ((int(state.get("h", 0)) & 0xFF) << 8) | (int(state.get("l", 0)) & 0xFF)

    words = [af, bc, de, hl,
             _word(state, "ix"), _word(state, "iy"),
             _word(state, "sp"), _word(state, "pc"), _word(state, "wz")]
    shadows = [_word(state, key) for key in ("af_", "bc_", "de_", "hl_")]
    regs = [int(state.get(key, 0)) & 0xFF for key in
            ("i", "r", "im", "iff1", "iff2", "p", "q", "ei")]

    body = struct.pack("<9H4H8B", *(words + shadows + regs))

    ram = [(int(addr) & 0xFFFF, int(value) & 0xFF)
           for addr, value in (state.get("ram") or [])
           if value is not None]
    body += struct.pack("<I", len(ram))
    for addr, value in ram:
        body += struct.pack("<HB", addr, value)

    return struct.pack("<II", len(body), MAGIC_STATE) + body


def _bus_block(test):
    cycles = test.get("cycles") or []
    ports = b""
    count = 0

    for entry in test.get("ports") or []:
        port, value, kind = entry[0], entry[1], entry[2]
        if port is None or value is None:
            continue
        is_write = 1 if str(kind).startswith("w") else 0
        ports += struct.pack("<HBB", int(port) & 0xFFFF, int(value) & 0xFF, is_write)
        count += 1

    body = struct.pack("<II", len(cycles), count) + ports
    return struct.pack("<II", len(body), MAGIC_BUS) + body


def pack(json_path, bin_path):
    with open(json_path, "r", encoding="utf-8") as f:
        tests = json.load(f)

    data = [struct.pack("<II", MAGIC_FILE, len(tests))]
    for test in tests:
        name = test.get("name", "").encode("utf-8")
        data.append(struct.pack("<II", 0, MAGIC_TEST))
        data.append(struct.pack("<II", 0, MAGIC_NAME))
        data.append(struct.pack("<I", len(name)) + name)
        data.append(_state_block(test.get("initial") or {}))
        data.append(_state_block(test.get("final") or {}))
        data.append(_bus_block(test))
    _write(bin_path, b"".join(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--all", action="store_true", help="fetch every v1 file")
    parser.add_argument("--force", action="store_true", help="re-fetch and repack")
    parser.add_argument("--pack-only", action="store_true",
                        help="only repack files already in tests/z80/v1")
    parser.add_argument("--list", action="store_true", help="list remote file names")
    parser.add_argument("--rev", default=DEFAULT_REV, metavar="REV",
                        help="git revision to fetch (default: %(default)s)")
    args = parser.parse_args()

    os.makedirs(DEST, exist_ok=True)

    if args.pack_only:
        names = sorted(n for n in os.listdir(DEST) if n.endswith(".json"))
    else:
        ensure_cache(args.rev)
        remote = list_remote(args.rev)
        rev = resolve_rev(args.rev)
        print("upstream v1 at %s: %u files" % (rev, len(remote)), file=sys.stderr)
        if args.list:
            for name in remote:
                print(name)
            return 0
        selected = remote if args.all else SMOKE
        missing = [n for n in selected if n not in remote]
        if missing:
            print("warning: not in the remote v1 listing: %s" % ", ".join(missing),
                  file=sys.stderr)
        names = [n for n in selected if n in remote]
        todo = [n for n in names
                if args.force or not os.path.exists(os.path.join(DEST, n))]
        if todo:
            if args.all:
                print("fetching %u files at %s (bulk archive)" % (len(todo), rev),
                      file=sys.stderr)
                fetch_bulk(todo, args.rev)
            else:
                for i, name in enumerate(todo, 1):
                    fetch_one(name, args.rev)
                    if i % 16 == 0 or i == len(todo):
                        print("fetched %u/%u" % (i, len(todo)), file=sys.stderr)
        print("downloaded %u of %u files" % (len(todo), len(names)))

    packed = 0
    for name in names:
        json_path = os.path.join(DEST, name)
        bin_path = json_path + ".bin"
        if not os.path.exists(json_path):
            continue
        if os.path.exists(bin_path) and not args.force:
            if os.path.getmtime(bin_path) >= os.path.getmtime(json_path):
                continue
        pack(json_path, bin_path)
        packed += 1
    print("packed %u files into %s" % (packed, DEST))
    return 0


if __name__ == "__main__":
    sys.exit(main())
