#!/usr/bin/env python3
"""Sync data/ and the generated tables to one release of the Python reference.

The C++ engine and the language specs change together: a spec field without
the engine code that reads it is silently ignored. So the specs are vendored at
a pinned reference release, and this script is the only way they move. It

  1. downloads the orthography2ipa wheel for VERSION from PyPI and checks its
     sha256 against the digest PyPI publishes;
  2. replaces data/*.json with the wheel's specs (removed specs are deleted);
  3. writes the pin to tests/reference_version.txt;
  4. re-runs the tests/gen_*.py table generators against that same wheel, so
     the transcribed registry, type, script and transform tables match it;
  5. pins the reference checkout in .github/workflows/ci.yml to the commit
     that the reference repository tags as VERSION, so CI compares against
     the same release.

The tag is resolved before step 1. A PyPI release that the reference
repository never tagged stops the script before anything is written, so data/
and ci.yml cannot end up at two different releases.

The generators import the reference package, so run this script with a Python
that has the reference's dependencies installed (for example a virtual
environment with `orthography2ipa[portuguese]==VERSION`). The package itself is
always taken from the downloaded wheel, never from that environment.

Usage:
    python3 tools/sync_reference.py 7.111.14a1
    python3 tools/sync_reference.py latest            # newest release, prereleases included
    python3 tools/sync_reference.py 7.111.14a1 --langcodes   # also regenerate langcodes tables

Then rebuild and run the parity suite with O2I_PYTHON_ROOT pointing at a
reference of the same version.
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import zipfile

PACKAGE = "orthography2ipa"
CPP_ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = CPP_ROOT / "data"
PIN = CPP_ROOT / "tests" / "reference_version.txt"
CI = CPP_ROOT / ".github" / "workflows" / "ci.yml"
REFERENCE_REPO = "TigreGotico/orthography2ipa"
GENERATORS = (
    "gen_registry_tables.py",
    "gen_types_tables.py",
    "gen_script_registry.py",
    "gen_transform_tables.py",
)


def pypi_json(path: str) -> dict:
    with urllib.request.urlopen(f"https://pypi.org/pypi/{PACKAGE}/{path}", timeout=60) as response:
        return json.load(response)


def latest_version() -> str:
    """Newest upload of any release, prereleases included."""
    releases = pypi_json("json")["releases"]
    uploaded = {version: max(f["upload_time_iso_8601"] for f in files)
                for version, files in releases.items() if files}
    return max(uploaded, key=uploaded.get)


def fetch_wheel(version: str, into: pathlib.Path) -> pathlib.Path:
    files = pypi_json(f"{version}/json")["urls"]
    wheels = [f for f in files if f["packagetype"] == "bdist_wheel"]
    if not wheels:
        sys.exit(f"{PACKAGE} {version} has no wheel on PyPI")
    wheel = wheels[0]
    target = into / wheel["filename"]
    with urllib.request.urlopen(wheel["url"], timeout=300) as response:
        target.write_bytes(response.read())
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    if digest != wheel["digests"]["sha256"]:
        sys.exit(f"sha256 mismatch for {wheel['filename']}: got {digest}")
    print(f"wheel: {wheel['filename']} sha256={digest}")
    return target


def sync_specs(source: pathlib.Path) -> None:
    new = {p.name: p for p in source.glob("*.json")}
    old = {p.name for p in DATA.glob("*.json")}
    if not new:
        sys.exit(f"no specs found in {source}")

    removed = sorted(old - set(new))
    added = sorted(set(new) - old)
    changed = 0
    for name in removed:
        (DATA / name).unlink()
    for name, path in new.items():
        target = DATA / name
        if name in old and target.read_bytes() == path.read_bytes():
            continue
        changed += name in old
        shutil.copyfile(path, target)

    copied = sorted(p.name for p in DATA.glob("*.json"))
    if copied != sorted(new):
        sys.exit("data/ does not match the wheel's spec set after the copy")
    print(f"specs: {len(new)} in the wheel, {len(copied)} in data/; "
          f"{len(added)} added, {len(removed)} removed, {changed} changed")
    for name in removed:
        print(f"  removed {name}")
    for name in added:
        print(f"  added   {name}")


def tag_commit(version: str) -> str:
    """The commit that the reference repository tags *version*."""
    url = f"https://api.github.com/repos/{REFERENCE_REPO}/commits/{version}"
    try:
        with urllib.request.urlopen(url, timeout=60) as response:
            return json.load(response)["sha"]
    except urllib.error.HTTPError as error:
        sys.exit(f"{REFERENCE_REPO} has no tag {version} (GitHub answered {error.code}); "
                 "nothing was written")


def pin_ci(sha: str) -> None:
    """Point the CI reference checkout at *sha*."""
    text = CI.read_text(encoding="utf-8")
    pattern = rf"(repository: {re.escape(REFERENCE_REPO)}\n\s+ref: )[0-9a-f]{{40}}"
    pinned, count = re.subn(pattern, rf"\g<1>{sha}", text)
    if count != 1:
        sys.exit(f"{CI.relative_to(CPP_ROOT)}: expected one {REFERENCE_REPO} checkout, found {count}")
    CI.write_text(pinned, encoding="utf-8")
    print(f"pinned {CI.relative_to(CPP_ROOT)}: {REFERENCE_REPO}@{sha}")


def run_generators(reference: pathlib.Path, python: str, langcodes: bool) -> None:
    env = dict(os.environ, O2I_PYTHON_ROOT=str(reference))
    names = GENERATORS + (("gen_langcodes_tables.py",) if langcodes else ())
    for name in names:
        result = subprocess.run([python, str(CPP_ROOT / "tests" / name)], env=env,
                                cwd=CPP_ROOT, capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"{name} failed:\n{result.stdout}{result.stderr}")
        print(f"{name}: {result.stdout.strip().splitlines()[-1] if result.stdout.strip() else 'ok'}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("version", help="a release such as 7.111.14a1, or 'latest'")
    parser.add_argument("--python", default=sys.executable,
                        help="interpreter that runs the generators (needs the reference's dependencies)")
    parser.add_argument("--langcodes", action="store_true",
                        help="also regenerate src/langcodes_tables.inc from the installed langcodes")
    args = parser.parse_args()

    version = latest_version() if args.version == "latest" else args.version
    print(f"reference: {PACKAGE}=={version}")
    sha = tag_commit(version)
    print(f"tag: {REFERENCE_REPO}@{version} is {sha}")
    with tempfile.TemporaryDirectory(prefix="o2i-reference-") as scratch:
        scratch = pathlib.Path(scratch)
        with zipfile.ZipFile(fetch_wheel(version, scratch)) as wheel:
            wheel.extractall(scratch / "root")
        reference = scratch / "root"
        if not (reference / PACKAGE / "phonetok.py").is_file():
            sys.exit("the wheel does not contain the reference package")
        sync_specs(reference / PACKAGE / "data")
        PIN.write_text(f"{PACKAGE}=={version}\n", encoding="utf-8")
        run_generators(reference, args.python, args.langcodes)
    print(f"pinned {PIN.relative_to(CPP_ROOT)}: {PACKAGE}=={version}")
    pin_ci(sha)


if __name__ == "__main__":
    main()
