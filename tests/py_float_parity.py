#!/usr/bin/env python3
"""py_float parity: the CLI's float text against Python repr(), which the
reference's json.dumps writes.

The inputs are every power of two from 2**-1074 to 2**1023 with both signs,
where the shortest round-trip digits are not always the correctly rounded
ones, plus zeros, the bounds of the fixed notation, and 20000 seeded random
doubles. Exit 0 only when every text equals repr().

Usage: py_float_parity.py <path-to-check-binary>
"""
import random
import struct
import subprocess
import sys

values = [0.0, -0.0, 1e-4, 1e-5, 1e15, 1e16, 9999999999999998.0, 5e-324, 1.7976931348623157e308]
values += [sign * 2.0 ** e for e in range(-1074, 1024) for sign in (1.0, -1.0)]
rng = random.Random(29)
values += [struct.unpack("<d", struct.pack("<Q", rng.getrandbits(64)))[0] for _ in range(10000)]
values += [rng.random() for _ in range(10000)]
values = [v for v in values if v == v and abs(v) != float("inf")]
out = subprocess.run([sys.argv[1]], input="\n".join(v.hex() for v in values) + "\n",
                     capture_output=True, text=True, check=True).stdout.splitlines()
if len(out) != len(values):
    sys.exit(f"py_float parity: {len(values)} values in, {len(out)} out")
bad = [(v, got) for v, got in zip(values, out) if got != repr(v)]
print(f"py_float parity: {len(values)} values, {len(bad)} differ from repr()")
for v, got in bad[:10]:
    print(f"  {v.hex()}: repr {v!r}, py_float {got}")
sys.exit(1 if bad else 0)
