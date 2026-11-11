#!/usr/bin/env python3
"""Compress the authoritative page at build time, without timestamps or filenames."""
import gzip
import io
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:])
buffer = io.BytesIO()
with gzip.GzipFile(fileobj=buffer, mode="wb", filename="", mtime=0, compresslevel=9) as output:
    output.write(source.read_bytes())
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_bytes(buffer.getvalue())
