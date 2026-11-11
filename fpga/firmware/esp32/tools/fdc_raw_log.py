#!/usr/bin/env python3
"""Read FDR1 observations from ESP32 into CSV. No control/device writes."""
import argparse,csv,json,sys,time,urllib.error,urllib.request
FIELDS=['elapsed_s','sequence','revision','ch0','ch1','gain_q16','gain_percent',
        'valid','fresh','sample_valid','sample_fresh','quality_ch0','quality_ch1',
        'muted','pitch_stale','volume_stale']
def observation(base,timeout=2.0):
    url=base.rstrip('/')+'/api/v2/fdc'
    request=urllib.request.Request(url,headers={'Accept':'application/json','Cache-Control':'no-store'},method='GET')
    with urllib.request.urlopen(request,timeout=timeout) as response:
        state=json.load(response)
    raw=state.get('raw')
    if not isinstance(raw,dict) or raw.get('version')!=1 or not raw.get('supported'):
        raise ValueError('配套 FPGA/ESP32 尚未提供 FDR1 原始数据；需要更新双方版本。')
    return raw
def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',required=True,help='ESP32 base URL, e.g. http://192.168.4.1')
    parser.add_argument('--out',required=True,help='New CSV file (existing files are preserved)')
    parser.add_argument('--seconds',type=float,default=30)
    parser.add_argument('--interval',type=float,default=.1)
    args=parser.parse_args(argv)
    if args.seconds<=0 or args.interval<.1:parser.error('seconds>0 and interval>=0.1 required')
    first=observation(args.url)
    start=time.monotonic();deadline=start+args.seconds;rows=0
    with open(args.out,'x',newline='',encoding='utf-8') as file:
        writer=csv.DictWriter(file,fieldnames=FIELDS);writer.writeheader()
        raw=first
        try:
            while True:
                now=time.monotonic()
                row={key:raw.get(key) for key in FIELDS};row['elapsed_s']=round(now-start,6)
                writer.writerow(row);file.flush();rows+=1
                remaining=deadline-time.monotonic()
                if remaining<=0:break
                time.sleep(min(args.interval,remaining))
                if time.monotonic()>=deadline:break
                raw=observation(args.url)
        except KeyboardInterrupt:pass
    print(f'Saved {rows} observations to {args.out}; only valid/fresh samples should be used for calibration.')
if __name__=='__main__':
    try:main()
    except (OSError,ValueError,urllib.error.URLError) as error:
        print(str(error),file=sys.stderr);sys.exit(1)
