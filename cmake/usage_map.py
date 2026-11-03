#!/usr/bin/env python3
"""Which part of WallpaperColor uses each source file, found by following the #includes from
each program's main.cpp and each driver's module.cpp (a header pulls in its .cpp).

    python3 cmake/usage_map.py src            prints the map
    python3 cmake/usage_map.py src --check    also fails if a file is in the wrong place

Where a file must be, by who uses it (docs/ARCHITECTURE.md, "Where things go"):
    daemon/, terminal/          only that program
    drivers/<name>/             only the drivers
    drivers/sdk/                the drivers and at least one program
    drivers/host/               both programs
    shared/                     both programs
    utils/                      anyone: plain code, nothing about WallpaperColor
(the layers inside each part are checked by cmake/check_layers.py).
"""

import collections
import os
import re
import sys


APPLICATION_FOLDERS = ('contracts', 'features', 'models', 'events')


def main():
    root = sys.argv[1]
    check = "--check" in sys.argv
    files = sorted(
        os.path.relpath(os.path.join(d, f), root)
        for d, _, names in os.walk(root)
        for f in names
        if f.endswith((".hpp", ".cpp"))
    )
    includes = {}
    for f in files:
        with open(os.path.join(root, f), encoding="utf-8") as text:
            found = re.findall(r'^#include "([^"]+)"', text.read(), re.M)
        includes[f] = [i for i in found if i in files]

    # A .cpp implements the headers of its own folder that it includes: x.cpp for x.hpp, or
    # several .cpp files for one header (the commands of terminal/presentation/cli/commands.hpp).
    implemented_by = collections.defaultdict(list)
    for f in files:
        if not f.endswith(".cpp"):
            continue
        for header in includes[f]:
            if os.path.dirname(header) == os.path.dirname(f):
                implemented_by[header].append(f)

    def reached(starts):
        seen, todo = set(), list(starts)
        while todo:
            f = todo.pop()
            if f in seen:
                continue
            seen.add(f)
            todo += includes[f] + implemented_by[f]
        return seen

    parts = {
        "daemon": reached(["daemon/main.cpp"]),
        "terminal": reached(["terminal/main.cpp"]),
        "drivers": reached([f for f in files if f.startswith("drivers/") and f.endswith("/module.cpp")]),
    }
    users = {f: tuple(p for p in parts if f in parts[p]) for f in files}

    groups = collections.defaultdict(list)
    for f in files:
        groups[users[f]].append(f)
    for who, group in sorted(groups.items(), key=lambda g: (-len(g[0]), g[0])):
        print(f"used by {' + '.join(who) or 'nobody'} ({len(group)})")
        for f in group:
            print(f"    {f}")

    if not check:
        return 0
    problems = []
    for f in files:
        who = set(users[f])
        top = f.split("/")[0]
        if not who:
            problems.append(f"{f}: used by nobody")
        elif top in ("daemon", "terminal") and who != {top}:
            problems.append(f"{f}: in {top}/ but used by {', '.join(sorted(who))}")
        elif f.startswith("drivers/sdk/") and not ("drivers" in who and who & {"daemon", "terminal"}):
            problems.append(f"{f}: in drivers/sdk/ but used by {', '.join(sorted(who))}")
        elif f.startswith("drivers/host/") and not {"daemon", "terminal"} <= who:
            problems.append(f"{f}: in drivers/host/ but used by {', '.join(sorted(who))}")
        elif top == "drivers" and f.split("/")[1] not in ("sdk", "host") and who != {"drivers"}:
            problems.append(f"{f}: a driver's file used by {', '.join(sorted(who))}")
        elif top == "shared" and not {"daemon", "terminal"} <= who:
            problems.append(f"{f}: in shared/ but used only by {', '.join(sorted(who))}: move it there")
        parts_of = f.split("/")
        if parts_of[0] in ("daemon", "terminal") and parts_of[1] == "application":
            if len(parts_of) == 3:
                problems.append(f"{f}: loose in {parts_of[0]}/application/: put it in {', '.join(APPLICATION_FOLDERS)}")
            elif parts_of[2] not in APPLICATION_FOLDERS:
                problems.append(f"{f}: {parts_of[0]}/application/{parts_of[2]}/ is not an application folder "
                                f"({', '.join(APPLICATION_FOLDERS)}); what talks to the outside is infrastructure")
    for p in problems:
        print(f"WRONG PLACE: {p}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
