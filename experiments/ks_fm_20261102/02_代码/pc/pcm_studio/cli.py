"""Unified command-line front end; desktop dependencies are imported lazily."""
from pathlib import Path
import argparse
import importlib.metadata
import json
import sys
import time


def capture_main(argv=None):
    p=argparse.ArgumentParser(description='CH569 PCM mono/stereo16/32 capture / RAW or WAV replay')
    p.add_argument('--input',type=Path,help='offline RAW/WAV input; never touches USB')
    p.add_argument('--source',choices=('usb','demo','raw','wav'),default=None)
    p.add_argument('--out',type=Path,help='new output directory; existing directories refused')
    p.add_argument('--no-record',action='store_true',help='bounded real-time analysis without recording')
    p.add_argument('--duration',type=float,default=10,help='seconds; 0 means until EOF/Ctrl-C')
    p.add_argument('--require-pattern',action='store_true')
    p.add_argument('--fast',action='store_true',help='unpaced file processing; not a real-time simulation')
    p.add_argument('--progress',action='store_true')
    a=p.parse_args(argv)
    if not a.no_record and a.out is None:p.error('--out is required unless --no-record')
    if a.no_record and a.out is not None:p.error('choose either --out or --no-record')
    key=a.source or ('wav' if a.input and a.input.suffix.lower()=='.wav' else 'raw' if a.input else 'usb')
    if key in ('raw','wav') and a.input is None:p.error('file sources require --input')
    if key in ('usb','demo') and a.input is not None:p.error('--input cannot be combined with USB/demo')
    try:
        from .session import CaptureSession
        from .sources import UsbSource,RawSource,WavSource,DemoSource
        factories={'usb':lambda:UsbSource(),'raw':lambda:RawSource(a.input,realtime=not a.fast),
                   'wav':lambda:WavSource(a.input,realtime=not a.fast),'demo':lambda:DemoSource()}
        s=CaptureSession();s.start(factories[key],a.out,duration=a.duration,require_pattern=a.require_pattern)
        last=0
        try:
            while not s.wait(0.1):
                if a.progress and time.monotonic()-last>=1:
                    stats=s.stats();print(f"{stats['status']}: {stats['sample_frames']} sample frames",file=sys.stderr)
                    last=time.monotonic()
        except KeyboardInterrupt:
            s.stop();s.wait()
        stats=s.stats();print(json.dumps(stats,ensure_ascii=False,indent=2,allow_nan=False))
        return 0 if stats['status']=='PASS' else 1
    except Exception as exc:
        print(f'采集无法启动：{exc}；依赖缺失时请运行本目录安装脚本。',file=sys.stderr)
        return 2


def compare_main(argv=None):
    from .comparison import compare_wavs
    p=argparse.ArgumentParser(description='Exact mono/stereo PCM WAV comparison')
    p.add_argument('capture',type=Path);p.add_argument('reference',type=Path)
    p.add_argument('--reference-offset',type=int,default=0);p.add_argument('--out',type=Path)
    a=p.parse_args(argv)
    try:result=compare_wavs(a.capture,a.reference,a.reference_offset)
    except Exception as exc:result=dict(status='FAIL',error=str(exc))
    text=json.dumps(result,ensure_ascii=False,indent=2)+'\n'
    if a.out:a.out.write_text(text,encoding='utf-8')
    print(text,end='');return 0 if result['status']=='PASS' else 1


def gui_main(argv=None):
    p=argparse.ArgumentParser(description='PCM Studio desktop interface')
    p.add_argument('--demo',action='store_true',help='start synthetic demo; no USB access')
    p.add_argument('--output-root',type=Path);p.add_argument('--duration',type=float,default=10)
    a=p.parse_args(argv)
    try:
        from .gui import main
        return main(output_root=a.output_root,demo=a.demo,duration=a.duration)
    except ImportError as exc:
        print(f'界面依赖缺失：{exc}。请先运行 install_linux.sh 或 install_windows.cmd。',file=sys.stderr)
        return 2


def doctor_main(argv=None):
    p=argparse.ArgumentParser(description='Read-only dependency and optional USB discovery check')
    p.add_argument('--usb',action='store_true');a=p.parse_args(argv)
    report={'status':'PASS','python':sys.version,'executable':sys.executable,'dependencies':{},'device_flash_write':False}
    for name in ('numpy','PySide6-Essentials','pyqtgraph','pyusb','libusb-package'):
        try:report['dependencies'][name]=importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError:report['dependencies'][name]='MISSING';report['status']='FAIL'
    try:
        from .sources import usb_backend,list_usb_devices
        report['libusb_backend']=type(usb_backend()).__name__
        if a.usb:report['usb_devices']=list_usb_devices()
    except Exception as exc:report.update(status='FAIL',error=str(exc))
    print(json.dumps(report,ensure_ascii=False,indent=2))
    return 0 if report['status']=='PASS' else 1


def main(argv=None):
    args=list(sys.argv[1:] if argv is None else argv)
    commands={'gui':gui_main,'capture':capture_main,'compare':compare_main,'doctor':doctor_main}
    if not args:args=['gui']
    if args[0]=='analyze':
        from .offline import main as analyze_main
        sys.argv=[sys.argv[0]]+args[1:];analyze_main();return 0
    if args[0] not in commands:
        print('Usage: pcm_tool.py {gui,capture,analyze,compare,doctor} [options]',file=sys.stderr)
        return 2
    return commands[args[0]](args[1:])
