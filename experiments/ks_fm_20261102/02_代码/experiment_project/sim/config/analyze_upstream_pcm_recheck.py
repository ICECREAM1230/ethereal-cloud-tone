#!/usr/bin/env python3
"""Offline PCM24 capture comparison. Integer time shift only; never fit audio gain.

Exact PCM equality, inferred timing alignment and spectral similarity are
separate results. Capture version/state/native sample identity is not proven
by a waveform match or by USB sequence continuity.
"""
from __future__ import annotations
import argparse, csv, hashlib, importlib.util, json, math, struct, sys, wave
from dataclasses import asdict
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
FS24 = 8388608.0
NFFT = 8192

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def db(value):
    return 20 * math.log10(max(float(value), 1e-15))

def rms(x):
    x = np.asarray(x, dtype=float)
    return float(np.sqrt(np.mean(x*x))) if x.size else None

def correlation(x, y):
    x = np.asarray(x, float); y = np.asarray(y, float)
    x = x-x.mean(); y = y-y.mean()
    norm = np.linalg.norm(x)*np.linalg.norm(y)
    return float(np.dot(x,y)/norm) if norm else None

def read_wav(path):
    with wave.open(str(path), 'rb') as wav:
        assert wav.getnchannels()==2 and wav.getsampwidth() in (3,4)
        assert wav.getcomptype()=='NONE'
        width=wav.getsampwidth(); rate=wav.getframerate()
        raw=wav.readframes(wav.getnframes())
    if width==4:
        values=np.frombuffer(raw,'<i4').reshape(-1,2)
        assert np.all((values & 255)==0), 'S32 is not a verified left-aligned PCM24 container'
        values=(values>>8).copy()
    else:
        b=np.frombuffer(raw,np.uint8).reshape(-1,3).astype(np.int32)
        v=b[:,0]|b[:,1]<<8|b[:,2]<<16
        values=((v^0x800000)-0x800000).reshape(-1,2)
    bit_or=int(np.bitwise_or.reduce(values.ravel().view(np.uint32)))
    trailing=(bit_or & -bit_or).bit_length()-1 if bit_or else None
    info={'path':str(Path(path).resolve()),'SHA256':sha(path),'frames':len(values),
          'WAV_rate_Hz':rate,'channels':2,'container_bits':8*width,
          'PCM24_common_trailing_zero_bits':trailing,
          'low8_zero_verified':True if width==4 else None,
          'zero_channel':[bool(np.all(values[:,c]==0))for c in range(2)]}
    return values,rate,raw,info

def verify_transport(folder, wav_raw, wav_width):
    rawpath=folder/'capture/raw.bin'; payload=folder/'capture/payload.pcm'
    if not rawpath.exists():
        return {'status':'NOT_AVAILABLE','reason':'No raw.bin; WAV analysis does not establish transport validity'}
    modpath=ROOT/'tools/ch569_pcm_capture/pcm_capture/protocol.py'
    spec=importlib.util.spec_from_file_location('pcm_recheck_protocol',modpath)
    module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)
    parser=module.FrameParser(); stream=rawpath.read_bytes(); frames=[]
    for at in range(0,len(stream),333): frames.extend(parser.feed(stream[at:at+333]))
    decoded=b''.join(f.payload for f in frames)
    assert wav_width==4,'Transport equality currently requires the captured original S32 WAV'
    assert frames and all(f.version==1 and f.flags==3 and f.sample_rate==96008 and f.payload_len==512 and f.crc16==0 for f in frames)
    assert parser.stats.frames_invalid==parser.stats.seq_jumps==parser.stats.sample_rate_changes==0
    assert parser.stats.bytes_discarded==parser.stats.resync_count==0,'Raw stream required resynchronization'
    assert len(stream)%4096==0 and parser.stats.frames_valid==len(stream)//4096*7
    assert parser.stats.padding_bytes==len(stream)//4096*414
    assert parser.pending_bytes==0, f'Unparsed/truncated raw bytes: {parser.pending_bytes}'
    assert decoded==wav_raw, 'Raw transport payload differs from WAV PCM'
    if payload.exists(): assert payload.read_bytes()==wav_raw,'payload.pcm differs from WAV'
    return {'status':'PASS','raw_SHA256':sha(rawpath),'raw_bytes':len(stream),
            'parser_SHA256':sha(modpath),'parser':asdict(parser.stats),
            'raw_payload_WAV_exact':True,'native_sample_index_verified':False,
            'CRC_verified':False,'FPGA_drop_counter_verified':False}

def detect_onset(x, rate, start=0):
    block=96
    frames=len(x)//block*block
    y=x[:frames].astype(float).reshape(-1,block,2)/FS24
    ac=np.sqrt(np.mean((y-y.mean(axis=1,keepdims=True))**2,axis=(1,2)))
    before=ac[:min(len(ac),max(1,int(start/block)))]
    # A low-level failed take must remain identifiable; do not normalize it
    # until its residual is mislabelled as a successful attack.
    noise=float(np.median(before)) if len(before) else 0
    threshold=max(4/FS24,0.0005,noise*8)
    good=(ac>threshold) & (np.arange(len(ac))*block>=start)
    sustained=np.flatnonzero(np.convolve(good.astype(int),np.ones(3,dtype=int),'valid')==3)
    onset=int(sustained[0]*block) if len(sustained) else None
    return onset,{'method':'96-frame AC-RMS, 3 consecutive windows',
                  'threshold_FS':threshold,'preroll_AC_RMS_FS':noise,
                  'start_search_frame':int(start),'substantial_attack_detected':onset is not None}

def best_common_lag(reference,capture,guess,rate,radius_seconds=.03):
    """One stereo lag, no channel swap, interpolation, resampling or gain fitting."""
    start=round(.08*rate); length=round(.30*rate); radius=round(radius_seconds*rate)
    if guess<radius: radius=max(0,guess)
    lo=guess-radius+start;hi=guess+radius+start+length
    if hi>len(capture) or start+length>len(reference):return None
    template=reference[start:start+length].astype(float)
    search=capture[lo:hi].astype(float)
    template-=template.mean(axis=0,keepdims=True)
    size=1<<(len(search)+len(template)-2).bit_length()
    numerator=np.zeros(2*radius+1)
    for ch in range(2):
        conv=np.fft.irfft(np.fft.rfft(search[:,ch],size)*np.fft.rfft(template[::-1,ch],size),size)
        numerator+=conv[length-1:length+2*radius]
    squares=np.cumsum(np.r_[0.0,np.sum(search*search,axis=1)])
    sums=np.vstack([np.zeros((1,2)),np.cumsum(search,axis=0)])
    energy=squares[length:]-squares[:-length]-np.sum((sums[length:]-sums[:-length])**2,axis=1)/length
    norm=np.sqrt(np.maximum(energy,0)*np.sum(template*template))
    coeff=np.divide(numerator,norm,out=np.zeros_like(numerator),where=norm>0)
    chosen=int(np.argmax(coeff));lag=guess-radius+chosen
    return {'capture_start_frame':lag,'coefficient':float(coeff[chosen]),
            'search_radius_frames':radius,'common_stereo_lag':True,
            'scope':'Diagnostic best positive correlation. Periodic/free-running waveforms can have ambiguous lag; no proof of native sample identity.'}

def difference(reference,capture,start):
    assert start>=0 and len(capture)>=start+len(reference),'capture too short for full reference comparison'
    y=capture[start:start+len(reference)]
    delta=y.astype(np.int64)-reference.astype(np.int64)
    per=[]
    for ch in range(2):
        x=reference[:,ch].astype(float);z=y[:,ch].astype(float)
        denom=float(np.dot(x,x))
        per.append({'channel':'LR'[ch],'reference_RMS_dBFS':db(rms(x)/FS24),
            'capture_RMS_dBFS':db(rms(z)/FS24),'RMS_difference_dB':db(rms(z)/max(rms(x),1e-15)),
            'correlation':correlation(x,z),
            'diagnostic_projection_gain':float(np.dot(x,z)/denom)if denom else None,
            'projection_gain_applied':False,'delta_RMS_dBFS':db(rms(delta[:,ch])/FS24)})
    return {'capture_start_frame':int(start),'frames':len(reference),'gain_applied':1,
        'normalization':False,'resampling':False,'channel_order':'L,R unchanged',
        'bit_exact':bool(np.array_equal(reference,y)),
        'changed_scalar_samples':int(np.count_nonzero(delta)),
        'max_abs_delta24':int(np.max(np.abs(delta))),
        'delta_RMS_dBFS':db(rms(delta)/FS24),
        'delta_relative_to_reference_dB':db(rms(delta)/max(rms(reference),1e-15)),
        'first_changed_pair':int(np.flatnonzero(np.any(delta!=0,axis=1))[0])if np.any(delta)else None,
        'channels':per,
        'same_channel_correlation':[[correlation(reference[:,a],y[:,b])for b in range(2)]for a in range(2)]}

def spectrum(x,rate,start,end):
    z=x[start:end].astype(float)/FS24
    assert len(z)>=NFFT,'held FFT interval too short'
    win=np.hanning(NFFT);spectra=[]
    for at in range(0,len(z)-NFFT+1,NFFT//2):
        segment=z[at:at+NFFT];segment=segment-segment.mean(axis=0,keepdims=True)
        mag=np.abs(np.fft.rfft(segment*win[:,None],axis=0))*2/win.sum()
        mag[[0,-1]]*=.5;spectra.append(mag*mag)
    power=np.mean(spectra,axis=0);f=np.fft.rfftfreq(NFFT,1/rate)
    return f,power

def audio_metrics(x,rate,onset,name):
    # Equivalent to the final RTL analyser's .18-.45s hold when its onset=.05s.
    lo=onset+round(.13*rate);hi=onset+round(.40*rate)
    f,power=spectrum(x,rate,lo,hi);channels=[]
    for ch in range(2):
        y=x[:,ch].astype(float)/FS24;h=y[lo:hi];total=power[f>=20,ch].sum()
        bands={str(edge):float(power[f>edge,ch].sum()/total)if total else None for edge in [2000,5000,10000,20000]}
        peaks=np.where((power[1:-1,ch]>power[:-2,ch])&(power[1:-1,ch]>=power[2:,ch]))[0]+1
        peaks=[k for k in sorted(peaks,key=lambda k:power[k,ch],reverse=True)if 20<=f[k]<=20000][:12]
        harmonics=[]
        if name=='single_c4':
            for order in range(1,33):
                target=261.6255653*order;bw=max(2*rate/NFFT,.02*target)
                mask=abs(f-target)<=bw
                harmonics.append({'order':order,'target_Hz':target,'peak_dBFS':db(np.sqrt(power[mask,ch].max()))})
        trusted=rms(h)>=10**(-70/20)
        slope=float(np.polyfit(np.log2([h['target_Hz']for h in harmonics[1:16]]),[h['peak_dBFS']for h in harmonics[1:16]],1)[0])if trusted and harmonics else None
        tail=[]
        # Reference schedule NoteOff is ~450ms after first NoteOn. This is an
        # explicit display coordinate, not a measured hardware note-off tag.
        for after in [0,.25,.5,.9]:
            at=onset+round((.45+after)*rate);seg=y[at:at+round(.05*rate)]
            tail.append({'seconds_after_assumed_NoteOff':after,'RMS_dBFS':db(rms(seg))if len(seg)else None})
        channels.append({'channel':'LR'[ch],'peak24':int(np.max(np.abs(x[:,ch].astype(np.int64)))),
            'peak_dBFS':db(np.max(np.abs(y))),'hold_RMS_dBFS':db(rms(h)),
            'hold_AC_RMS_dBFS':db(float(np.std(h))),'hold_DC_FS':float(h.mean()),
            'rail_samples':int(np.count_nonzero((x[:,ch]==8388607)|(x[:,ch]==-8388608))),
            'whole_zero':bool(np.all(x[:,ch]==0)),
            'spectral_trusted':trusted,'band_power_fraction_gt_Hz':bands,
            'spectral_centroid_Hz':float((f[f>=20]*power[f>=20,ch]).sum()/total)if total else None,
            'strongest_local_peaks':[{'frequency_Hz':float(f[k]),'amplitude_dBFS':db(np.sqrt(power[k,ch]))}for k in peaks],
            'C4_harmonic_envelope':harmonics,'C4_envelope_slope_dB_per_octave_h2_h16':slope,
            'tail_windows':tail})
    return {'frames':len(x),'duration_s':len(x)/rate,'onset_frame':int(onset),
            'held_start_frame':lo,'held_end_frame':hi,'NFFT':NFFT,
            'window':'Hann, per-window/channel mean removed; RMS-averaged spectral power',
            'normalization':False,'tail_noteoff_coordinate_assumption_s_after_onset':.45,
            'channels':channels},(f,power)

def jsonfile(path):return json.loads(path.read_text())if path.exists()else None

def infer_name(name):
    return 'poly32' if 'poly32' in name else 'chord_ceg' if 'chord_ceg' in name else 'single_c4' if 'single_c4' in name else None

def compare_case(folder,reference_root,out,explicit_start=None,alignment='onset'):
    name=infer_name(folder.name);assert name, 'Cannot infer reference scenario from capture directory name'
    capwav=folder/'capture/audio.wav';refwav=reference_root/name/(name+'-usb_s32.wav')
    x,sr,_,xref=read_wav(refwav);y,cr,raw,yref=read_wav(capwav);assert sr==cr,'Nominal sample rates differ; no resampling allowed'
    run=jsonfile(folder/'run.json')or{};midi=jsonfile(folder/'midi.json')or{}
    transport=verify_transport(folder,raw,yref['container_bits']//8)
    preroll=int(run.get('ready_preroll',{}).get('sample_frames',round(.25*cr)))
    refon,refdetect=detect_onset(x,sr,0);capon,capdetect=detect_onset(y,cr,preroll)
    assert refon is not None,'Reference has no substantial attack'
    hostestimate=None
    if run.get('midi_launch_monotonic') is not None and run.get('capture_started_monotonic') is not None and midi.get('events'):
        hostestimate=round((run['midi_launch_monotonic']-run['capture_started_monotonic']+(midi['events'][0]['dispatch_ms']-midi['replay_start_ms'])/1000)*cr)
    if explicit_start is not None: start=explicit_start;method='explicit capture frame supplied by caller'
    elif capon is not None:start=capon-refon;method='coarse PCM onset difference'
    elif hostestimate is not None:start=max(0,hostestimate-refon);method='host-time estimate for quiet take; not sample-accurate'
    else:raise ValueError('No substantial attack/host estimate; specify --capture-start-frame to analyze quiet capture')
    assert start>=0
    fine=best_common_lag(x,y,start,sr)
    chosen=fine['capture_start_frame']if alignment=='correlation'and fine else start
    if alignment=='correlation':method+='; diagnostic common-stereo correlation refinement'
    result={'status':'MEASURED','capture_name':folder.name,'reference_scenario':name,
            'capture':yref,'reference':xref,'transport':transport,
            'reference_onset':refdetect,'capture_onset':capdetect,
            'alignment_method':method,'host_NoteOn_frame_estimate':hostestimate,
            'primary_alignment':difference(x,y,start),'diagnostic_common_lag':fine,
            'selected_comparison':difference(x,y,chosen),
            'reference_vs_capture_state_identity_proven':False,
            'interpretation':'Only an integer time shift is applied. Gain, channels and rate remain unchanged. Exact equality does not establish build/state identity; non-equality under free-running phase/LFO or uncertain native event timing does not localize a hardware defect.'}
    if fine:result['diagnostic_lag_comparison']=difference(x,y,fine['capture_start_frame'])
    selected_onset=chosen+refon
    result['reference_audio'],rspec=audio_metrics(x,sr,refon,name)
    result['capture_audio'],cspec=audio_metrics(y,cr,selected_onset,name)
    if not capdetect['substantial_attack_detected']:
        for ch in result['capture_audio']['channels']:
            ch['spectral_trusted']=False
            ch['C4_envelope_slope_dB_per_octave_h2_h16']=None
    result['held_RMS_difference_dB']=[b['hold_RMS_dBFS']-a['hold_RMS_dBFS']for a,b in zip(result['reference_audio']['channels'],result['capture_audio']['channels'])]
    result['tail_timing_scope']='.45s after aligned onset is the reference schedule only; inspect actual MIDI ACK/time metadata before interpreting tail differences'
    result['metadata_files_SHA256']={p.name:sha(p)for p in [folder/'run.json',folder/'midi.json',folder/'events.json']if p.exists()}
    dest=out/folder.name;dest.mkdir(exist_ok=False)
    (dest/'comparison.json').write_text(json.dumps(result,ensure_ascii=False,indent=2,allow_nan=False)+'\n')
    with(dest/'spectrum.csv').open('w',newline='')as f:
        w=csv.writer(f);w.writerow(['frequency_Hz','reference_L_dBFS','reference_R_dBFS','capture_L_dBFS','capture_R_dBFS'])
        for i,hz in enumerate(rspec[0]):w.writerow([hz,*[db(v)for v in np.sqrt(rspec[1][i])],*[db(v)for v in np.sqrt(cspec[1][i])]])
    return result

def self_test(out):
    rng=np.random.default_rng(20261101);n=60000
    x=rng.integers(-500000,500000,(n,2),dtype=np.int32)
    y=np.concatenate([rng.integers(-500000,500000,(137,2),dtype=np.int32),x,np.zeros((5000,2),np.int32)])
    tests={}
    tests['offset_unaligned_detected']=not difference(x,y,0)['bit_exact']
    fine=best_common_lag(x,y,150,96008,radius_seconds=.001)
    tests['offset_137_found']=fine['capture_start_frame']==137 and difference(x,y,137)['bit_exact']
    gain=x//2;tests['half_gain_not_hidden']=not difference(x,gain,0)['bit_exact'] and abs(difference(x,gain,0)['channels'][0]['diagnostic_projection_gain']-.5)<1e-4
    swap=x[:,::-1].copy();tests['channel_swap_detected']=not difference(x,swap,0)['bit_exact'] and difference(x,swap,0)['same_channel_correlation'][0][1]>.999999
    dropped=np.concatenate([x[:30000],x[30001:],np.zeros((1,2),np.int32)]);diff=difference(x,dropped,0)
    tests['single_pair_drop_detected']=not diff['bit_exact']and diff['first_changed_pair']==30000
    tests['one_lsb_detected']=not difference(x,np.concatenate([x[:100],x[100:101]+1,x[101:]]),0)['bit_exact']
    # A sequence gap must remain detectable even when audio is superficially smooth.
    modpath=ROOT/'tools/ch569_pcm_capture/pcm_capture/protocol.py';spec=importlib.util.spec_from_file_location('pcm_selftest_protocol',modpath);m=importlib.util.module_from_spec(spec);sys.modules[spec.name]=m;spec.loader.exec_module(m)
    p=m.FrameParser();p.feed(m.pack_frame(bytes(512),seq=0,sample_rate=96008,flags=3)+m.pack_frame(bytes(512),seq=2,sample_rate=96008,flags=3))
    tests['sequence_gap_detected']=p.stats.seq_jumps==1
    assert all(tests.values()),tests
    report={'status':'PASS','tests':tests,'scope':'Synthetic negative controls of offline alignment/difference/transport checks; no board result','script_SHA256':sha(__file__)}
    (out/'self-test.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

def plot_results(out,results):
    import os
    os.environ['QT_QPA_PLATFORM']='offscreen'
    pc=Path('/media/ice/D;/PROJECT_improve/pcm24_subtractive_ks_20261025T185127/pc')
    sys.path.insert(0,str(pc))
    from pcm_studio.qt_runtime import configure_qt
    configure_qt()
    from PySide6.QtWidgets import QApplication
    import pyqtgraph as pg
    import pyqtgraph.exporters
    app=QApplication.instance()or QApplication([])
    pg.setConfigOptions(background='w',foreground='#243244',antialias=True)
    layout=pg.GraphicsLayoutWidget();layout.resize(1500,320*min(6,len(results)))
    for row,r in enumerate(results[:6]):
        ax=layout.addPlot(row=row,col=0,title=r['capture_name']+' | original RMS, integer time aligned')
        ax.addLegend();ax.setLabel('bottom','Time after aligned onset','s');ax.setLabel('left','RMS','dBFS')
        ax.setXRange(-.05,1.45);ax.setYRange(-100,0);ax.showGrid(x=True,y=True,alpha=.2)
        for origin,color in [('reference','#2465a3'),('capture','#c94b46')]:
            x,rate,_,_=read_wav(Path(r[origin]['path']))
            onset=r[origin+'_audio']['onset_frame'];block=960
            start=max(0,onset-round(.05*rate));end=min(len(x),onset+round(1.45*rate))
            n=(end-start)//block;z=x[start:start+n*block].astype(float).reshape(n,block,2)/FS24
            level=np.sqrt(np.mean(z*z,axis=(1,2)))
            time=(start+(np.arange(n)+.5)*block-onset)/rate
            ax.plot(time,20*np.log10(np.maximum(level,1e-15)),pen=pg.mkPen(color,width=2),name=origin)
        ax=layout.addPlot(row=row,col=1,title='Held FFT | same window, stereo power average, no normalization')
        ax.addLegend();ax.setLabel('bottom','Frequency','Hz');ax.setLabel('left','Amplitude','dBFS')
        ax.setXRange(20,10000);ax.setYRange(-130,0);ax.showGrid(x=True,y=True,alpha=.2)
        data=np.genfromtxt(out/r['capture_name']/'spectrum.csv',delimiter=',',skip_header=1)
        for name,color,cols in [('reference','#2465a3',[1,2]),('capture','#c94b46',[3,4])]:
            mag=10*np.log10(np.maximum(np.mean(10**(data[:,cols]/10),axis=1),1e-30))
            ax.plot(data[:,0],mag,pen=pg.mkPen(color,width=1.4),name=name)
    app.processEvents();exporter=pg.exporters.ImageExporter(layout.ci);exporter.parameters()['width']=2000
    exporter.export(str(out/'capture-vs-RTL.png'));layout.close();app.processEvents()

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture-root',type=Path);ap.add_argument('--reference',type=Path)
    ap.add_argument('--out',required=True,type=Path);ap.add_argument('--capture-start-frame',type=int)
    ap.add_argument('--alignment',choices=['onset','correlation'],default='onset')
    ap.add_argument('--plot',action='store_true',help='PNG using the existing PC PySide6/pyqtgraph environment; first six takes')
    ap.add_argument('--self-test',action='store_true');a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    if a.self_test:return self_test(a.out)
    assert a.capture_root and a.reference,'--capture-root and --reference required'
    folders=sorted({p.parent.parent for p in a.capture_root.rglob('capture/audio.wav')if infer_name(p.parent.parent.name)})
    if (a.capture_root/'capture/audio.wav').exists()and infer_name(a.capture_root.name):folders=[a.capture_root]+[p for p in folders if p!=a.capture_root]
    assert folders,'No recognized single_c4/chord_ceg/poly32 capture folders found'
    results=[compare_case(p,a.reference,a.out,a.capture_start_frame,a.alignment)for p in folders]
    summary={'status':'MEASURED','normalization':False,'gain_applied':1,'resampling':False,
             'script_SHA256':sha(__file__),'cases':results,
             'all_selected_PCM_bit_exact':all(r['selected_comparison']['bit_exact']for r in results),
             'native_event_sample_identity_proven':False}
    (a.out/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2,allow_nan=False)+'\n')
    if a.plot:plot_results(a.out,results)
    for r in results:print(json.dumps({'capture':r['capture_name'],'bit_exact':r['selected_comparison']['bit_exact'],
        'max_abs_delta24':r['selected_comparison']['max_abs_delta24'],'held_RMS_difference_dB':r['held_RMS_difference_dB'],
        'transport':r['transport']['status']},ensure_ascii=False))

if __name__=='__main__':main()
