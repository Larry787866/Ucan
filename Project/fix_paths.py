"""Rewrite Keil project file paths from the vendor SDK layout to this repo's flattened layout."""
import io
import re

B = "\\"  # single backslash literal

SUBS = [
    # C compiler include search path
    (B.join(["..", "..", "Firmware", "CMSIS"]) + ";"
     + B.join(["..", "..", "Firmware", "CMSIS", "GD", "GD32F30x", "Include"]) + ";"
     + B.join(["..", "..", "Firmware", "GD32F30x_standard_peripheral", "Include"]) + ";"
     + B.join(["..", "..", "Utilities"]) + ";.." + B,
     B.join(["..", "User"]) + ";"
     + B.join(["..", "Firmware", "Include"]) + ";"
     + B.join(["..", "Cmsis"])),

    # startup .s files live in the ARM source subdirectory -- must run before the generic CMSIS rule
    (B.join(["..", "..", "Firmware", "CMSIS", "GD", "GD32F30x", "Source", "ARM"]) + B,
     B.join(["..", "Cmsis"]) + B),

    # system_gd32f30x.c
    (B.join(["..", "..", "Firmware", "CMSIS", "GD", "GD32F30x", "Source"]) + B,
     B.join(["..", "Cmsis"]) + B),

    # standard peripheral driver sources
    (B.join(["..", "..", "Firmware", "GD32F30x_standard_peripheral", "Source"]) + B,
     B.join(["..", "Firmware", "Source"]) + B),

    # application sources
    (B.join(["..", "gd32f30x_it.c"]), B.join(["..", "User", "gd32f30x_it.c"])),
    (B.join(["..", "systick.c"]), B.join(["..", "User", "systick.c"])),
    (B.join(["..", "main.c"]), B.join(["..", "User", "main.c"])),
]

# the vendor eval-board support group: gd32f307c_eval.c is gone, main.c no longer uses it
GROUP_RE = re.compile(r"[ \t]*<Group>\s*<GroupName>Utilities</GroupName>.*?</Group>[ \t]*\n", re.S)

for name in ("Project.uvprojx", "Project.uvoptx"):
    src = io.open(name, encoding="utf-8", errors="surrogateescape").read()
    src, ngrp = GROUP_RE.subn("", src)
    print("=== %s: removed %d Utilities group(s)" % (name, ngrp))
    for old, new in SUBS:
        n = src.count(old)
        if n:
            src = src.replace(old, new)
            print("    %3d x  %s" % (n, old.replace(B, "/")))
    io.open(name, "w", encoding="utf-8", errors="surrogateescape", newline="").write(src)

print("\nremaining stale paths:")
for name in ("Project.uvprojx", "Project.uvoptx"):
    src = io.open(name, encoding="utf-8", errors="surrogateescape").read()
    hits = re.findall(r"[.\w\\]*\.\.\\\.\.[.\w\\]*", src)
    print("  %s: %s" % (name, sorted(set(hits)) or "none"))
