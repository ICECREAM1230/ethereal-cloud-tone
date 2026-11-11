#!/usr/bin/env python3
"""Runtime lifecycle and discovery acceptance; no device or emulator operation."""
import subprocess
import pathlib
import tempfile
from toolchain import ROOT, JDK_BIN as JDK
from fm_contract import (check_fm_contract, check_historical_fm_contract, core_sources,
                         HISTORICAL_CONTRACT_DIRECTORY, CURRENT_HTTP_FIXTURES, HISTORICAL_HTTP_FIXTURES)


def variant(label, fixtures, contract_directory=None):
    scratch=ROOT/'build/fm-current/runtime';scratch.mkdir(parents=True,exist_ok=True)
    out=pathlib.Path(tempfile.mkdtemp(prefix=label+'-',dir=scratch))
    sources=core_sources(contract_directory=contract_directory)+[ROOT/'tools/RuntimeModel.java',ROOT/'tools/RuntimeHttpTest.java']+[ROOT/'core/src/test/java/cn/fpgasynth/control/core/V2SessionTest.java',ROOT/'core/src/test/java/cn/fpgasynth/control/core/AppControllerTest.java']
    print('Runtime variant: '+label+'; host fixtures, no device/audio claim',flush=True)
    subprocess.run([str(JDK/'javac'),'-encoding','UTF-8','--release','8','-Xlint:all','-d',str(out),*[str(x) for x in sources]],check=True)
    for test in ('V2SessionTest','AppControllerTest','RuntimeHttpTest'):
        command=[str(JDK/'java'),'-ea','-cp',str(out),'cn.fpgasynth.control.core.'+test,str(fixtures)]
        if test=='RuntimeHttpTest':command.append(str(scratch/('fm-http-vectors-'+label+'.json')))
        subprocess.run(command,check=True)


if __name__=='__main__':
    check_fm_contract()
    check_historical_fm_contract()
    variant('current',ROOT/CURRENT_HTTP_FIXTURES)
    variant('historical',ROOT/HISTORICAL_HTTP_FIXTURES,HISTORICAL_CONTRACT_DIRECTORY)
