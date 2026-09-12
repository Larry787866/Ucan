"""Re-add the vendor eval-board (Utilities) group to the Keil project files and
put ..\\Utilities back on the C include search path."""
import io
import re

B = "\\"
UTIL_GROUP = B.join(["..", "Utilities"])
OLD_GROUP = B.join(["..", "..", "Utilities"])

OLD_INC = B.join(["..", "User"]) + ";" + B.join(["..", "Firmware", "Include"]) + ";" + B.join(["..", "Cmsis"])
NEW_INC = B.join(["..", "User"]) + ";" + UTIL_GROUP + ";" + B.join(["..", "Firmware", "Include"]) + ";" + B.join(["..", "Cmsis"])


def read(p):
    return io.open(p, encoding="utf-8", errors="surrogateescape").read()


def write(p, s):
    io.open(p, "w", encoding="utf-8", errors="surrogateescape", newline="").write(s)


def extract_group(text, indent):
    """Pull the original Utilities <Group> block out of a backup file."""
    pat = re.compile(r"[ \t]*<Group>\s*<GroupName>Utilities</GroupName>.*?</Group>[ \t]*\r?\n", re.S)
    m = pat.search(text)
    assert m, "Utilities group not found"
    block = m.group(0).replace(OLD_GROUP, UTIL_GROUP)  # repoint at the new layout
    if indent and not block.startswith(indent):
        block = indent + block.lstrip(" \t")
    return block


for name, indent in (("Project.uvprojx", "        "), ("Project.uvoptx", "  ")):
    src = read(name)
    # guard against running twice
    if "<GroupName>Utilities</GroupName>" in src:
        print("=== %s: Utilities group already present, skipping" % name)
    else:
        block = extract_group(read(name + ".bak"), indent)
        # insert directly before the Doc group, where it used to sit
        doc = re.compile(r"[ \t]*<Group>(?=\s*<GroupName>Doc</GroupName>)")
        m = doc.search(src)
        assert m, "Doc group not found in " + name
        src = src[:m.start()] + block + src[m.start():]
        print("=== %s: restored Utilities group" % name)

    n = src.count(OLD_INC)
    src = src.replace(OLD_INC, NEW_INC)
    print("    %d include path(s) updated" % n)
    write(name, src)

print("\nfinal file-reference check:")
import os
for name in ("Project.uvprojx", "Project.uvoptx"):
    src = read(name)
    if name.endswith(".uvprojx"):
        refs = re.findall(r"<FilePath>([^<]+)</FilePath>", src)
    else:
        refs = re.findall(r"<PathWithFileName>([^<]+)</PathWithFileName>", src)
    missing = sorted({r for r in refs if not os.path.exists(r.replace(B, "/"))})
    print("  %s: %d refs, missing: %s" % (name, len(refs), [m.replace(B, "/") for m in missing] or "none"))
