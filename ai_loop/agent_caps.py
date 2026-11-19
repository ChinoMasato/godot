#!/usr/bin/env python3
"""Agent capability profiles (BACKLOG B-404): what the calling agent can do.

Agents differ: one can generate images, another cannot; one has a GPU,
Blender or a browser, another does not. Instead of tools failing when a
capability is absent, the agent declares a profile and the tools adapt:
the catalog annotates entries with the capabilities a profile is missing
(and what to use instead), and commands like texture-route pick a route
that works with the profile they are given.

Declare a profile in either (or both) of two ways; they are merged:
  * the environment variable UZU_AGENT_CAPS, a comma-separated list:
        UZU_AGENT_CAPS=image_gen,gpu
  * a JSON profile file, named by UZU_AGENT_PROFILE or --profile:
        {"capabilities": ["image_gen", "gpu"]}
    (a bare JSON list of names is also accepted)
Commands also accept an explicit --caps list, which is merged in last.

The known capabilities are KNOWN_CAPABILITIES below. An unknown name is
an error (CapsError), never silently ignored: a typo must not quietly
turn into a missing capability and a wrong route.

Usage: ai_loop/uzu.py caps [--caps image_gen,gpu] [--profile caps.json]
Prints one JSON line {"ok": bool, ...}. Standard library only.
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# The capability vocabulary. Tools declare what they need with these
# names (see TOOL_CAPABILITIES in mcp_server.py and the CAPABILITIES
# constants in tool modules); anything else is a validation error.
KNOWN_CAPABILITIES = ("blender", "browser", "gpu", "image_gen")

ENV_CAPS = "UZU_AGENT_CAPS"
ENV_PROFILE = "UZU_AGENT_PROFILE"


class CapsError(ValueError):
    """A capability declaration that cannot be read or names an unknown capability."""


def validate_names(names, where="capabilities"):
    """The known names in `names`, or CapsError naming the unknown ones."""
    out = []
    for name in names:
        cleaned = str(name).strip().lower()
        if not cleaned:
            continue
        if cleaned not in KNOWN_CAPABILITIES:
            raise CapsError(f"unknown capability {name!r} in {where}: known capabilities are {', '.join(KNOWN_CAPABILITIES)}")
        out.append(cleaned)
    return out


def parse_caps(text, where="--caps"):
    """A comma-separated capability list ("image_gen, gpu") as a frozenset."""
    if not text:
        return frozenset()
    return frozenset(validate_names(str(text).split(","), where))


def caps_from_file(path):
    """The capabilities a JSON profile file declares, as a frozenset."""
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except OSError as e:
        raise CapsError(f"cannot read capability profile {path}: {e}")
    except ValueError as e:
        raise CapsError(f"capability profile {path} is not valid JSON: {e}")
    if isinstance(data, dict):
        names = data.get("capabilities", data.get("caps"))
        if names is None:
            raise CapsError(f"capability profile {path} has no 'capabilities' list")
    else:
        names = data
    if not isinstance(names, list):
        raise CapsError(f"capability profile {path}: 'capabilities' must be a list of names")
    return frozenset(validate_names(names, f"profile {path}"))


def load_profile(explicit_caps=None, profile_path=None, environ=None):
    """The merged capability profile: (frozenset of names, list of sources).

    Sources, all merged: the profile file named by `profile_path` or by
    $UZU_AGENT_PROFILE, the $UZU_AGENT_CAPS list, and `explicit_caps`
    (an iterable of names or a comma-separated string, e.g. from --caps).
    An empty sources list means nobody declared anything.
    """
    environ = os.environ if environ is None else environ
    caps, sources = set(), []
    path = profile_path or environ.get(ENV_PROFILE) or None
    if path:
        caps |= caps_from_file(path)
        sources.append(f"file:{path}")
    if environ.get(ENV_CAPS):
        caps |= parse_caps(environ[ENV_CAPS], f"${ENV_CAPS}")
        sources.append(f"env:{ENV_CAPS}")
    if explicit_caps:
        if isinstance(explicit_caps, str):
            caps |= parse_caps(explicit_caps)
        else:
            caps |= validate_names(list(explicit_caps), "--caps")
        sources.append("argument")
    return frozenset(caps), sources


def missing_capabilities(required, profile):
    """The sorted names in `required` that `profile` does not declare."""
    return sorted(set(required) - set(profile))


def main(argv):
    caps_arg, profile_arg = None, None
    i = 0
    while i < len(argv):
        if argv[i] in ("--caps", "--profile") and i + 1 < len(argv):
            if argv[i] == "--caps":
                caps_arg = argv[i + 1]
            else:
                profile_arg = argv[i + 1]
            i += 2
        else:
            print(json.dumps({"ok": False, "error": f"caps: unexpected argument {argv[i]!r} (use --caps a,b or --profile file.json)"}))
            return 2
    try:
        caps, sources = load_profile(caps_arg, profile_arg)
    except CapsError as e:
        print(json.dumps({"ok": False, "error": str(e)}, ensure_ascii=False))
        return 2
    print(json.dumps({"ok": True, "command": "caps", "capabilities": sorted(caps), "known_capabilities": list(KNOWN_CAPABILITIES), "sources": sources}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
