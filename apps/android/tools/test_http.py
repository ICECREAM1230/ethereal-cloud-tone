#!/usr/bin/env python3
import pathlib
import subprocess
from toolchain import ROOT, JDK_BIN as JDK
from fm_contract import check_fm_contract, core_sources
check_fm_contract()
OUT=ROOT/'build/http-test-classes'
OUT.mkdir(parents=True,exist_ok=True)
sources=core_sources()+[ROOT/'tools/MockServer.java',ROOT/'tools/HttpScenarioTest.java']
subprocess.run([str(JDK/'javac'),'-encoding','UTF-8','-d',str(OUT),*[str(f) for f in sources]],check=True)
server=subprocess.Popen([str(JDK/'java'),'--add-modules','jdk.httpserver','-cp',str(OUT),'MockServer','0'],stdout=subprocess.PIPE,text=True)
try:
    line=server.stdout.readline().strip()
    if not line.startswith('MOCK_PORT='):raise RuntimeError('mock startup failed: '+line)
    subprocess.run([str(JDK/'java'),'-cp',str(OUT),'HttpScenarioTest','http://127.0.0.1:'+line.split('=')[1]],check=True)
finally:
    server.terminate()
    server.wait(timeout=5)
