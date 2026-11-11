#!/usr/bin/env python3
"""Read lawfully obtained external manual pages; no download or installation."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


MANUAL_DIR = Path(__file__).resolve().parent.parent / "references" / "manuals"


def page_range(value, maximum, limit):
    match = re.fullmatch(r"([1-9][0-9]*)(?:-([1-9][0-9]*))?", value)
    if not match:
        raise ValueError("pages must be a positive page or contiguous range, e.g. 4-6")
    first = int(match.group(1))
    last = int(match.group(2) or first)
    if first > last or last > maximum:
        raise ValueError(f"page range must be ordered and within 1-{maximum}")
    if last - first + 1 > limit:
        raise ValueError(f"request exceeds --max-pages {limit}; narrow the range or raise the limit")
    return first, last


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manual", nargs="?", help="manifest ID, e.g. UG300E or SUG550")
    parser.add_argument("--list", action="store_true", help="list manuals without reading PDFs")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--pages", help="printed body page(s), e.g. 4 or 4-6")
    group.add_argument("--pdf-pages", help="1-based PDF page(s), including cover/contents")
    parser.add_argument("--max-pages", type=int, default=8, help="maximum pages per call (default: 8)")
    parser.add_argument("--verify", action="store_true", help="verify PDF and selected Markdown SHA-256")
    parser.add_argument("--pdf-text", action="store_true", help="extract original PDF text instead of reading Markdown")
    args = parser.parse_args()
    try:
        manuals = json.loads((MANUAL_DIR / "manifest.json").read_text(encoding="utf-8"))["manuals"]
        if args.list:
            if args.manual or args.pages or args.pdf_pages or args.verify or args.pdf_text:
                raise ValueError("--list cannot be combined with a manual or page/verify options")
            for key, item in manuals.items():
                print(f"{key}: {item['title']} | {item['version']} | {item['date']} | "
                      f"body pages 1-{item['last_body_page']} | PDF pages 1-{item['pdf_pages']}")
            return 0
        if not args.manual or not (args.pages or args.pdf_pages):
            raise ValueError("choose a manual and --pages or --pdf-pages; use --list to inspect IDs")
        key = args.manual.upper().removesuffix(".PDF")
        if key not in manuals:
            raise ValueError(f"unknown manual {args.manual!r}; use --list")
        if args.max_pages < 1:
            raise ValueError("--max-pages must be positive")
        item = manuals[key]
        configured = os.environ.get("GOWIN_MANUAL_DIR")
        if not configured:
            raise ValueError("manual bodies are not bundled; set GOWIN_MANUAL_DIR to authorized local documents; source: " + item["official_url"])
        external = Path(configured).expanduser().resolve()
        candidates = [external / item["file"], external / (key + ".pdf")]
        pdf = next((path for path in candidates if path.is_file()), candidates[0])
        offset = item["first_body_pdf_page"] - 1
        if args.pages:
            first, last = page_range(args.pages, item["last_body_page"], args.max_pages)
            first += offset
            last += offset
        else:
            first, last = page_range(args.pdf_pages, item["pdf_pages"], args.max_pages)
        if not pdf.is_file():
            raise ValueError(f"external PDF is missing: {pdf}; expected edition {item['version']}; source: {item['official_url']}")
        if args.verify and hashlib.sha256(pdf.read_bytes()).hexdigest() != item["sha256"]:
            raise ValueError(f"SHA-256 mismatch: {pdf}; restore the matching original before citing it")
        markdown_pages = None
        conversion = None
        conversion_file = MANUAL_DIR / "conversion.json"
        if not args.pdf_text and conversion_file.is_file():
            conversion = json.loads(conversion_file.read_text(encoding="utf-8"))["manuals"].get(key)
        markdown = external / (conversion.get("markdown_file", f"{key}.md") if conversion else f"{key}.md")
        if markdown.is_file() and not args.pdf_text:
            if conversion is None:
                raise ValueError(f"Markdown conversion metadata is missing: {markdown}")
            if conversion["source_sha256"] != item["sha256"]:
                raise ValueError(f"Markdown source mismatch: {markdown}; regenerate from the matching PDF")
            if args.verify and hashlib.sha256(markdown.read_bytes()).hexdigest() != conversion["markdown_sha256"]:
                raise ValueError(f"Markdown SHA-256 mismatch: {markdown}")
            sections = re.split(r"(?m)^## PDF page ([1-9][0-9]*)\s*$", markdown.read_text(encoding="utf-8"))
            markdown_pages = {int(sections[i]): sections[i + 1].strip() for i in range(1, len(sections), 2)}
            if any(number not in markdown_pages for number in range(first, last + 1)):
                raise ValueError(f"requested Markdown page is missing: {markdown}")
        else:
            executable = shutil.which("pdftotext")
            if not executable:
                raise ValueError("pdftotext is unavailable; inspect the selected PDF pages")
        for number in range(first, last + 1):
            if markdown_pages is not None:
                content = markdown_pages[number]
            else:
                result = subprocess.run(
                    [executable, "-f", str(number), "-l", str(number), "-layout", str(pdf), "-"],
                    capture_output=True, text=True, encoding="utf-8", errors="replace", check=False,
                )
                if result.returncode:
                    raise ValueError(f"pdftotext failed for {key} PDF page {number}: {result.stderr.strip()}")
                if result.stderr.strip():
                    print(result.stderr.strip(), file=sys.stderr)
                content = result.stdout.replace("\f", "").strip()
            body_page = number - offset
            body_label = f", printed page {body_page}" if 1 <= body_page <= item["last_body_page"] else ""
            print(f"=== {key} | {item['version']} | PDF page {number}{body_label} ===")
            print(content or "[No extractable text; render this PDF page for inspection.]")
            print()
        return 0
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
