#!/usr/bin/env python3
"""Fetch and pack SingleStepTests/z80 vectors for z80_test.c.

Upstream v1 holds one JSON file per opcode with 1000 tests each.  The harness
reads packed .json.bin files, so this script only needs to run when the test
data is fetched or refreshed.

usage:
  ./prepare-tests.py              # representative smoke set
  ./prepare-tests.py --all        # every file in v1
  ./prepare-tests.py --force      # re-download and repack existing files
  ./prepare-tests.py --pack-only  # repack already downloaded json files
  ./prepare-tests.py --list       # list remote file names
"""

import argparse
import concurrent.futures
import json
import os
import struct
import sys
import urllib.parse
import urllib.request

API_URL = "https://api.github.com/repos/SingleStepTests/z80/contents/v1"
RAW_BASE = "https://raw.githubusercontent.com/SingleStepTests/z80/main/v1"
DEST = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    "tests", "z80", "v1")

# The full v1 directory is 1000 files; a few connections make that bearable
# without hammering github too hard.
WORKERS = 8

# Representative subset: base, CB and DD opcodes, memory/branch/stack forms,
# HALT, port I/O and the one ED file upstream currently ships.
SMOKE = [
    "00.json", "04.json", "09.json", "10.json", "18.json", "20.json",
    "22.json", "27.json", "2a.json", "31.json", "36.json", "3a.json",
    "3e.json", "44.json", "70.json", "76.json", "7e.json", "80.json",
    "86.json", "a0.json", "b0.json", "c0.json", "c1.json", "c3.json",
    "c5.json", "c6.json", "c9.json", "cd.json", "d3.json", "db.json",
    "e3.json", "e9.json", "eb.json",
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


def fetch_names():
    with urllib.request.urlopen(API_URL) as response:
        entries = json.load(response)
    return sorted(e["name"] for e in entries if e["name"].endswith(".json"))


def _write(path, data):
    # Write beside the target and rename, so an interrupted run never leaves a
    # truncated file that a later run would treat as complete.
    tmp = path + ".part"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def download(name, force):
    path = os.path.join(DEST, name)
    if os.path.exists(path) and not force:
        return False
    url = RAW_BASE + "/" + urllib.parse.quote(name)
    request = urllib.request.Request(url, headers={"User-Agent": "z80-test-harness"})
    with urllib.request.urlopen(request) as response:
        data = response.read()
    _write(path, data)
    return True


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
    parser.add_argument("--force", action="store_true", help="re-download and repack")
    parser.add_argument("--pack-only", action="store_true",
                        help="only repack files already in tests/z80/v1")
    parser.add_argument("--list", action="store_true", help="list remote file names")
    args = parser.parse_args()

    os.makedirs(DEST, exist_ok=True)

    if args.pack_only:
        names = sorted(n for n in os.listdir(DEST) if n.endswith(".json"))
    else:
        remote = fetch_names()
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
        fetched = 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
            futures = [(name, pool.submit(download, name, args.force)) for name in names]
            for name, future in futures:
                try:
                    if future.result():
                        fetched += 1
                except urllib.error.URLError as e:
                    print("error: %s: %s" % (name, e), file=sys.stderr)
        print("downloaded %u of %u files" % (fetched, len(names)))

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
