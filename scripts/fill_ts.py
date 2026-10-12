#!/usr/bin/env python3
"""Fill i18n/jot_<lang>.ts from i18n/<lang>.json (source text → translation;
plural strings map to a list of numerus forms). Reports untranslated texts."""
import json, sys
import xml.etree.ElementTree as ET

ts_path, json_path = sys.argv[1], sys.argv[2]
table = json.load(open(json_path, encoding="utf-8"))
tree = ET.parse(ts_path)
missing = []
for msg in tree.iter("message"):
    src = msg.find("source").text or ""
    tr = msg.find("translation")
    for child in list(tr):
        tr.remove(child)
    tr.text = None
    tr.attrib.pop("type", None)
    value = table.get(src)
    if value is None:
        missing.append(src)
        tr.set("type", "unfinished")
        continue
    if msg.get("numerus") == "yes":
        forms = value if isinstance(value, list) else [value] * 3
        for f in forms:
            ET.SubElement(tr, "numerusform").text = f
    else:
        tr.text = value if isinstance(value, str) else value[-1]
tree.write(ts_path, encoding="utf-8", xml_declaration=True)
for m in sorted(set(missing)):
    print("untranslated:", repr(m))
print(f"{len(set(missing))} untranslated")
