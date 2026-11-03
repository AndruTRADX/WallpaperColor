#!/usr/bin/env python3
"""Checks the layers of src/ (docs/ARCHITECTURE.md, "The layers" and "Who may include what").

    python3 cmake/check_layers.py src

1. Every file includes only what its layer may (the longest matching folder of RULES decides).
2. A contract holds only interfaces: no function bodies, no data structures (those are models).
3. A service never uses another service: a file in infrastructure/services/ includes no other
   service, and an adapter in infrastructure/external/ wraps exactly one service of another
   module.
4. application/ holds only contracts/ (flat, plus external/), features/<feature>/, events/
   and models/; infrastructure/ only services/ and external/. The same for each driver, whose
   only other file is module.cpp.
"""

import os
import re
import sys

COMMON = ["shared/domain/", "shared/models/", "drivers/sdk/", "utils/"]


def layer_rules(p, common, presentation_extra, main):
    """The rules of the layers under `p`: a program (daemon/, terminal/) or a driver."""
    a = f"{p}/application/"
    return {
        f"{p}/domain/": [f"{p}/domain/"] + common,
        f"{a}contracts/": [f"{a}contracts/", f"{a}models/", f"{p}/domain/"] + common,
        f"{a}models/": [f"{a}models/", f"{a}contracts/", f"{p}/domain/"] + common,
        f"{a}features/": [f"{a}features/", f"{a}contracts/", f"{a}models/", f"{p}/domain/"] + common,
        f"{a}events/": [f"{a}events/", f"{a}contracts/", f"{a}models/", f"{p}/domain/"] + common,
        f"{p}/infrastructure/services/": ["OWN_HEADER", f"{a}contracts/", f"{a}models/", f"{p}/domain/"] + common,
        f"{p}/infrastructure/external/": ["OWN_HEADER", f"{a}contracts/external/", f"{a}models/",
                                          "shared/infrastructure/services/", "drivers/host/services/"] + common,
        # How features are shown, and running them: features and what they return. No services,
        # no contracts, no utils.
        f"{p}/presentation/": [f"{p}/presentation/", f"{a}features/", f"{a}models/"] + presentation_extra,
        main: [f"{p}/"] + common + (["shared/", "drivers/host/"] if not p.startswith("drivers/") else []),
    }


def program_rules(p):
    # A program's presentation may also use its events (the service loop receives their
    # notifications) and the shared entities' names and types, to show them.
    return layer_rules(p, COMMON, [f"{p}/application/events/", "shared/domain/", "shared/models/"],
                       f"{p}/main.cpp")


def driver_rules(root):
    """Each driver's layers: its own folder, the sdk, utils; its presentation implements the
    sdk's Driver."""
    rules = {}
    drivers = os.path.join(root, "drivers")
    for name in sorted(os.listdir(drivers)) if os.path.isdir(drivers) else []:
        if name in ("sdk", "host") or not os.path.isdir(os.path.join(drivers, name)):
            continue
        rules.update(layer_rules(f"drivers/{name}", ["drivers/sdk/", "utils/"], ["drivers/sdk/"],
                                 f"drivers/{name}/module.cpp"))
    return rules


RULES = {
    "utils/": ["utils/"],
    "shared/domain/": ["shared/domain/", "drivers/sdk/", "utils/"],
    "shared/models/": ["shared/models/", "shared/domain/", "drivers/sdk/", "utils/"],
    "shared/infrastructure/services/": ["OWN_HEADER", "shared/domain/", "shared/models/", "drivers/sdk/", "utils/"],
    "drivers/sdk/": ["drivers/sdk/", "shared/domain/colours/color.hpp", "utils/"],
    "drivers/host/services/": ["OWN_HEADER", "drivers/sdk/", "utils/"],
    # A driver's files outside its layers (only module.cpp belongs there).
    "drivers/": ["OWN", "drivers/sdk/", "utils/"],
    **program_rules("daemon"),
    **program_rules("terminal"),
}

LAYER_FOLDERS = ("domain", "application", "infrastructure", "presentation")
APPLICATION_FOLDERS = ("contracts", "features", "events", "models")
INFRASTRUCTURE_FOLDERS = ("services", "external")
SERVICE_FOLDERS = ("infrastructure/services/", "drivers/host/services/")


def own_header(f):
    return os.path.splitext(f)[0] + ".hpp"


def is_service(path):
    return any(part in path for part in SERVICE_FOLDERS)


def main():
    root = sys.argv[1]
    files = sorted(
        os.path.relpath(os.path.join(d, f), root)
        for d, _, names in os.walk(root)
        for f in names
        if f.endswith((".hpp", ".cpp"))
    )
    rules = {**RULES, **driver_rules(root)}
    problems = []
    for f in files:
        with open(os.path.join(root, f), encoding="utf-8") as text:
            source = text.read()
        includes = re.findall(r'^#include "([^"]+)"', source, re.M)

        # 4. folders (first: a file in a wrong folder has no rule either)
        parts = f.split("/")
        layered = parts[0] in ("daemon", "terminal")
        if parts[0] == "drivers" and parts[1] not in ("sdk", "host"):
            layered = True
            parts = parts[1:]  # a driver has the same layers as a program
            if len(parts) == 2 and parts[1] not in ("module.cpp",):
                problems.append(f"{f}: a driver holds only its layers and module.cpp")
        if layered and len(parts) > 2:
            if parts[1] not in LAYER_FOLDERS:
                problems.append(f"{f}: {parts[1]}/ is not a layer ({', '.join(LAYER_FOLDERS)})")
            elif parts[1] == "application":
                if len(parts) == 3 or parts[2] not in APPLICATION_FOLDERS:
                    problems.append(f"{f}: application/ holds only {', '.join(APPLICATION_FOLDERS)}")
                elif parts[2] == "contracts" and not (len(parts) == 4 or (len(parts) == 5 and parts[3] == "external")):
                    problems.append(f"{f}: contracts are flat (plus contracts/external/)")
                elif parts[2] == "features" and (len(parts) != 5 or not parts[4].startswith(parts[3] + ".")):
                    problems.append(f"{f}: a feature is features/<feature>/<feature>.{{hpp,cpp}}")
            elif parts[1] == "infrastructure" and parts[2] not in INFRASTRUCTURE_FOLDERS:
                problems.append(f"{f}: infrastructure/ holds only {', '.join(INFRASTRUCTURE_FOLDERS)}")


        # 1. includes
        best = max((w for w in rules if f.startswith(w)), key=len, default=None)
        if best is None:
            problems.append(f"{f}: in a folder with no rule")
            continue
        allowed = []
        for a in rules[best]:
            if a == "OWN":
                allowed.append("/".join(f.split("/")[:2]) + "/")
            elif a == "OWN_HEADER":
                allowed.append(own_header(f))
            else:
                allowed.append(a)
        for i in includes:
            if not any(i.startswith(a) for a in allowed):
                problems.append(f"{f}: includes {i} ({best} may include: {', '.join(allowed)})")

        # 3. a service never uses another service
        if is_service(f):
            for i in includes:
                if is_service(i) and i != own_header(f):
                    problems.append(f"{f}: a service using another service ({i})")
        if "/infrastructure/external/" in f:
            wrapped = [i for i in includes if is_service(i)]
            if f.endswith(".hpp") and len(wrapped) != 1:
                problems.append(f"{f}: an adapter wraps exactly one service, not {len(wrapped)}")

        # 2. contracts: only interfaces
        if "/application/contracts/" in f:
            if f.endswith(".cpp"):
                problems.append(f"{f}: a contract has no code")
            for n, line in enumerate(source.splitlines(), 1):
                code = line.split("//")[0]
                if re.match(r"\s*(struct|enum)\b", code):
                    problems.append(f"{f}:{n}: a data structure in a contract (it is a model)")
                elif "{" in code and not re.match(r"\s*(class|namespace)\b", code) and "= 0" not in code:
                    problems.append(f"{f}:{n}: code in a contract (only interfaces)")

    for p in problems:
        print(f"LAYERS: {p}", file=sys.stderr)
    if problems:
        return 1
    print(f"{len(files)} files: every layer holds and includes only what it may")
    return 0


if __name__ == "__main__":
    sys.exit(main())
