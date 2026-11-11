import test from 'node:test';
import assert from 'node:assert/strict';
import {RuntimeController,createRuntimeTransport} from '../src/runtime-controller.mjs';
import {RawMidiDevice} from './helpers/raw-midi-device.mjs';
import {settle,turn} from './helpers/runtime-device.mjs';
async function connected(){const d=new RawMidiDevice();const c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms)});await c.connect();return {d,c};}
const bodies=d=>d.rawRequests.map(r=>JSON.parse(r.options.body));
test('raw performance uses current binding and never reports FPGA or owner adoption from 202',async()=>{
  const {d,c}=await connected();assert.equal(typeof c.rawMidi,'function');
  const bytes=[159,60,0];assert.equal(c.rawMidi(bytes),true);bytes[0]=0;await settle(c);
  assert.deepEqual(bodies(d),[{source:'web',bytes:[159,60,0],token:100,sequence:2,context_epoch:10}]);
  assert.equal(d.rawRequests[0].options.headers['Content-Type'],'application/json');
  assert.equal(c.lastReceived,1);assert.equal(c.lastAdopted,1);assert.match(c.message,/排队/);assert.doesNotMatch(c.message,/已采用/);
  assert.equal(c.rawMidiReceipt.stage,'queued');assert.equal(c.rawMidiReceipt.sequence,2);
});
test('raw and snapshot frames preserve order and share one increasing sequence',async()=>{
  const {d,c}=await connected();assert.equal(typeof c.rawMidi,'function');
  c.keys(1);c.rawMidi([176,64,127]);c.keys(0);await settle(c);
  assert.deepEqual(d.log.filter(e=>e.stage==='received'||e.stage==='midi-queued').map(e=>[e.sequence,e.stage,e.a??e.bytes]),[[1,'received',0],[2,'received',1],[3,'midi-queued',[176,64,127]],[4,'received',0]]);
});
test('raw frames between snapshots cannot coalesce away an Off and re-press',async()=>{
  const {d,c}=await connected();d.holdRaw=true;c.rawMidi([224,0,32]);
  for(let i=0;i<30&&!d.rawGate;i++)await turn();assert.ok(d.rawGate);
  c.keys(1);c.rawMidi([176,64,127]);c.keys(0);c.keys(1);d.rawGate();await settle(c);
  assert.deepEqual(d.commands().filter(e=>e.op===3).map(e=>e.a),[0,1,0,1]);
  assert.deepEqual(bodies(d).map(b=>b.bytes),[[224,0,32],[176,64,127]]);
});
test('raw input rejects invalid bytes and closes during recovery or legacy without sending',async()=>{
  const {d,c}=await connected();assert.equal(typeof c.rawMidi,'function');
  for(const bytes of [[],[-1],[256],Array(257).fill(0)])assert.equal(c.rawMidi(bytes),false);
  d.state.mode.recovering=true;await c.poll();await settle(c);assert.equal(c.rawMidi([144,60,100]),false);assert.equal(d.rawRequests.length,0);
  const legacy=new RawMidiDevice();legacy.missing=404;const v1=new RuntimeController(legacy.transport,{now:()=>legacy.time,delay:ms=>legacy.delay(ms)});await v1.connect();assert.equal(v1.rawMidi([144,60,100]),false);
});
for(const status of [400,409,503])test('raw HTTP '+status+' releases binding without replay on reconnect',async()=>{
  const {d,c}=await connected();assert.equal(typeof c.rawMidi,'function');d.rawError=status;c.rawMidi([144,60,100]);await settle(c);
  assert.equal(c.status,'offline');assert.equal(c.desiredMask,0);assert.equal(c.rawMidiReceipt,null);assert.equal(d.rawRequests.length,1);
  d.rawError=null;await c.connect();await settle(c);assert.equal(c.status,'online');assert.equal(d.rawRequests.length,1);
});
test('Panic discards queued raw On and fences a late raw receipt',async()=>{
  const {d,c}=await connected();assert.equal(typeof c.rawMidi,'function');d.holdRaw=true;c.rawMidi([224,0,32]);
  for(let i=0;i<30&&!d.rawGate;i++)await turn();assert.ok(d.rawGate);c.rawMidi([144,60,100]);c.panic();await settle(c);
  const token=c.token;d.rawGate();await turn();await settle(c);
  assert.equal(c.token,token);assert.equal(c.status,'online');assert.equal(c.rawMidiReceipt,null);assert.equal(d.rawRequests.length,1);assert.ok(d.commands().some(e=>e.op===4));
});
test('background and new context clear raw intent and never replay it in a fresh session',async()=>{
  const {d,c}=await connected();assert.equal(typeof c.rawMidi,'function');c.rawMidi([144,60,100]);await settle(c);const token=c.token;
  c.suspend();d.moveContext();await c.resume();await settle(c);
  assert.notEqual(c.token,token);assert.equal(c.rawMidiReceipt,null);assert.equal(d.rawRequests.length,1);assert.equal(c.desiredMask,0);
});
test('raw queue overflow cancels the binding rather than silently losing a release',async()=>{
  const {d,c}=await connected();assert.equal(typeof c.rawMidi,'function');d.holdRaw=true;c.rawMidi([144,60,100]);
  for(let i=0;i<30&&!d.rawGate;i++)await turn();assert.ok(d.rawGate);
  for(let i=0;i<32;i++)assert.equal(c.rawMidi([128,60,0]),true);
  assert.equal(c.rawMidi([144,60,100]),false);assert.equal(c.status,'offline');d.rawGate();await turn();assert.equal(d.rawRequests.length,1);assert.equal(c.queued,0);
});
test('keyboard-triggered mixed queue overflow cancels binding and releases raw source',async()=>{
  const {d,c}=await connected();d.holdRaw=true;c.rawMidi([144,60,100]);
  for(let i=0;i<30&&!d.rawGate;i++)await turn();assert.ok(d.rawGate);
  for(let i=0;i<32;i++)assert.equal(c.rawMidi([128,60,0]),true);
  assert.equal(c.keys(1),false);assert.equal(c.status,'offline');assert.equal(c.token,0);
  assert.equal(c.desiredMask,0);assert.equal(c.queued,0);assert.ok(d.requests.some(r=>r.path==='release'));
  d.rawGate();await turn();await settle(c);assert.equal(d.rawRequests.length,1);assert.equal(c.rawMidiReceipt,null);
});
test('raw endpoint requires HTTP 202 even if another success response carries accepted=true',async()=>{
  const t=createRuntimeTransport(async()=>({ok:true,status:200,json:async()=>({accepted:true,queued_bytes:3})}));
  await assert.rejects(t.request('/api/v1/midi/raw',{method:'POST'},202));
});
test('queued raw bytes are immutable while an earlier request is pending',async()=>{
  const {d,c}=await connected();d.holdRaw=true;c.rawMidi([224,0,32]);
  for(let i=0;i<30&&!d.rawGate;i++)await turn();const bytes=[159,60,0];c.rawMidi(bytes);bytes[2]=127;d.rawGate();await settle(c);
  assert.deepEqual(bodies(d).map(b=>b.bytes),[[224,0,32],[159,60,0]]);
});
test('expired raw work cancels the source binding instead of dropping Off and continuing',async()=>{
  const {d,c}=await connected();d.holdRaw=true;c.rawMidi([144,60,100]);
  for(let i=0;i<30&&!d.rawGate;i++)await turn();c.rawMidi([128,60,0]);d.time+=301;d.rawGate();await settle(c);
  assert.equal(c.status,'offline');assert.equal(c.queued,0);assert.match(c.error,/expired/);assert.equal(d.rawRequests.length,1);
});
test('a raw queue-full HTTP response without JSON still gives an explicit reason',async()=>{
  const {d,c}=await connected(),request=d.transport.request;
  d.transport.request=(path,options)=>path==='/api/v1/midi/raw'?Promise.reject(Object.assign(new Error('HTTP 503'),{status:503})):request(path,options);
  c.rawMidi([144,60,100]);await settle(c);assert.equal(c.status,'offline');assert.equal(c.error,'midi_queue_full');
});
