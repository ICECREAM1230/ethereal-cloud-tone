#!/usr/bin/env python3
"""Read the board's existing FT2CH channel B, without transmitting/resetting it.

Uses the installed D2XX library when this debugger has no Linux ttyUSB node.
API: FTDI D2XX Programmer's Guide, classic FT_Open/Read/Close functions.
"""
import argparse,ctypes,ctypes.util,datetime,json,os,re,time
from pathlib import Path

def capture(seconds,baud,output,library=None):
    library=library or os.environ.get("FTD2XX_LIBRARY") or ctypes.util.find_library("ftd2xx")
    if not library:raise SystemExit("D2XX library not found; pass --library or set FTD2XX_LIBRARY")
    output.mkdir(parents=True,exist_ok=False)
    lib=ctypes.CDLL(str(library));handle=ctypes.c_void_p()
    u32=ctypes.c_uint32;u8=ctypes.c_ubyte
    signatures={
        'FT_Open':[ctypes.c_int,ctypes.POINTER(ctypes.c_void_p)],
        'FT_Close':[ctypes.c_void_p],
        'FT_SetBaudRate':[ctypes.c_void_p,u32],
        'FT_SetDataCharacteristics':[ctypes.c_void_p,u8,u8,u8],
        'FT_SetFlowControl':[ctypes.c_void_p,ctypes.c_ushort,u8,u8],
        'FT_SetTimeouts':[ctypes.c_void_p,u32,u32],
        'FT_GetQueueStatus':[ctypes.c_void_p,ctypes.POINTER(u32)],
        'FT_Read':[ctypes.c_void_p,ctypes.c_void_p,u32,ctypes.POINTER(u32)]}
    for name,args in signatures.items():
        f=getattr(lib,name);f.argtypes=args;f.restype=u32
    def call(name,*args):
        code=getattr(lib,name)(*args)
        if code:raise RuntimeError(f'{name} returned FT_STATUS {code}')
    result={'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'channel':1,'baud':baud,'format':'8N1','transmitted_bytes':0,'library':str(library)}
    raw=bytearray();opened=False;error=None
    try:
        call('FT_Open',1,ctypes.byref(handle));opened=True
        call('FT_SetBaudRate',handle,baud)
        call('FT_SetDataCharacteristics',handle,8,0,0)
        call('FT_SetFlowControl',handle,0,0,0)
        call('FT_SetTimeouts',handle,1000,1000)
        deadline=time.monotonic()+seconds
        with (output/'capture.bin').open('wb') as stream:
            while time.monotonic()<deadline:
                queued=u32();call('FT_GetQueueStatus',handle,ctypes.byref(queued))
                if queued.value:
                    count=min(queued.value,65536);buf=ctypes.create_string_buffer(count);read=u32()
                    call('FT_Read',handle,buf,count,ctypes.byref(read))
                    data=buf.raw[:read.value];raw.extend(data);stream.write(data);stream.flush()
                else:time.sleep(0.01)
    except Exception as exc:
        error=str(exc)
    finally:
        if opened:
            code=lib.FT_Close(handle)
            if code:error=(error+'; ' if error else '')+f'FT_Close returned {code}'
    text=raw.decode('ascii',errors='replace');(output/'capture.txt').write_text(text)
    pattern=r'AE35 S=([0-9a-f]{8}) B=([0-9a-f]{8}) N=([0-9a-f]{8}) A=([0-9a-f]{8}) W=([0-9a-f]{8}) R=([0-9a-f]{8}) C=([0-9a-f]{8}) P=([0-9a-f]{8}) V=([0-9a-f]{8})\r\n'
    records=[dict(zip(['status','boot_stage','apb_count','last_address','last_write','last_read','mcause','mepc','mtval'],[int(v,16) for v in m])) for m in re.findall(pattern,text)]
    result.update({'bytes':len(raw),'records':records,'error':error,
        'status':'READ_FAILED' if error else 'RECORDS_CAPTURED' if records else 'NO_DIAGNOSTIC_RECORDS',
        'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result));return 1 if error else 0

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--seconds',type=float,default=10)
    p.add_argument('--baud',type=int,default=115200);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--library',type=Path,help='Installed D2XX library; alternatively FTD2XX_LIBRARY or system loader')
    a=p.parse_args()
    if not 0<a.seconds<=60:p.error('seconds must be in (0,60]')
    raise SystemExit(capture(a.seconds,a.baud,a.output,a.library))
