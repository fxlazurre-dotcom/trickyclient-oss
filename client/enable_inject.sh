#!/bin/bash
# Point bedrock-on-linux's injector at TrickyClient-OSS.dll (backs up settings first)
set -e
S=/home/florixx/.local/share/bedrock-on-linux/settings.json
DLL=/tmp/opencode/ModCore/TrickyClient-OSS.dll
mkdir -p /home/florixx/mc-modloader/runtime
cp -n "$S" "$S.bak-modcore" 2>/dev/null || true
cp "$DLL" /home/florixx/mc-modloader/runtime/TrickyClient-OSS.dll
python3 - "$S" <<'EOF'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["injector_dll"] = "/home/florixx/mc-modloader/runtime/TrickyClient-OSS.dll"
d["injector_auto_enable"] = True
json.dump(d, open(p, "w"), indent=2)
print("injector_dll ->", d["injector_dll"], "| auto:", d["injector_auto_enable"])
EOF
echo "OK: запусти игру через BedrockOnLinux, в меню нажми INSERT"
echo "лог: tail -f /home/florixx/mc-modloader/runtime/mcmod.log"
