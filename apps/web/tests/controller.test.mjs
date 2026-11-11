import test from 'node:test';
import assert from 'node:assert/strict';
import {SynthController} from '../src/controller.mjs';
import {HttpError,decodeCommand,encodeCommand} from '../src/protocol.mjs';
import {MockDevice,initialState} from '../mock/device.mjs';

const turn=()=>new Promise(resolve=>setImmediate(resolve));
async function settle(controller){for(let i=0;i<200;i++){await turn();if(!controller.running)return;}throw Error('controller did not settle');}
function fixture(){
  const f={time:10,requests:[],hold:null,clearCount:0};
  f.device=new MockDevice({now:()=>f.time});
  const transport={
    async request(path,options={}){
      f.requests.push({path,options});
      if(f.failPath===path)throw Error('network failure');
      const result=path==='/api/state'?{status:200,body:f.device.snapshot()}:path==='/api/session'?f.device.session(options.body,options.headers?.['X-FPGA-Control']):f.device.command(options.body,options.headers?.['X-FPGA-Control']);
      if(result.status>=400)throw new HttpError(result.status);
      if(path==='/api/command'&&(f.holdNext||f.holdWhen?.(decodeCommand(options.body)))){f.holdNext=false;f.holdWhen=null;await new Promise(resolve=>f.hold=resolve);}
      return result.body;
    },
    release(body){f.requests.push({path:'release',body});f.device.command(body,'1');return Promise.resolve();}
  };
  f.controller=new SynthController(transport,{now:()=>f.time,delay:async ms=>{f.time+=ms;await turn();},onClear:()=>f.clearCount++});
  f.commands=()=>f.device.log.filter(event=>event.stage==='received');
  return f;
}
async function connected(){const f=fixture();await f.controller.connect();assert.equal(f.controller.status,'online');return f;}

test('connect reads patch and only binds with zero bitmap; no default parameter writes',async()=>{
  const f=await connected();assert.deepEqual(f.commands().map(c=>[c.op,c.a]),[[3,0]]);assert.equal(f.controller.state.volume,207);assert(f.requests.findIndex(r=>r.path==='/api/state')<f.requests.findIndex(r=>r.path==='/api/session'));assert.equal(f.controller.lastAdopted,1);
});
test('short On/Off survives busy HTTP and waits for owner adoption before Off',async()=>{
  const f=await connected();f.holdNext=true;f.controller.keys(1<<12);f.controller.keys(0);await turn();
  assert.equal(f.commands().at(-1).a,1<<12);assert.equal(f.controller.lastAdopted,1);assert.equal(f.controller.frames.length,1);
  f.hold();await settle(f.controller);
  assert.deepEqual(f.commands().slice(-2).map(c=>c.a),[1<<12,0]);
  const owner=f.device.log.findIndex(e=>e.stage==='owner'&&e.op===3&&e.a===1<<12);
  const off=f.device.log.findIndex(e=>e.stage==='received'&&e.op===3&&e.a===0&&e.sequence>2);assert(owner<off);assert.equal(f.device.snapshot().keys[0],0);
});
test('parameter, bitmap, mode, decay and panic use one increasing sequence',async()=>{
  const f=await connected();const c=f.controller;c.cc(7,40);await settle(c);c.keys(3);await settle(c);c.keys(0);await settle(c);c.mode(1);await settle(c);c.decay(200);await settle(c);c.panic();await settle(c);
  assert.equal(c.status,'online');assert.equal(c.state.ks_decay_ms,200);assert.equal(c.state.volume,80);
  const commands=f.commands();assert.deepEqual(commands.map(e=>e.sequence),commands.map((_,i)=>i+1));assert(commands.some(e=>e.op===4));assert.equal(c.state.fpga,'unverified');
});
test('queued nonzero older than 300 ms is discarded and safe zero re-arms',async()=>{
  const f=await connected();f.holdNext=true;f.controller.keys(1);await turn();f.controller.keys(3);f.time+=400;f.hold();await settle(f.controller);
  assert.deepEqual(f.commands().slice(-2).map(e=>e.a),[1,0]);assert.equal(f.controller.desiredMask,0);assert.equal(f.device.snapshot().keys[0],0);
});
test('queue limit forces release instead of replaying stale notes',async()=>{
  const f=await connected();f.holdNext=true;f.controller.keys(1);await turn();for(let i=0;i<33;i++)f.controller.keys(i%2?1:3);
  assert(f.clearCount>=2);f.hold();await settle(f.controller);assert.equal(f.controller.desiredMask,0);assert.equal(f.device.snapshot().keys[0],0);
});
test('250 ms heartbeats renew the 1.2 s lease; poll alone does not',async()=>{
  const f=await connected();f.controller.keys(1);await settle(f.controller);const start=f.time;
  for(let i=0;i<6;i++){f.time+=250;f.controller.tick();await settle(f.controller);}assert.equal(f.device.snapshot().keys[0],1);assert(f.time-start>1200);
  f.time+=1201;assert.equal(f.device.snapshot().keys[0],0);await f.controller.poll();await settle(f.controller);assert.equal(f.controller.desiredMask,0);assert.equal(f.device.snapshot().keys[0],0);
});
test('hidden/blur/pagehide suspension sends higher-sequence keepalive zero and clears queue',async()=>{
  const f=await connected();f.holdNext=true;f.controller.keys(1);await turn();f.controller.keys(3);f.controller.suspend();
  const release=f.requests.find(r=>r.path==='release');assert(release);const parsed=decodeCommand(release.body);assert.equal(parsed.a,0);assert(parsed.sequence>f.commands().find(e=>e.a===1).sequence);
  f.hold();await settle(f.controller);assert.equal(f.controller.status,'paused');assert.equal(f.controller.desiredMask,0);assert.equal(f.commands().filter(e=>e.a===3).length,0);
  f.controller.resume();await settle(f.controller);assert.equal(f.controller.status,'online');assert.equal(f.device.snapshot().keys[0],0);assert.equal(f.controller.desiredMask,0);
});
test('ambiguous HTTP timeout releases once and never replays old On after reconnect',async()=>{
  const f=await connected();f.holdNext=true;f.controller.keys(1);await turn();f.controller.keys(3);
  f.controller.fail(new Error('timeout after receive'));f.hold();await settle(f.controller);assert.equal(f.controller.status,'offline');assert.equal(f.controller.token,0);
  await f.controller.connect();assert.equal(f.controller.status,'online');assert.equal(f.commands().filter(c=>c.a===3&&c.op===3).length,0);assert(f.commands().filter(c=>c.op===3&&c.a===1).length===1);assert.equal(f.controller.desiredMask,0);
});
test('owner acknowledgement timeout and token replacement stop client without a duplicate On',async()=>{
  const f=await connected();f.device.ownerDelay=1000;f.controller.keys(2);await settle(f.controller);assert.equal(f.controller.status,'offline');assert.equal(f.commands().filter(e=>e.op===3&&e.a===2).length,1);
  const g=await connected();g.device.state.tokens[0]=987;await g.controller.poll();assert.equal(g.controller.status,'offline');assert.equal(g.controller.desiredMask,0);
});
test('capability missing, switching, recovery and external panic gate fresh input',async()=>{
  const f=await connected();const c=f.controller;f.device.configure({available:false});await c.poll();assert.equal(c.mode(1),false);assert.equal(c.playable,true);
  f.device.configure({available:true});await c.poll();c.keys(3);await settle(c);f.device.configure({recovering:true});await c.poll();assert.equal(c.playable,false);assert.equal(c.desiredMask,0);assert.equal(c.keys(7),false);await settle(c);
  f.device.configure({recovering:false});await c.poll();await settle(c);assert.equal(c.playable,true);assert.equal(c.desiredMask,0);
  c.keys(1);await settle(c);f.device.configure({panic:true});await c.poll();await settle(c);assert.equal(c.desiredMask,0);assert.equal(f.device.snapshot().keys[0],0);
});
test('own release preserves other source; panic clears all sources and diagnostic tone',async()=>{
  const f=await connected();const other=f.device.session('','1').body;f.device.command(encodeCommand(other.token,1,3,1,90),'1');f.time+=25;
  f.controller.keys(1);await settle(f.controller);f.controller.releaseOwn();await settle(f.controller);assert.deepEqual(f.device.snapshot().keys.slice(0,2),[0,1]);
  f.device.externalNotes=[60];f.device.state.test_tone=true;f.controller.panic();await settle(f.controller);assert.deepEqual(f.device.snapshot().keys,[0,0,0,0]);assert.deepEqual(f.device.externalNotes,[]);assert.equal(f.device.state.test_tone,false);
});
test('KS restricts parameters and switch readback clears held input without default writes',async()=>{
  const f=await connected();const c=f.controller;c.keys(1);await settle(c);c.mode(1);assert.equal(c.playable,false);assert.equal(c.keys(3),false);await settle(c);
  assert.equal(c.playable,true);assert.equal(c.desiredMask,0);assert.equal(c.state.engine.applied,1);assert.equal(c.cc(74,100),false);assert.equal(c.cc(7,50),true);await settle(c);assert.equal(c.state.volume,100);
  assert.equal(c.decay(8000),true);await settle(c);assert.equal(c.state.ks_decay_ms,8000);c.mode(0);await settle(c);assert.deepEqual(c.state.osc,[[0,0,0,0],[3,-12,0,0]]);
});
test('mode waits for own empty bitmap adoption and drops queued notes and parameters',async()=>{
  const f=await connected();const c=f.controller;f.holdNext=true;c.keys(1);await turn();
  c.keys(3);c.cc(7,12);assert.equal(c.mode(1),true);assert.equal(c.keys(7),false);
  f.hold();await settle(c);
  const mode=f.device.log.findIndex(e=>e.stage==='received'&&e.op===5);
  const release=f.device.log.findIndex(e=>e.stage==='owner'&&e.op===3&&e.a===0&&e.sequence>1);
  assert(release>=0&&release<mode,'the owner must adopt this session release before a mode command is published');
  assert.equal(f.commands().filter(e=>e.op===3&&e.a===3).length,0);
  assert.equal(f.commands().filter(e=>e.op===2).length,0);
  assert.equal(c.playable,true);assert.equal(c.desiredMask,0);
});
test('release adoption timeout prevents publishing an engine switch',async()=>{
  const f=await connected();f.controller.keys(1);await settle(f.controller);
  f.device.ownerDelay=1000;f.controller.mode(1);await settle(f.controller);
  assert.equal(f.controller.status,'offline');assert.equal(f.controller.playable,false);
  assert.equal(f.commands().filter(e=>e.op===5).length,0);
  assert(f.requests.some(r=>r.path==='release'));
});
for(const action of ['panic','suspend'])test(`${action} cancels a switch while its prerequisite release is pending`,async()=>{
  const f=await connected();f.holdNext=true;f.controller.mode(1);await turn();
  const prerequisite=f.commands().at(-1);f.controller[action]();f.hold();await settle(f.controller);
  assert.deepEqual([prerequisite.op,prerequisite.a],[3,0]);
  assert.equal(f.commands().filter(e=>e.op===5).length,0);
  assert.equal(f.controller.desiredMask,0);
  if(action==='panic')assert(f.commands().some(e=>e.op===4));
  else assert.equal(f.controller.status,'paused');
});
test('late prerequisite zero adoption cannot unlock a newer Panic before stop adoption',async()=>{
  const f=await connected();const c=f.controller;f.holdNext=true;c.mode(1);await turn();
  c.panic();f.holdWhen=command=>command.op===4;
  const releasePrerequisite=f.hold;f.hold=null;releasePrerequisite();
  for(let i=0;i<200&&!f.hold;i++)await turn();assert(f.hold,'Panic must follow the prior release');
  try{assert.equal(c.state.stop_processed[0],0);assert.equal(c.playable,false);assert.equal(c.keys(1),false);}
  finally{f.hold();await settle(c);}
  assert.equal(c.playable,true);assert.equal(c.desiredMask,0);
});
test('mode completion keeps fresh input gated until a second empty bitmap is adopted',async()=>{
  const f=await connected();const c=f.controller;
  f.holdWhen=command=>command.op===3&&command.a===0&&f.commands().some(e=>e.op===5);
  c.mode(1);for(let i=0;i<200&&!f.hold;i++)await turn();
  assert(f.hold,'the post-switch empty bitmap must be sent');
  try{assert.equal(c.state.engine.applied,1);assert.equal(c.playable,false);assert.equal(c.keys(1),false);}
  finally{f.hold();await settle(c);}
  assert.equal(c.playable,true);assert.equal(c.desiredMask,0);
});
test('editing coalesces latest pending parameter while prior receipt is busy',async()=>{
  const f=await connected();f.holdNext=true;f.controller.cc(7,1);await turn();f.controller.cc(7,2);f.controller.cc(7,3);assert.equal(f.controller.pending.get(7),3);f.hold();await settle(f.controller);
  assert.deepEqual(f.commands().filter(e=>e.op===2).map(e=>e.b),[1,3]);assert.equal(f.controller.pending.size,0);assert.equal(f.controller.state.volume,6);
});
test('Panic holds new input until stop adoption and empty re-arm are acknowledged',async()=>{
  const f=await connected();f.holdNext=true;f.controller.panic();await turn();assert.equal(f.controller.playable,false);assert.equal(f.controller.keys(1),false);f.hold();await settle(f.controller);
  assert.equal(f.controller.playable,true);assert.equal(f.controller.desiredMask,0);assert.deepEqual(f.commands().slice(-2).map(e=>[e.op,e.a]),[[4,0],[3,0]]);
});
test('device restart loads fresh patch despite lower revision and never restores old contacts',async()=>{
  const f=await connected();f.controller.state.revision=99999;f.controller.fail(new Error('restart'));f.device.state=initialState();f.device.state.volume=40;f.device.sessions.forEach(s=>s.seen=-Infinity);f.device.events=[];
  await f.controller.connect();assert.equal(f.controller.status,'online');assert.equal(f.controller.state.volume,40);assert.equal(f.controller.desiredMask,0);assert.equal(f.controller.state.keys[f.controller.client],0);
});
test('revision wrap is accepted while older snapshots are ignored',async()=>{
  const f=await connected();f.controller.state.revision=4294967294;const wrap=f.device.snapshot();wrap.revision=1;wrap.pan=12;f.controller.observe(wrap);assert.equal(f.controller.state.pan,12);
  const stale=structuredClone(wrap);stale.revision=0;stale.pan=-20;f.controller.observe(stale);assert.equal(f.controller.state.pan,12);
});
test('stuck engine mode readback disables the session and releases instead of allowing new notes',async()=>{
  const f=await connected();f.device.modeDelay=10000;f.controller.mode(1);await settle(f.controller);assert.equal(f.controller.status,'offline');assert.equal(f.controller.playable,false);assert.equal(f.controller.desiredMask,0);assert(f.requests.some(r=>r.path==='release'));
});
test('session full, invalid header, stale sequence and expired token are explicit errors',async()=>{
  const f=fixture();for(let i=0;i<4;i++)f.device.session('','1');await f.controller.connect();assert.equal(f.controller.status,'offline');assert.match(f.controller.message,/四个控制席位/);
  const session=f.device.sessions[0];assert.equal(f.device.command(encodeCommand(session.token,1,3,0,100)).status,403);assert.equal(f.device.command(encodeCommand(session.token,1,3,0,100),'1').status,202);assert.equal(f.device.command(encodeCommand(session.token,1,3,0,100),'1').status,409);f.time+=10001;assert.equal(f.device.command(encodeCommand(session.token,2,3,0,100),'1').status,410);
});
