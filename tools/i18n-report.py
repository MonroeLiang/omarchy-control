#!/usr/bin/env python3
"""Lists every translatable string and reports per-language coverage.

Framework support for a language and translated strings are separate facts, so
this is what the docs quote rather than a claim that every language is finished.
  tools/i18n-report.py            coverage per shipped catalog
  tools/i18n-report.py --list     all source strings as JSON
"""
import glob, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")


def strings():
    ui, plural = set(), set()
    pattern = re.compile(r'\b(?:qsTr|tr)\(\s*"((?:[^"\\]|\\.)*)"(\s*,\s*""\s*,)?')
    translate = re.compile(r'(?:qsTranslate|QT_TRANSLATE_NOOP|QCoreApplication::translate)\(\s*"[A-Za-z]+"\s*,\s*"((?:[^"\\]|\\.)*)"')
    for path in glob.glob(SRC + "/*.qml") + glob.glob(SRC + "/*.cpp"):
        text = open(path, encoding="utf-8").read()
        for match in pattern.finditer(text):
            value = bytes(match.group(1), "utf-8").decode("unicode_escape").encode("latin-1", "ignore").decode("utf-8", "ignore") if "\\" in match.group(1) else match.group(1)
            (plural if match.group(2) else ui).add(value)
        for match in translate.finditer(text):
            ui.add(match.group(1))
    catalog = set()
    for page in json.load(open(SRC + "/data/pages.json", encoding="utf-8")):
        catalog.update([page["label"], page["description"], *page.get("sections", []), *page.get("jumps", [])])
        for row in page.get("panels", []):
            for column in row.get("columns", []):
                for panel in column:
                    catalog.update(filter(None, [panel.get("title"), panel.get("footnote"), (panel.get("notice") or {}).get("text")]))
    for name in ("settings", "workflows"):
        for entry in json.load(open(f"{SRC}/data/{name}.json", encoding="utf-8")):
            catalog.update(filter(None, [entry["label"], entry.get("description"), entry.get("section"), entry.get("confirmText"), entry.get("confirmAlways"), entry.get("placeholder"), entry.get("restart"),
                                         entry.get("multiLabel"), entry.get("multiDescription"), (entry.get("read") or {}).get("unsupported")]))
            catalog.update(entry.get("effects", []))
            catalog.update(c["label"] for c in entry.get("choices", []))
            catalog.update(entry.get("map", {}).values() if isinstance(entry.get("map"), dict) else [])
    return sorted(ui - plural), sorted(plural), sorted(catalog - ui - plural)


def main():
    ui, plural, catalog = strings()
    if "--list" in sys.argv:
        print(json.dumps({"ui": ui, "plural": plural, "catalog": catalog}, ensure_ascii=False, indent=1))
        return
    print(f"source strings: {len(ui)} interface, {len(plural)} counted, {len(catalog)} catalog")
    for path in sorted(glob.glob(SRC + "/i18n/*.json")):
        code = os.path.basename(path)[:-5]
        have = json.load(open(path, encoding="utf-8"))
        done_ui = sum(s in have for s in ui + plural)
        done_catalog = sum(s in have for s in catalog)
        print(f"{code:6} interface {done_ui}/{len(ui) + len(plural)}  catalog {done_catalog}/{len(catalog)}")


main()
