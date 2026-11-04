#!/usr/bin/env python3
"""lexicon parity: the port's lexicon sources against the reference's
lexicon.py, for local paths that start with ~ and for sources that are not a
file.

resolve_lexicon_source expands a leading ~ as pathlib.Path.expanduser does and
raises FileNotFoundError("lexicon not found: <source>") for a local source that
is not a file. get_lexicon and lexicon_path reach it through register_lexicon,
and set_lexicon_dir and $ORTHOGRAPHY2IPA_LEXICON_DIR expand ~ too. Each run
points HOME at a temporary directory, sets it empty or to ~, or unsets it, and
compares the result or the exception type and message line by line. Exit 0
only when every line matches.

Usage: lexicon_parity.py <path-to-check-lexicon-binary>
"""
import getpass
import os
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
reference = os.environ.get("O2I_PYTHON_ROOT") or str(HERE.parent.parent / "orthography2ipa")
if not (pathlib.Path(reference) / "orthography2ipa" / "lexicon.py").is_file():
    sys.exit(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
sys.path.insert(0, reference)

from orthography2ipa import lexicon  # noqa: E402

UNKNOWN_USER = "~o2i-no-such-user"
USER = "~" + getpass.getuser()


def expected(field):
    command = field[0]
    try:
        if command == "resolve":
            value = str(lexicon.resolve_lexicon_source(field[1]))
        elif command == "envget":
            value = "".join(f"{word}={ipa}," for word, ipa in sorted(lexicon.get_lexicon(field[1]).items()))
        elif command in ("get", "path"):
            lexicon.register_lexicon(field[1], field[2])
            if command == "path":
                value = str(lexicon.lexicon_path(field[1]))
            else:
                value = "".join(f"{word}={ipa}," for word, ipa in sorted(lexicon.get_lexicon(field[1]).items()))
        else:
            if command == "dir":
                lexicon.set_lexicon_dir(field[1])
            value = str(lexicon.lexicon_path(field[-1]))
        return f"ok\t{value}"
    except (FileNotFoundError, RuntimeError) as error:
        return f"{type(error).__name__}\t{error}"
    finally:
        lexicon.clear_lexicons()


def check(binary, root, label, env, commands):
    environ = {k: v for k, v in os.environ.items() if k not in ("HOME", "ORTHOGRAPHY2IPA_LEXICON_DIR")}
    environ.update({k: v for k, v in env.items() if v is not None})
    environ["XDG_CACHE_HOME"] = str(root / "cache")
    lines = ["\t".join(c) for c in commands]
    out = subprocess.run([binary], input="\n".join(lines) + "\n", capture_output=True, text=True,
                         check=True, cwd=root, env=environ).stdout.splitlines()
    if len(out) != len(commands):
        sys.exit(f"lexicon parity {label}: {len(commands)} commands in, {len(out)} lines out")
    saved, cwd = dict(os.environ), os.getcwd()
    os.environ.clear()
    os.environ.update(environ)
    os.chdir(root)
    try:
        want = [expected(c) for c in commands]
    finally:
        os.chdir(cwd)
        os.environ.clear()
        os.environ.update(saved)
    bad = [(line, w, g) for line, w, g in zip(lines, want, out) if w != g]
    for line, w, g in bad:
        print(f"FAIL {label}: {line!r}\n  python: {w!r}\n  cpp:    {g!r}")
    return len(commands), len(bad)


def main():
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        home = root / "home"
        (home / "sub").mkdir(parents=True)
        (home / "lex.tsv").write_text("casa\tkaza\n", encoding="utf-8")
        (home / "sub" / "en.tsv").write_text("zorkflim\tzɔɹkflɪm\n", encoding="utf-8")
        (root / "pt-PT.tsv").write_text("casa\tkaza\n", encoding="utf-8")
        (home / "loop.tsv").symlink_to("loop.tsv")
        (home / "loopdir").mkdir()
        (home / "loopdir" / "en-GB.tsv").symlink_to("en-GB.tsv")
        double_root = "//" + str(home).lstrip("/")
        local = [
            ("resolve", "~/lex.tsv"), ("resolve", "~//lex.tsv"), ("resolve", "~/./sub/../lex.tsv"),
            ("resolve", "~/lex.tsv/"), ("resolve", "~"), ("resolve", "~/"), ("resolve", "~/missing.tsv"),
            ("resolve", "home/lex.tsv"), ("resolve", "./home//lex.tsv"), ("resolve", "missing.tsv"),
            ("resolve", "/nonexistent-o2i/lex.tsv"), ("resolve", "home"), ("resolve", "a/~/lex.tsv"),
            ("resolve", UNKNOWN_USER + "/lex.tsv"), ("resolve", UNKNOWN_USER),
            ("resolve", USER + "/o2i-no-such-lexicon.tsv"), ("resolve", "~nobody/etc/passwd"),
            ("get", "pt-PT", "~/lex.tsv"), ("get", "pt-PT", "~/missing.tsv"), ("get", "pt", UNKNOWN_USER + "/x"),
            ("path", "pt-PT", "~/lex.tsv"), ("path", "pt-PT", "~/missing.tsv"),
            ("dir", "~/sub", "en-GB"), ("dir", "~/sub/", "en-GB"), ("dir", UNKNOWN_USER, "en-GB"),
            ("resolve", "~/loop.tsv"), ("get", "pt-PT", "~/loop.tsv"), ("path", "pt-PT", "~/loop.tsv"),
            ("dir", ".", "pt-PT"), ("dir", "", "pt-PT"), ("dir", "./", "pt-PT"), ("dir", "/", "pt-PT"),
        ]
        runs = [
            ("HOME=<tmp>", {"HOME": str(home)}, local),
            ("HOME=<tmp>/", {"HOME": str(home) + "/"}, [("resolve", "~/lex.tsv"), ("resolve", "~")]),
            ("HOME=", {"HOME": ""}, [("resolve", "~/lex.tsv"), ("resolve", "~"), ("resolve", "~/home/lex.tsv")]),
            ("HOME=/", {"HOME": "/"}, [("resolve", "~/lex.tsv"), ("resolve", "~" + str(home) + "/lex.tsv")]),
            ("HOME=~z", {"HOME": "~z"}, [("resolve", "~/lex.tsv"), ("resolve", "home/lex.tsv")]),
            ("HOME=.", {"HOME": "."}, [("dir", "~", "pt-PT"), ("resolve", "~/pt-PT.tsv")]),
            ("HOME=//", {"HOME": "//"}, [("resolve", "~/" + double_root[2:] + "/lex.tsv"), ("resolve", "~")]),
            ("HOME=//<tmp>", {"HOME": double_root}, [("resolve", "~/lex.tsv"), ("resolve", "~"),
                                                     ("dir", "~", "lex")]),
            ("HOME unset", {"HOME": None}, [("resolve", "~/o2i-no-such-lexicon.tsv"), ("resolve", "~")]),
            ("lexicon dir ~/sub", {"HOME": str(home), "ORTHOGRAPHY2IPA_LEXICON_DIR": "~/sub"},
             [("envdir", "en-GB"), ("envdir", "pt-PT")]),
            ("lexicon dir ~unknown", {"HOME": str(home), "ORTHOGRAPHY2IPA_LEXICON_DIR": UNKNOWN_USER},
             [("envdir", "en-GB")]),
            ("lexicon dir with a symlink loop", {"HOME": str(home), "ORTHOGRAPHY2IPA_LEXICON_DIR": "~/loopdir"},
             [("envdir", "en-GB"), ("envget", "en-GB"), ("envdir", "pt-PT")]),
            ("lexicon dir ~/sub, read", {"HOME": str(home), "ORTHOGRAPHY2IPA_LEXICON_DIR": "~/sub"},
             [("envget", "en-GB"), ("envget", "en-US")]),
        ]
        total = failed = 0
        for label, env, commands in runs:
            n, bad = check(binary, root, label, env, commands)
            total, failed = total + n, failed + bad
    print(f"lexicon parity: {total} commands, {failed} differ from the reference")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
