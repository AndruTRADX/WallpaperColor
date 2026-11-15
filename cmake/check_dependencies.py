#!/usr/bin/env python3
"""Checks the modules of src/ (docs/ARCHITECTURE.md, "The modules"):

    python3 cmake/check_dependencies.py src

1. Every file includes only the modules its own module may use. The table below is the
   dependency graph: it has no cycle, and the lower modules know nothing of the higher ones.
2. The pure modules (colour maths, the service's logic) include no header that touches the
   operating system: they reach the world only through their ports, so their tests need none.
"""

import os
import re
import sys

COMMON = ["drivers/sdk/", "utils/"]

# A file follows the longest rule its path starts with. "OWN" is the file's own module folder
# (for a driver: drivers/<name>/).
RULES = {
    "utils/": ["utils/"],
    "drivers/sdk/": ["drivers/sdk/", "utils/error.hpp", "utils/text.hpp"],
    "colours/": ["colours/"] + COMMON,
    "settings/": ["settings/", "colours/"] + COMMON,
    "protocol/": ["protocol/", "colours/"] + COMMON,
    "pictures/": ["pictures/", "colours/", "utils/"],
    "drivers/host/": ["drivers/host/"] + COMMON,
    "drivers/": ["OWN"] + COMMON,  # a driver: itself, the SDK, utils; never a program or another driver
    # The service's logic: its ports, the shared modules, and the ports both programs share.
    "daemon/service/": ["daemon/service/", "colours/", "settings/", "protocol/", "pictures/picture_files.hpp",
                        "drivers/host/driver_host.hpp"] + COMMON,
    # The service's ports' implementations: each one only implements a port, it never uses another.
    "daemon/kde/": ["daemon/kde/", "daemon/service/ports.hpp", "settings/", "colours/"] + COMMON,
    "daemon/system/": ["OWN_HEADER", "daemon/service/ports.hpp", "settings/", "colours/"] + COMMON,
    "daemon/main.cpp": ["daemon/", "colours/", "settings/", "protocol/", "pictures/", "drivers/host/"] + COMMON,
    "terminal/app/": ["terminal/app/", "colours/", "settings/", "protocol/", "pictures/picture_files.hpp",
                      "drivers/host/driver_host.hpp"] + COMMON,
    "terminal/system/": ["OWN_HEADER", "terminal/app/ports.hpp", "settings/", "colours/"] + COMMON,
    # Showing: the use cases and the types they return; no ports' implementations, no utils.
    "terminal/cli/": ["terminal/cli/", "terminal/app/", "colours/", "settings/", "protocol/messages.hpp", "drivers/sdk/"],
    "terminal/tui/": ["terminal/tui/", "terminal/app/", "colours/", "settings/", "protocol/messages.hpp", "drivers/sdk/"],
    "terminal/main.cpp": ["terminal/", "colours/", "settings/", "protocol/", "pictures/", "drivers/host/"] + COMMON,
}

# What the pure modules may not include: anything that reaches the operating system.
PURE = ["colours/", "daemon/service/"]
SYSTEM_HEADER = re.compile(r"^(sys/|unistd\.h|fcntl\.h|dlfcn\.h|poll\.h|fstream|iostream|cstdio|sdbus-c\+\+/|"
                           r"netinet/|netdb\.h|linux/)")


def own_header(f):
    return os.path.splitext(f)[0] + ".hpp"


def main():
    root = sys.argv[1]
    files = sorted(
        os.path.relpath(os.path.join(d, f), root)
        for d, _, names in os.walk(root)
        for f in names
        if f.endswith((".hpp", ".cpp"))
    )
    problems = []
    for f in files:
        with open(os.path.join(root, f), encoding="utf-8") as text:
            source = text.read()
        rule = max((r for r in RULES if f.startswith(r)), key=len, default=None)
        if rule is None:
            problems.append(f"{f}: not in a module (see RULES in cmake/check_dependencies.py)")
            continue
        allowed = []
        for a in RULES[rule]:
            if a == "OWN":
                allowed.append("/".join(f.split("/")[:2]) + "/")
            elif a == "OWN_HEADER":
                allowed.append(own_header(f))
            else:
                allowed.append(a)
        for i in re.findall(r'^#include "([^"]+)"', source, re.M):
            if not any(i.startswith(a) for a in allowed):
                problems.append(f"{f}: includes {i} ({rule} may include: {', '.join(allowed)})")
        if any(f.startswith(p) for p in PURE):
            for i in re.findall(r"^#include <([^>]+)>", source, re.M):
                if SYSTEM_HEADER.match(i):
                    problems.append(f"{f}: includes <{i}>: {rule} is pure, it reaches the system through its ports")

    for p in problems:
        print(f"DEPENDENCIES: {p}", file=sys.stderr)
    if problems:
        return 1
    print(f"{len(files)} files: every module includes only what it may")
    return 0


if __name__ == "__main__":
    sys.exit(main())
