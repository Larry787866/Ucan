"""Drop the unused vendor eval-board (Utilities) group from the Keil project files.

gd32f307c_eval.c is no longer called by anything -- the application now uses the
Config/ BSP -- and it defines a second global fputc, which collides with the one
in Config/Src/bsp_usart1.c. The files stay on disk; only the project entries go.
"""
import io
import os
import re

B = "\\"
GROUP_RE = re.compile(r"[ \t]*<Group>\s*<GroupName>Utilities</GroupName>.*?</Group>[ \t]*\r?\n", re.S)
OLD_INC = B.join(["..", "User"]) + ";" + B.join(["..", "Utilities"]) + ";" + B.join(["..", "Firmware", "Include"]) + ";" + B.join(["..", "Cmsis"])
NEW_INC = B.join(["..", "User"]) + ";" + B.join(["..", "Firmware", "Include"]) + ";" + B.join(["..", "Cmsis"])


def read(p):
    return io.open(p, encoding="utf-8", errors="surrogateescape").read()


def write(p, s):
    io.open(p, "w", encoding="utf-8", errors="surrogateescape", newline="").write(s)


for name in ("Project.uvprojx", "Project.uvoptx"):
    src = read(name)
    src, ngrp = GROUP_RE.subn("", src)
    ninc = src.count(OLD_INC)
    src = src.replace(OLD_INC, NEW_INC)
    write(name, src)
    print("=== %s: removed %d Utilities group(s), %d include path(s) updated" % (name, ngrp, ninc))

print("\nremaining references to the eval library:")
found = False
for name in ("Project.uvprojx", "Project.uvoptx"):
    src = read(name)
    refs = [r for r in re.findall(r"<(?:FilePath|PathWithFileName)>([^<]+)<", src) if "eval" in r or "Utilities" in r]
    miss = sorted({r for r in re.findall(r"<(?:FilePath|PathWithFileName)>([^<]+)<", src) if not os.path.exists(r.replace(B, "/"))})
    print("  %s: eval refs: %s | missing files: %s" % (name, refs or "none", [m.replace(B, "/") for m in miss] or "none"))
    found = found or bool(refs)
