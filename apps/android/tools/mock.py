#!/usr/bin/env python3
"""Run the full Java protocol simulator as a localhost-only HTTP device."""
import pathlib
import subprocess
import sys
from toolchain import ROOT, JDK_BIN as JDK
from fm_contract import check_fm_contract, core_sources
check_fm_contract()
OUT=ROOT/'build/mock-classes'
OUT.mkdir(parents=True,exist_ok=True)
sources=core_sources()+[ROOT/'tools/MockServer.java']
subprocess.run([str(JDK/'javac'),'-encoding','UTF-8','-d',str(OUT),*[str(f) for f in sources]],check=True)
subprocess.run([str(JDK/'java'),'--add-modules','jdk.httpserver','-cp',str(OUT),'MockServer',sys.argv[1] if len(sys.argv)>1 else '8765'],check=True)
