#!/usr/bin/env bash
# Self-test for ai_loop/engine.sh: runs every command against a throwaway project
# and checks the JSON results and the saved scene. Exit 0 when all checks pass.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
E="$HERE/engine.sh"
P="$(mktemp -d)"
trap 'rm -rf "$P"' EXIT
printf 'config_version=5\n\n[application]\n\nconfig/name="engine selftest"\nrun/main_scene="res://main.tscn"\n' >"$P/project.godot"
cat >"$P/hello.gd" <<'GD'
extends Node3D
signal poked
func greet(who: String) -> String:
	return "hello " + who
func on_poked() -> void:
	pass
func _ready() -> void:
	push_error("selftest error from _ready")
GD
printf 'extends Node\nfunc broken(:\n' >"$P/broken.gd"

FAIL=0
expect() { # expect <description> <python expression on r> <json>
  if python3 -I -c "import json,sys; r=json.loads(sys.argv[1]); sys.exit(0 if ($2) else 1)" "$3"; then
    echo "ok   $1"
  else
    echo "FAIL $1: $3"; FAIL=1
  fi
}
S="--scene res://main.tscn"
expect "new"        'r["ok"]' "$("$E" "$P" new $S --type Node3D --name Main)"
expect "add mesh"   'r["ok"] and r["node"]["type"] == "MeshInstance3D"' "$("$E" "$P" add $S --parent . --type MeshInstance3D --name Box --set '{"mesh": "BoxMesh", "position": "Vector3(0, 1, 0)"}')"
expect "get"        'r["value"] == "Vector3(0, 1, 0)"' "$("$E" "$P" get $S --node Box --property position)"
expect "set"        'r["ok"] and r["value"] == "Vector3(2, 0, -1)"' "$("$E" "$P" set $S --node Box --property position --value 'Vector3(2, 0, -1)')"
expect "set indexed" 'r["ok"] and r["value"] == 3.0' "$("$E" "$P" set $S --node Box --property position:y --value 3)"
expect "add group"  'r["ok"]' "$("$E" "$P" add $S --parent . --type Node3D --name Group)"
expect "reparent"   'r["ok"] and r["node"]["path"] == "Group/Box"' "$("$E" "$P" reparent $S --node Box --parent Group)"
expect "tree"       'r["ok"] and [n["path"] for n in r["nodes"]] == [".", "Group", "Group/Box"]' "$("$E" "$P" tree $S)"
expect "props"      'r["ok"] and r["properties"]["mesh"] == {"class": "BoxMesh", "embedded": True} and r["properties"]["position"] == "Vector3(2, 3, -1)"' "$("$E" "$P" props $S --node Group/Box)"
expect "script"     'r["ok"] and r["node"]["script"] == "res://hello.gd"' "$("$E" "$P" script $S --node . --path res://hello.gd)"
expect "call"       'r["ok"] and r["result"] == "hello agent"' "$("$E" "$P" call $S --node . --method greet --args '["agent"]')"
expect "connect"    'r["ok"]' "$("$E" "$P" connect $S --from . --signal poked --to . --method on_poked)"
grep -q 'connection signal="poked"' "$P/main.tscn" && echo "ok   connection saved" || { echo "FAIL connection not saved"; FAIL=1; }
expect "classes"    'r["ok"] and "OmniLight3D" in r["classes"] and "Node2D" not in r["classes"]' "$("$E" "$P" classes --inherits Light3D)"
expect "describe"   'r["ok"] and any(p["name"] == "omni_range" for p in r["properties"])' "$("$E" "$P" describe --class OmniLight3D)"
expect "check good" 'r["ok"]' "$("$E" "$P" check --path res://hello.gd)"
expect "check bad"  'not r["ok"] and len(r["errors"]) > 0 and all(k in r["errors"][0] for k in ("file", "line", "column", "code", "message")) and r["errors"][0]["line"] > 0' "$("$E" "$P" check --path res://broken.gd || true)"
expect "run"        'r["ok"] and any("selftest error from _ready" in e["message"] and e["code"].startswith("E") and e["file"] for e in r["errors"])' "$("$E" "$P" run --frames 5)"
# validate (B-203): the broken script and a missing file are found; fixed, nothing is left.
printf '[gd_resource type="Resource" load_steps=2 format=3]\n\n[ext_resource type="Texture2D" path="res://missing.png" id="1"]\n\n[resource]\nmetadata/tex = ExtResource("1")\n' >"$P/ref.tres"
expect "validate broken" 'not r["ok"] and {("script_error", "res://broken.gd"), ("missing_dependency", "res://ref.tres")} <= {(i["kind"], i["file"]) for i in r["issues"]} and any(e["line"] == 2 for i in r["issues"] for e in i.get("errors", []))' "$("$E" "$P" validate || true)"
rm "$P/broken.gd" "$P/ref.tres"
expect "validate clean" 'r["ok"] and r["issues"] == [] and r["checked"] >= 2' "$("$E" "$P" validate)"
expect "remove"     'r["ok"] and r["removed"] == "Group/Box"' "$("$E" "$P" remove $S --node Group/Box)"
expect "bad node"   'not r["ok"]' "$("$E" "$P" get $S --node Nope --property position || true)"
expect "bad scene"  'not r["ok"]' "$("$E" "$P" tree --scene res://nope.tscn || true)"
[ $FAIL -eq 0 ] && echo "engine selftest: ok" || { echo "engine selftest: FAILED"; exit 1; }
