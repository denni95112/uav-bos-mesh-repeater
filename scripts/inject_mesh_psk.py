# PlatformIO pre-script. Injects MESH_PSK_B64 from the environment or a gitignored .env file.
# The value is only the initial key for devices without a key in NVS. It ends up readable in the
# binary, so public release builds should be made without it.

Import("env")

import base64
import os
import sys
from pathlib import Path


def fail(message):
    print(message, file=sys.stderr)
    env.Exit(1)


def load_psk():
    value = os.environ.get("MESH_PSK_B64", "").strip()
    if value:
        return value

    env_file = Path(env.subst("$PROJECT_DIR")) / ".env"
    if not env_file.is_file():
        return ""

    for line in env_file.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, raw = line.partition("=")
        if key.strip() == "MESH_PSK_B64":
            return raw.strip().strip('"').strip("'")
    return ""


psk = load_psk()
header_dir = Path(env.subst("$BUILD_DIR"))
header_dir.mkdir(parents=True, exist_ok=True)
header = header_dir / "mesh_psk.h"

if psk:
    try:
        raw = base64.b64decode(psk, validate=True)
    except Exception:
        fail("MESH_PSK_B64 is not valid base64")
    if len(raw) not in (16, 32):
        fail("MESH_PSK_B64 must decode to 16 or 32 bytes, got %d" % len(raw))
    escaped = psk.replace("\\", "\\\\").replace('"', '\\"')
    header.write_text('#pragma once\n#define MESH_PSK_B64 "%s"\n' % escaped, encoding="utf-8")
    print("Mesh key loaded (%d bytes)" % len(raw))
else:
    header.write_text("#pragma once\n", encoding="utf-8")
    print("MESH_PSK_B64 not set; devices use the key stored on them (set on the settings page)")

env.Append(CCFLAGS=["-include", header.as_posix()])
