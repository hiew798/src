"""Create local mesh credentials once. Existing credentials are never replaced."""
from pathlib import Path
import secrets
path = Path(__file__).resolve().parents[1] / "NetworkSecrets.h"
if path.exists():
    raise SystemExit("NetworkSecrets.h already exists; kept unchanged. Use this same file on every node.")
key = ", ".join(f"0x{b:02x}" for b in secrets.token_bytes(32))
password = secrets.token_urlsafe(12)
path.write_text('#pragma once\n// Keep this SAME private file on every node.\nstatic const unsigned char MESH_KEY[32] = { ' + key + ' };\n#define RESCUER_USERNAME "rescuer"\n#define RESCUER_PASSWORD "' + password + '"\n', encoding="utf-8")
print("Created NetworkSecrets.h. Credentials were not printed. Keep a private backup.")
