Import("env")
import os


# signing is driven by the -DUPDATE_SIGNING build flag. Envs that set
# it (release/deploy) require private.key and get a signed firmware.bin; envs
# that don't (dev board envs, contributors) build unsigned with no keys needed.
# The same flag also compiles the verifier into the firmware, so one switch in
# platformio.ini controls both halves.
def _signing_enabled(e):
    for d in e.get("CPPDEFINES", []):
        name = d[0] if isinstance(d, (list, tuple)) else d
        if str(name) == "UPDATE_SIGNING":
            return True
    flags = e.GetProjectOption("build_flags", [])
    if isinstance(flags, str):
        flags = flags.split()
    return any("UPDATE_SIGNING" in str(f) for f in flags)


if _signing_enabled(env):
    key = os.path.join(env["PROJECT_DIR"], "private.key")
    if not os.path.isfile(key):
        env.Exit("UPDATE_SIGNING set but private.key missing - refusing to ship unsigned")

    sign_py = os.path.join(env["PROJECT_DIR"], "sign.py")
    passin = os.environ.get("SIGN_PASSIN")

    def _sign(source, target, env):
        extra = ' --passin "%s"' % passin if passin else ""
        env.Execute('"$PYTHONEXE" "%s" --key "%s"%s --bin "$BUILD_DIR/${PROGNAME}.bin"'
                    % (sign_py, key, extra))

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _sign)
else:
    print("UPDATE_SIGNING not set - building UNSIGNED firmware (dev mode)")
