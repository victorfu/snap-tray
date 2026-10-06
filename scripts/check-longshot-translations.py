#!/usr/bin/env python3
"""Check all longshot UI translations and positional argument parity."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
controller = root / "src/qml/LongshotController.cpp"
required = {"LongshotController": set(re.findall(r'tr\("([^"\n]*)"\)', controller.read_text(encoding="utf-8")))}
for context in ("RecordingPreview", "LongshotWorkspace"):
    qml = root / "src/qml/recording" / f"{context}.qml"
    required[context] = set(re.findall(r'qsTr\("([^"\n]*)"\)', qml.read_text(encoding="utf-8")))
failures = []
files = sorted((root / "translations").glob("snaptray_*.ts"))
for path in files:
    document = ET.parse(path)
    contexts = {c.findtext("name"): c for c in document.findall("context")}
    for context, sources in required.items():
        messages = {m.findtext("source"): m.find("translation")
                    for m in contexts.get(context, []) if m.tag == "message"}
        for source in sorted(sources):
            translation = messages.get(source)
            text = "" if translation is None else "".join(translation.itertext())
            if (translation is None or translation.get("type") or not text
                    or sorted(re.findall(r"%\d+", source)) != sorted(re.findall(r"%\d+", text))):
                failures.append(f"{path.name}: {context}: {source}")
if failures:
    raise SystemExit("\n".join(failures))
print(f"Longshot translations verified in {len(files)} catalogs")
