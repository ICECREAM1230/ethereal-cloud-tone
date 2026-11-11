import test from 'node:test';
import assert from 'node:assert/strict';
import {RuntimeController} from '../src/runtime-controller.mjs';
import {ReverbDevice} from './helpers/reverb-device.mjs';
import {settle,turn} from './helpers/runtime-device.mjs';

async function connected(setup=()=>{}){
  const d=new ReverbDevice();setup(d);
  const c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms)});
  await c.connect();
  assert.equal(typeof c.pollReverb,'function','reverb capability/readback must be exposed to the page');
  await c.pollReverb();return {d,c};
}
async function dispatch(d,c){d.time+=160;c.tick();await settle(c);}
test('the ordinary page tick discovers reverb without a separate manual API call',async()=>{
  const d=new ReverbDevice(),c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms)});
  await c.connect();c.tick();await settle(c);await turn();
  assert.ok(d.reverbReads>0,'background capability probe must run when the musical queue is idle');
  assert.equal(c.reverbState.writable,true);
});
test('a read started before POST cannot postpone the new request confirmation probe',async()=>{
  const {d,c}=await connected();d.holdReverbRead=true;const poll=c.pollReverb();
  for(let i=0;i<30&&!d.reverbReadGate;i++)await turn();
  c.queueReverb(16000,8000);await dispatch(d,c);d.reverbReadGate();await poll;
  c.tick();await settle(c);await turn();assert.ok(d.reverbReads>=3);
});
test('initial read displays the actual pair without uploading any defaults',async()=>{
  const {d,c}=await connected(d=>{d.effect.wet_q15=0;d.effect.damp_q15=28672;});
  assert.equal(c.reverbState.writable,true);assert.equal(c.reverbState.wet_q15,0);assert.equal(c.reverbState.damp_q15,28672);
  assert.equal(d.reverbPosts.length,0);assert.equal(c.playable,true);
});
test('dragging coalesces into one exact pair sharing the musical session sequence',async()=>{
  const {d,c}=await connected();
  assert.equal(c.queueReverb(16384,8192),true);assert.equal(c.queueReverb(20000,9000),true);
  await settle(c);assert.equal(d.reverbPosts.length,0);
  await dispatch(d,c);assert.equal(d.reverbPosts.length,1);assert.equal(d.reverbPosts[0].body,'100 2 10 20000 9000');
  assert.equal(d.reverbPosts[0].headers['X-FPGA-Control'],'1');assert.equal(c.reverbState.phase,'pending');
  assert.notEqual(c.reverbState.phase,'confirmed');
  d.time+=450;await c.pollReverb();assert.equal(c.reverbState.phase,'confirmed');
  assert.equal(c.reverbState.wet_q15,20000);assert.equal(c.playable,true);
});
test('pending native acknowledgement does not delay notes, Off or the 250ms heartbeat',async()=>{
  const {d,c}=await connected();d.reverbDelay=900;
  c.queueReverb(16000,8000);await dispatch(d,c);
  c.keys(1);await settle(c);const at=d.time;
  d.time+=250;c.tick();await settle(c);
  assert.ok(d.commands().some(e=>e.op===3&&e.a===1&&e.at>=at+250));
  c.keys(0);await settle(c);assert.equal(d.commands().at(-1).a,0);assert.equal(c.reverbState.phase,'pending');
  assert.equal(c.status,'online');assert.equal(d.reverbPosts.length,1);
});
test('a slow reverb HTTP receipt cannot block the musical heartbeat or ordinary Off',async()=>{
  const {d,c}=await connected();c.keys(1);await settle(c);d.holdReverbPost=true;
  c.queueReverb(16000,8000);d.time+=160;c.tick();
  for(let i=0;i<30&&!d.reverbPostGate;i++)await turn();assert.ok(d.reverbPostGate);
  try{
    // A later browser timer fires on a new turn, after an idle drain settles.
    await turn();
    const count=d.commands().length;d.time+=250;c.tick();
    for(let i=0;i<30&&d.commands().length===count;i++)await turn();
    assert.ok(d.commands().length>count,'heartbeat must be sent before the old reverb HTTP receipt returns');
    c.keys(0);for(let i=0;i<30&&d.commands().at(-1).a!==0;i++)await turn();
    assert.equal(d.commands().at(-1).a,0,'Off must be sent before that receipt returns');
  }finally{d.reverbPostGate();await turn();await settle(c);}
});
test('an unsent draft pauses when a foreign request owns the effect instead of spinning',async()=>{
  const {d,c}=await connected();c.queueReverb(16000,8000);d.effect.pending=true;await c.pollReverb();d.time+=160;
  assert.equal(c.reverbState.writable,false);assert.equal(c.reverbDue(),false,'unwritable work cannot be selected by the drain loop');
  c.tick();await settle(c);assert.equal(d.reverbPosts.length,0);assert.equal(c.status,'online');
});
test('an unsent draft pauses after a failed state read instead of spinning',async()=>{
  const {d,c}=await connected();c.queueReverb(16000,8000);d.effect.valid='invalid';await c.pollReverb();d.time+=160;
  assert.equal(c.reverbState.writable,false);assert.equal(c.reverbDue(),false);
  c.tick();await settle(c);assert.equal(d.reverbPosts.length,0);assert.equal(c.status,'online');
});
test('a newer musical sequence can supersede a delayed effect without replay or disconnect',async()=>{
  const {d,c}=await connected(),request=d.transport.request;let gate;
  d.transport.request=async(path,options,expected)=>{
    if(path==='/api/v2/effects/reverb'&&options?.method==='POST')await new Promise(resolve=>gate=resolve);
    return request(path,options,expected);
  };
  c.queueReverb(16000,8000);d.time+=160;c.tick();
  for(let i=0;i<30&&!gate;i++)await turn();assert.ok(gate);
  c.keys(1);for(let i=0;i<30&&d.commands().at(-1).a!==1;i++)await turn();
  assert.equal(d.commands().at(-1).a,1);gate();await turn();await settle(c);
  assert.equal(c.status,'online');assert.equal(c.reverbState.phase,'error');assert.equal(d.reverbPosts.length,0);
  c.tick();await settle(c);assert.equal(d.reverbPosts.length,0);
});
test('native completion observed before the HTTP receipt still cannot report success',async()=>{
  const {d,c}=await connected();d.holdReverbPost=true;c.queueReverb(16000,8000);d.time+=160;c.tick();
  for(let i=0;i<30&&!d.reverbPostGate;i++)await turn();
  d.time+=450;await c.pollReverb();assert.notEqual(c.reverbState.phase,'confirmed');
  d.reverbPostGate();await turn();await c.pollReverb();assert.equal(c.reverbState.phase,'confirmed');
});
test('a late receipt cannot revive an expired request or overwrite a later draft',async()=>{
  const {d,c}=await connected();d.holdReverbPost=true;c.queueReverb(16000,8000);d.time+=160;c.tick();
  for(let i=0;i<30&&!d.reverbPostGate;i++)await turn();
  d.time+=1300;await c.pollReverb();assert.equal(c.reverbState.phase,'error');
  assert.equal(c.queueReverb(5000,6000),true);d.reverbPostGate();await turn();
  assert.equal(c.reverbState.phase,'queued');assert.equal(c.reverbState.wet_q15,5000);
  await dispatch(d,c);assert.equal(d.reverbPosts.length,2);
});
test('a slow confirmation keeps only the newest unsent adjustment',async()=>{
  const {d,c}=await connected();c.queueReverb(1000,2000);await dispatch(d,c);
  c.queueReverb(3000,4000);c.queueReverb(5000,6000);await dispatch(d,c);assert.equal(d.reverbPosts.length,1);
  d.time+=450;await c.pollReverb();await dispatch(d,c);
  assert.equal(d.reverbPosts.length,2);assert.match(d.reverbPosts[1].body,/ 5000 6000$/);
});
test('unsupported and old HTTP endpoints disable only reverb and never write',async()=>{
  for(const setup of [d=>{d.reverbMissing=true;},d=>{d.effect.available=false;d.effect.valid=false;d.effect.wet_q15=d.effect.damp_q15=null;}]){
    const {d,c}=await connected(setup);assert.equal(c.reverbState.writable,false);assert.equal(c.queueReverb(1,2),false);
    assert.equal(c.status,'online');c.keys(1);await settle(c);assert.equal(d.commands().at(-1).a,1);assert.equal(d.reverbPosts.length,0);
  }
});
test('a missing acknowledgement times out without replay or disconnecting the synthesizer',async()=>{
  const {d,c}=await connected();d.noReverbAck=true;c.queueReverb(16000,8000);await dispatch(d,c);
  d.time+=1600;await c.pollReverb();assert.equal(c.reverbState.phase,'error');assert.match(c.reverbState.message,/超时/);
  c.tick();await settle(c);assert.equal(d.reverbPosts.length,1);assert.equal(c.status,'online');assert.equal(c.reverbState.writable,false);
});
test('matching values with a foreign identity cannot confirm this request',async()=>{
  const {d,c}=await connected();c.queueReverb(16000,8000);await dispatch(d,c);d.time+=450;
  d.flush();d.effect.applied.token++;await c.pollReverb();assert.notEqual(c.reverbState.phase,'confirmed');
});
test('mismatched native sequence or parameter pair cannot confirm this request',async()=>{
  for(const corrupt of [d=>d.effect.applied.applied_sequence++,d=>d.effect.damp_q15++]){
    const {d,c}=await connected();c.queueReverb(16000,8000);await dispatch(d,c);d.time+=450;d.flush();corrupt(d);
    await c.pollReverb();assert.notEqual(c.reverbState.phase,'confirmed');
  }
});
test('a stale read cannot restore reverb controls after a context rebind',async()=>{
  const {d,c}=await connected();d.holdReverbRead=true;const poll=c.pollReverb();
  for(let i=0;i<30&&!d.reverbReadGate;i++)await turn();assert.ok(d.reverbReadGate);
  d.moveContext();await c.poll();await settle(c);d.reverbReadGate();await poll;
  assert.notEqual(c.token,100);assert.equal(c.queueReverb(1,2),false);assert.equal(d.reverbPosts.length,0);
});
test('STOP discards a queued adjustment before dispatch and remains the next operation',async()=>{
  const {d,c}=await connected();c.queueReverb(16000,8000);c.panic();await settle(c);
  assert.ok(d.commands().some(e=>e.op===4));assert.equal(d.reverbPosts.length,0);
});
test('a late HTTP receipt after suspension cannot restore the cancelled request',async()=>{
  const {d,c}=await connected();d.holdReverbPost=true;c.queueReverb(16000,8000);d.time+=160;c.tick();
  for(let i=0;i<30&&!d.reverbPostGate;i++)await turn();assert.ok(d.reverbPostGate);
  c.suspend();d.reverbPostGate();await turn();await settle(c);
  assert.equal(c.status,'paused');assert.equal(c.reverbState.writable,false);assert.notEqual(c.reverbState.phase,'confirmed');
});
test('a backend-only rejection keeps other synth controls online and never retries the payload',async()=>{
  const {d,c}=await connected();d.reverbError=503;c.queueReverb(16000,8000);await dispatch(d,c);
  assert.equal(c.reverbState.phase,'error');assert.equal(c.status,'online');assert.equal(c.playable,true);
  c.tick();await settle(c);assert.equal(d.reverbPosts.length,0);
});
test('invalid Q15 values are rejected without a request',async()=>{
  const {d,c}=await connected();
  for(const pair of [[-1,0],[32769,0],[0,28673],[1.5,0],['1',2],[NaN,2]])assert.equal(c.queueReverb(...pair),false);
  await dispatch(d,c);assert.equal(d.reverbPosts.length,0);
});
