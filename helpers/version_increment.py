# This will bump version on each build of the firmware
# and expose it to the compiler, so there is proof of version in the binary.
# The proof is printed to the serial at boot, so it's easy to tell if OTA worked.

import os
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
proj = env['PROJECT_DIR']
vf = os.path.join(proj, 'version.txt')

# read, bump patch
with open(vf, encoding='utf-8-sig') as f:
    major, minor, patch = map(int, f.read().strip().split('.'))
patch += 1
new = f"{major}.{minor}.{patch}"

# write back
with open(vf, 'w', encoding='utf-8') as f:
    f.write(new)

# expose to compiler
env.Append(
    BUILD_FLAGS=[f'-DFW_VERSION=\\"{new}\\"']
)
