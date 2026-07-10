from pathlib import Path
from zipfile import ZipFile
import sys

from docx import Document
from docx.oxml.ns import qn


def texts(element):
    result = []
    for node in element.iter():
        if node.tag in {qn("w:t"), qn("w:delText"), qn("w:instrText")} and node.text:
            result.append(node.text)
    return "".join(result).strip()


def paragraph_style(element):
    ppr = element.find(qn("w:pPr"))
    if ppr is None:
        return ""
    style = ppr.find(qn("w:pStyle"))
    return "" if style is None else style.get(qn("w:val"), "")


def extract(path, output):
    doc = Document(path)
    lines = [f"# SOURCE: {path}", "", "## MAIN BODY"]
    paragraph_no = 0
    table_no = 0
    for child in doc.element.body.iterchildren():
        if child.tag == qn("w:p"):
            paragraph_no += 1
            text = texts(child)
            if text:
                style = paragraph_style(child)
                lines.append(f"P{paragraph_no} [{style or 'Normal'}] {text}")
        elif child.tag == qn("w:tbl"):
            table_no += 1
            lines.append(f"\n### TABLE {table_no}")
            for row_no, row in enumerate(child.findall(qn("w:tr")), 1):
                cells = [texts(cell) for cell in row.findall(qn("w:tc"))]
                lines.append(f"R{row_no}: " + " | ".join(cells))

    lines.append("\n## HEADERS AND FOOTERS")
    for section_no, section in enumerate(doc.sections, 1):
        for label, part in (("HEADER", section.header), ("FOOTER", section.footer)):
            for p_no, paragraph in enumerate(part.paragraphs, 1):
                if paragraph.text.strip():
                    lines.append(
                        f"SECTION {section_no} {label} P{p_no}: {paragraph.text.strip()}"
                    )

    with ZipFile(path) as archive:
        lines.append("\n## COMMENTS, FOOTNOTES, ENDNOTES AND OTHER TEXT PARTS")
        for name in sorted(archive.namelist()):
            if not name.startswith("word/") or not name.endswith(".xml"):
                continue
            if name in {"word/document.xml", "word/styles.xml", "word/numbering.xml"}:
                continue
            data = archive.read(name)
            try:
                from lxml import etree

                root = etree.fromstring(data)
                value = texts(root)
            except Exception:
                value = ""
            if value:
                lines.append(f"PART {name}: {value}")

        media = [name for name in archive.namelist() if name.startswith("word/media/")]
        lines.append("\n## EMBEDDED MEDIA")
        for name in media:
            info = archive.getinfo(name)
            lines.append(f"{name} ({info.file_size} bytes)")

    Path(output).write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    extract(sys.argv[1], sys.argv[2])
