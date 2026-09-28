# Pre-script for the native test envs: puts CLion's bundled MinGW on PATH (no gcc
# is installed system-wide) and tells the tests where the project lives.
# Replaces helpers/version_increment.py for these envs, so tests never bump version.txt.

import glob
import os
import re
import subprocess

Import("env")

PATTERN = "C:/Program Files/JetBrains/CLion */bin/mingw/bin"


def clion_version(path):
    m = re.search(r"CLion ([0-9.]+)", path)
    return tuple(int(p) for p in m.group(1).split(".") if p) if m else ()


candidates = [d for d in glob.glob(PATTERN) if os.path.isfile(os.path.join(d, "g++.exe"))]
if not candidates:
    print("native_toolchain: no g++.exe under '%s' - install CLion or adjust %s" % (PATTERN, __file__))
    env.Exit(1)

mingw = max(candidates, key=clion_version).replace("\\", "/")
env.PrependENVPath("PATH", mingw)
# The test runner starts program.exe from this process, not from SCons.
os.environ["PATH"] = mingw + os.pathsep + os.environ.get("PATH", "")

version = subprocess.check_output([os.path.join(mingw, "g++.exe"), "--version"], text=True).splitlines()[0]
print("native_toolchain: %s/g++.exe - %s" % (mingw, version))

env.Replace(CC="gcc", CXX="g++", AR="ar", RANLIB="ranlib", LINK="g++")
# program.exe must run without MinGW's DLLs on PATH.
env.Append(LINKFLAGS=["-static"])
env.Append(CPPDEFINES=[
    ("TEST_PROJECT_DIR", env.StringifyMacro(env.subst("$PROJECT_DIR").replace("\\", "/"))),
    ("TEST_PIOENV", env.StringifyMacro(env["PIOENV"])),
])
