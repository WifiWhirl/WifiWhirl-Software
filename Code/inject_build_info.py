Import("env")

# Expose the active PlatformIO environment name (e.g. "d1_mini",
# "wifiwhirl_2_2_0") to the firmware as the PIO_ENV_NAME macro, so the support
# package and the Info page can report which build/target produced the binary.
flag = ("PIO_ENV_NAME", env.StringifyMacro(env["PIOENV"]))

# Project src/ files compile with a separate, already-cloned environment, so a
# global env append doesn't reach them - update projenv too.
env.Append(CPPDEFINES=[flag])
Import("projenv")
projenv.Append(CPPDEFINES=[flag])

# Build stamp for testers: which commit a test firmware was built
# from, shown under Softwareinfo ("2.1.0 (2fcfb5b, 12.11.2026)"). Written to a
# git-ignored header that only build_ver.h includes, and only rewritten when it
# changes, so a new commit recompiles three files instead of the whole firmware.
import os
import subprocess

def _git(*args):
    try:
        return subprocess.check_output(["git", *args], cwd=env["PROJECT_DIR"],
                                       stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return ""

_commit = _git("rev-parse", "--short=7", "HEAD")
if _commit:
    # "+" = built with uncommitted changes in the firmware sources
    if subprocess.call(["git", "diff", "--quiet", "HEAD", "--", "src", "lib", "frontend/src"],
                       cwd=env["PROJECT_DIR"], stderr=subprocess.DEVNULL) != 0:
        _commit += "+"
    _date = _git("log", "-1", "--format=%cd", "--date=format:%d.%m.%Y")
    _label = _commit + (", " + _date if _date else "")
else:
    _label = ""
# BUILD_TAG=... names a test variant in the stamp, e.g. "1000 Hz"
if os.environ.get("BUILD_TAG"):
    _label = (_label + ", " if _label else "") + os.environ["BUILD_TAG"]
_header = os.path.join(env["PROJECT_DIR"], "src", "build_info.h")
_text = '#pragma once\n#define BUILD_LABEL "%s"\n' % _label
try:
    _old = open(_header).read()
except OSError:
    _old = None
if _old != _text:
    with open(_header, "w") as f:
        f.write(_text)
