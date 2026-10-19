"""Package the current manuscript, results and editable figures with portable links."""
from pathlib import Path
import re
import zipfile

root = Path(__file__).resolve().parents[2]
research = root / "research"
paper = research / "Methodology-IEEE-HD-draft.md"
text = paper.read_text(encoding="utf-8")
links = re.findall(r"!\[[^\]]*\]\(([^)]+)\)", text)
if not links:
    raise ValueError("No manuscript figures found.")
figures = []
for link in links:
    path = Path(link)
    if not path.is_absolute():
        path = paper.parent / path
    path = path.resolve()
    path.relative_to(research.resolve())
    if not path.is_file() or not path.with_suffix(".svg").is_file():
        raise FileNotFoundError(f"Figure PNG/SVG pair missing: {path.name}")
    figures.append(path)
    text = text.replace("(" + link + ")", "(figures/" + path.name + ")")
references = re.findall(r"^\[(\d+)\] ", text, re.M)
equations = re.findall(r"\\tag\{(\d+)\}", text)
for name, values in (("references", references), ("equations", equations)):
    if not values or values != [str(i) for i in range(1, len(values) + 1)]:
        raise ValueError(f"Check sequential manuscript {name} numbering.")
output = research / "LoRa-Paper-Methodology-and-Figures.zip"
with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr("Methodology.md", text)
    archive.write(research / "Methodology-IEEE-references.bib", "References.bib")
    archive.write(research / "Reported-image-results-2026-10-18.json", "Results-data.json")
    archive.write(research / "Obstruction-results-2026-10-19.json", "Obstruction-results-2026-10-19.json")
    archive.write(research / "Methodology-drafting-notes.md", "Drafting-notes.md")
    archive.write(research / "Location-observation-2026-10-19.json", "Location-observation.json")
    archive.write(research / "location-literature-metadata-2026-10-19.json", "Location-references-metadata.json")
    for path in figures:
        archive.write(path, "figures/" + path.name)
        archive.write(path.with_suffix(".svg"), "figures/" + path.with_suffix(".svg").name)
print(f"Packaged manuscript, {len(references)} references, results, notes and {len(figures)} PNG/SVG figure pairs: {output.name}")
