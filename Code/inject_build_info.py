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
