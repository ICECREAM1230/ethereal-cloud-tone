import test from 'node:test';
import assert from 'node:assert/strict';
import {validateState,engineBlocked} from '../src/protocol.mjs';
import {RuntimeController} from '../src/runtime-controller.mjs';
import {initialState} from '../mock/device.mjs';
import {RuntimeDevice,settle,turn} from './helpers/runtime-device.mjs';

const inputs=()=>({version:1,available:true,initialized:true,enabled:true,ready:true,
  fault:0,capabilities_known:true,independent_usb:true,usb_configured:false,usb_error:false,
  stop_requested:0,stop_completed:0,stop_fault:0,stop_pending:false});

test('HTTP owner readiness and USB enumeration cannot grant RAW performance permission',()=>{
  const state=initialState();state.inputs=inputs();assert.equal(engineBlocked(state),false);
  for(const key of ['available','initialized','enabled','ready','capabilities_known']) {
    state.inputs={...inputs(),[key]:false,usb_configured:true};
    assert.equal(engineBlocked(state),true,key);
  }
  state.inputs={...inputs(),stop_pending:true};assert.equal(engineBlocked(state),true);
  state.inputs={...inputs(),usb_error:true};assert.equal(engineBlocked(state),false,'USB is an independent observation');
});
test('advertised input state rejects incomplete or unversioned observations',()=>{
  for(const bad of [null,{}, {...inputs(),version:2},{...inputs(),ready:'true'},
    {...inputs(),stop_completed:-1},{...inputs(),capabilities_known:undefined}]) {
    const state=initialState();state.inputs=bad;assert.throws(()=>validateState(state));
  }
  const old=initialState();assert.equal(validateState(old),old);assert.equal(engineBlocked(old),false);
});
for(const mode of [0,1,2])test('mode '+mode+' stays connected while RAW input is blocked, then needs fresh input',async()=>{
  const d=new RuntimeDevice();
  if(mode<2){d.state.mode.requested=d.state.mode.applied=mode;d.state.legacy.ready=true;d.state.fm.available=false;d.caps.engines[2].available=false;}
  d.state.legacy.inputs=inputs();
  const c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms)});
  await c.connect();assert.equal(c.status,'online');
  assert.equal(c.playable,true);
  d.state.legacy.inputs={...inputs(),ready:false,usb_configured:true};await c.poll();
  assert.equal(c.status,'online');assert.equal(c.playable,false);assert.equal(c.keys(1),false);
  assert.match(c.message,/演奏链路/);
  d.state.legacy.inputs=inputs();await c.poll();await settle(c);
  assert.equal(c.playable,true);assert.equal(d.commands().filter(v=>v.op===3&&v.a).length,0);
});
test('a pending FPGA STOP is displayed independently of the HTTP connection',async()=>{
  const d=new RuntimeDevice();d.state.legacy.inputs={...inputs(),ready:false,stop_pending:true,stop_requested:7};
  const c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms)});
  await c.connect();assert.equal(c.status,'online');assert.equal(c.playable,false);
  assert.match(c.message,/FPGA.*停止/);
});
test('v1 fallback reports connection without claiming performance readiness',async()=>{
  const d=new RuntimeDevice();d.missing=404;d.legacy.state.inputs={...inputs(),ready:false};
  const c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms)});
  await c.connect();assert.equal(c.status,'online');assert.equal(c.playable,false);
  assert.match(c.message,/演奏链路/);
});
for(const staleError of [false,true])test('closing the input gate releases held keys ahead of a slow receipt and discards queued RAW: '+(staleError?'late error':'late success'),async()=>{
  const d=new RuntimeDevice();d.state.legacy.inputs=inputs();let clears=0;
  const request=d.transport.request;
  d.transport.request=async(path,options)=>{
    const response=await request(path,options);
    if(staleError&&path==='/api/v2/command'&&options.body.split(' ')[3]==='3'&&options.body.split(' ')[4]==='3')throw Error('old keys timeout');
    return response;
  };
  const c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms),onClear:()=>clears++});
  await c.connect();c.keys(1);await settle(c);d.time+=25;await c.poll();
  d.holdWhen=value=>value.op===3&&value.a===3;c.keys(3);await turn();assert.ok(d.gate);
  assert.equal(c.rawMidi([0x90,62,100]),true);assert.equal(c.frames.length,1);
  const before=clears,token=c.token;
  d.state.legacy.inputs.ready=false;await c.poll();
  assert.equal(c.status,'online');assert.equal(c.token,token);assert.ok(clears>before);
  assert.equal(c.desiredMask,0);assert.equal(c.sentMask,0);assert.equal(c.frames.length,0);
  assert.equal(d.commands().at(-1).a,0,'safe release cannot wait for the old HTTP response');
  const afterClose=d.commands().length;d.gate();await settle(c);
  assert.equal(c.status,'online');assert.equal(c.sentMask,0);assert.equal(c.keysCommand,null);
  d.time+=250;c.tick();await settle(c);
  d.state.legacy.inputs.ready=true;await c.poll();await settle(c);
  assert.equal(c.playable,true);assert.equal(c.desiredMask,0);assert.equal(c.sentMask,0);
  assert.equal(d.commands().slice(afterClose).filter(value=>value.op===3&&value.a).length,0);
  assert.equal(d.requests.some(request=>request.path==='/api/v1/midi/raw'),false);
});
