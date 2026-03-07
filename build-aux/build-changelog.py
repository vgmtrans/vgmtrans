#!/usr/bin/env python3
"""
Utility script to prepare changelog for the release bundle.
Input markdown should be simple-ish.
Multi-level bulleted lists are not supported,
as well as potentially more stuff
"""

from __future__ import annotations

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

SECTION_RE = re.compile(
    r"^## \[(?P<label>[^\]]+)\](?: - (?P<date>\d{4}-\d{2}-\d{2}))?\s*$"
)
SUBSECTION_RE = re.compile(r"^### (?P<title>.+?)\s*$")
REFERENCE_RE = re.compile(r"^\[([^\]]+)\]:\s+(.+?)\s*$")
BULLET_RE = re.compile(r"^[-*] (?P<text>.+)$")
INDENTED_RE = re.compile(r"^[ \t]{2,}(?P<text>.+)$")


@dataclass(slots=True)
class Section:
    label: str
    heading: str
    lines: list[str]


def parse_argv() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract the current release notes from the changelog file."
    )
    parser.add_argument(
        "--input", required=True, type=Path, help="Path to CHANGELOG.md"
    )
    parser.add_argument(
        "--current-version",
        default="Unreleased",
        help="Current version label or tag. Values containing '+' are treated as Unreleased.",
    )
    parser.add_argument(
        "--format",
        required=True,
        choices=("markdown", "appstream"),
        help="Output format.",
    )
    parser.add_argument("--output", type=Path, help="Write output to this file.")
    return parser.parse_args()


def normalize_label(label: str) -> str:
    normalized = label.strip()
    if "+" in normalized or normalized.lower() == "unreleased":
        return "unreleased"
    if normalized.startswith("v") and len(normalized) > 1 and normalized[1].isdigit():
        normalized = normalized[1:]
    return normalized.casefold()


def parse_changelog(
    path: Path,
) -> tuple[list[str], list[Section], list[tuple[str, str]]]:
    intro: list[str] = []
    sections: list[Section] = []
    references: list[tuple[str, str]] = []
    current: Section | None = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.rstrip()
        if match := SECTION_RE.match(line):
            current = Section(label=match.group("label"), heading=line, lines=[])
            sections.append(current)
            continue

        if match := REFERENCE_RE.match(line):
            references.append((match.group(1), match.group(2)))
            continue

        if current is None:
            intro.append(line)
        else:
            current.lines.append(line)

    while intro and not intro[-1]:
        intro.pop()

    return intro, sections, references


def select_section(sections: list[Section], current_version: str) -> Section:
    wanted = normalize_label(current_version)
    for section in sections:
        if normalize_label(section.label) == wanted:
            return section

    available = ", ".join(section.label for section in sections) or "<none>"
    raise ValueError(
        f"Could not find changelog section for '{current_version}'. Available: {available}"
    )


def collect_references(text: str, references: list[tuple[str, str]]) -> list[str]:
    labels = {match.group(1) for match in re.finditer(r"\[([^\]]+)\](?!\()", text)}
    return [f"[{label}]: {target}" for label, target in references if label in labels]


def render_markdown(section: Section, references: list[tuple[str, str]]) -> str:
    output_lines = [section.heading]

    section_lines = list(section.lines)
    while section_lines and not section_lines[-1]:
        section_lines.pop()

    output_lines.extend(section_lines)

    body = "\n".join(output_lines).rstrip()
    reference_lines = collect_references(body, references)
    if reference_lines:
        body = f"{body}\n\n" + "\n".join(reference_lines)
    
    full_changelog_section = (
        "## Full changelog\n"
        "- See the full version history "
        "[here](https://github.com/vgmtrans/vgmtrans/blob/master/CHANGELOG.md)."
    )
    return f"{body}\n\n{full_changelog_section}\n"


def squash_inline_markdown(text: str) -> str:
    value = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)
    value = re.sub(r"\[([^\]]+)\]\[[^\]]+\]", r"\1", value)
    value = re.sub(r"`([^`]+)`", r"\1", value)
    value = re.sub(r"\*\*([^*]+)\*\*", r"\1", value)
    value = re.sub(r"__([^_]+)__", r"\1", value)
    value = re.sub(r"\*([^*]+)\*", r"\1", value)
    value = re.sub(r"_([^_]+)_", r"\1", value)
    return value.strip()


def render_appstream(section: Section) -> str:
    # Trim empty lines surrounding the section, if any
    lines = list(section.lines)
    while lines and not lines[0]:
        lines.pop(0)
    while lines and not lines[-1]:
        lines.pop()

    if not lines:
        return ""

    paragraph_lines: list[str] = []
    list_items: list[str] = []
    index = 0
    description = ET.Element("description")

    def flush_paragraph() -> None:
        nonlocal paragraph_lines
        if not paragraph_lines:
            return
        paragraph = ET.SubElement(description, "p")
        paragraph.text = squash_inline_markdown(" ".join(paragraph_lines))
        paragraph_lines = []

    def flush_list() -> None:
        nonlocal list_items
        if not list_items:
            return
        block = ET.SubElement(description, "ul")
        for item in list_items:
            entry = ET.SubElement(block, "li")
            entry.text = squash_inline_markdown(item)
        list_items = []

    while index < len(lines):
        line = lines[index].strip()
        if not line:
            flush_paragraph()
            flush_list()
            index += 1
            continue
        if match := SUBSECTION_RE.match(line):
            flush_paragraph()
            flush_list()
            paragraph = ET.SubElement(description, "p")
            paragraph.text = match.group("title")
            index += 1
            continue

        # ~~~
        # Parse lists, respecting indentation:
        # - First line
        #   wrapped continuation
        #   more detail
        # - Next item
        # ~~~
        if match := BULLET_RE.match(line):
            flush_paragraph()
            item = [match.group("text").strip()]
            index += 1
            while index < len(lines):
                continuation = lines[index]
                if not continuation.strip():
                    # Line is blank, end of bullet
                    break

                if SUBSECTION_RE.match(continuation.strip()) or BULLET_RE.match(
                    continuation.strip()
                ):
                    # New subsection or new bullet, stop parsing this one
                    break

                # Continuation of this bullet item
                if indented := INDENTED_RE.match(continuation):
                    item.append(indented.group("text").strip())
                else:
                    item.append(continuation.strip())

                index += 1

            list_items.append(" ".join(item))
            continue

        flush_list()
        paragraph_lines.append(line)
        index += 1

    flush_paragraph()
    flush_list()

    if not list(description):
        return ""

    return ET.tostring(description, encoding="unicode", short_empty_elements=False)


def main() -> int:
    args = parse_argv()

    try:
        _, sections, references = parse_changelog(args.input)
        section = select_section(sections, args.current_version)
    except ValueError as exc:
        print(exc, file=sys.stderr)
        return 1

    if args.format == "markdown":
        output = render_markdown(section, references)
    else:
        output = render_appstream(section)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    else:
        sys.stdout.write(output)

    return 0


if __name__ == "__main__":
    sys.exit(main())
