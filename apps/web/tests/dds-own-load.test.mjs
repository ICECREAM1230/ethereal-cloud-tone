import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {RuntimeController} from '../src/runtime-controller.mjs';
import {RuntimeDevice,settle,turn} from './helpers/runtime-device.mjs';

// Owner model: channel routing does not change context; new sessions start KS.
// A successful DDS load calls all_sound_off once, changing context AND panic.
class LoadDevice extends RuntimeDevice {
  constructor(){
    super();
    for(const [field,name] of [['caps','capabilities'],['state','state']])
      this[field]=JSON.parse(readFileSync(new URL('./fixtures/fm32/'+name+'.json',import.meta.url)));
  }
  async request(path,options={}){
    const result=await super.request(path,options);
    if(path==='/api/v2/session')this.state.clients[result.client].midi_channel=1;
    return result;
  }
  adopt(command,session){
    if(command.op!==5)return super.adopt(command,session);
    const slot=this.state.clients[session.client];
    Object.assign(slot,{token:command.token,result_sequence:command.sequence,result_code:0,
      result_context_epoch:command.context,parameter_processed:command.sequence,midi_channel:command.a});
    this.log.push({...command,stage:'owner',code:0});
  }
  load(){this.moveContext();this.state.legacy.panic=(this.state.legacy.panic+1)>>>0;return this.state.context_epoch;}
}
async function ready(){
  const d=new LoadDevice(),c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms)});
  await c.connect();assert.equal(c.webMidiChannel,1);assert.equal(c.mode(0),true);await settle(c);
  return {d,c};
}
test('own LOAD retires routine polls and old idle heartbeat before new context is available',async()=>{
  const {d,c}=await ready();c.keys(1);await settle(c);
  assert.equal(await c.beginDdsPresetLoad(),true);const ticket=c.ddsPresetLoadTicket;
  const commands=d.commands().length,sessions=d.requests.filter(x=>x.path==='/api/v2/session').length;
  const expected=d.load();d.time+=2500;c.tick();await c.poll();await settle(c);
  assert.equal(d.commands().length,commands,'old-context heartbeat must not run in exclusive wait');
  assert.equal(d.requests.filter(x=>x.path==='/api/v2/session').length,sessions,'routine poll must not bind default KS');
  assert.equal(c.ddsPresetLoadTicket,ticket);assert.equal(c.playable,false);
  await c.endDdsPresetLoad(ticket,expected);
  assert.equal(c.status,'online');assert.equal(c.webMidiChannel,0);assert.equal(c.playable,true);
  assert.equal(c.desiredMask,0);assert.equal(c.pending.size,0);
  const fresh=d.commands().slice(commands);
  assert.deepEqual(fresh.map(x=>[x.op,x.a]),[[3,0],[5,0]],'only fresh empty and explicit DDS metadata');
  assert.equal(fresh[1].context,expected);assert.equal(fresh[0].token,fresh[1].token);
  assert.equal(d.commands().filter(x=>x.op===3&&x.a).length,1,'held contact is not replayed');
  await c.endDdsPresetLoad(ticket,expected);assert.equal(d.commands().length,commands+2,'ticket is consumed once');
});
test('matching owner UNSUPPORTED CC preserves current DDS session and explicitly rejects parameter',async()=>{
  const {d,c}=await ready();const token=c.token,epoch=c.epoch,sessions=d.requests.filter(x=>x.path==='/api/v2/session').length;
  d.ownerCode=2;assert.equal(c.cc(73,31),true);await settle(c);
  assert.equal(c.status,'online');assert.equal(c.token,token);assert.equal(c.epoch,epoch);
  assert.equal(c.webMidiChannel,0);assert.equal(c.pending.has(73),false);assert.match(c.message,/未采用|不支持|拒绝/);
  d.time+=100;await c.poll();await settle(c);
  assert.equal(d.requests.filter(x=>x.path==='/api/v2/session').length,sessions);
  assert.equal(d.commands().filter(x=>x.op===2).length,1,'unsupported CC is not retried');
});
test('cancelled or failed LOAD stays fenced without binding or restoring DDS',async()=>{
  const {d,c}=await ready();await c.beginDdsPresetLoad();const ticket=c.ddsPresetLoadTicket;
  const commands=d.commands().length,sessions=d.requests.filter(x=>x.path==='/api/v2/session').length;
  await c.endDdsPresetLoad(ticket);d.time+=2500;c.tick();await c.poll();await settle(c);
  assert.equal(c.playable,false);assert.equal(c.desiredMask,0);assert.equal(c.keys(1),false);
  assert.equal(d.commands().length,commands);assert.equal(d.requests.filter(x=>x.path==='/api/v2/session').length,sessions);
});
for(const kind of ['extra context','extra panic','native identity','capability identity','source token','source route','source keys'])test('own LOAD rejects '+kind+' during completion',async()=>{
  const {d,c}=await ready();await c.beginDdsPresetLoad();const ticket=c.ddsPresetLoadTicket,expected=d.load();
  if(kind==='extra context')d.moveContext();if(kind==='extra panic')d.state.legacy.panic++;
  if(kind==='native identity')d.state.fm.session_id++;if(kind==='capability identity')d.caps.schema_sha256='different';
  if(kind==='source token')d.state.clients[c.client].token=777;
  if(kind==='source route')d.state.clients[c.client].midi_channel=1;
  if(kind==='source keys')d.state.clients[c.client].keys=2;
  const sessions=d.requests.filter(x=>x.path==='/api/v2/session').length,selects=d.commands().filter(x=>x.op===5).length;
  assert.equal(await c.endDdsPresetLoad(ticket,expected),false);assert.equal(c.playable,false);
  assert.equal(d.requests.filter(x=>x.path==='/api/v2/session').length,sessions);
  assert.equal(d.commands().filter(x=>x.op===5).length,selects);
});
for(const action of ['suspend','panic'])test(action+' retires a LOAD ticket before completion',async()=>{
  const {d,c}=await ready();await c.beginDdsPresetLoad();const ticket=c.ddsPresetLoadTicket;const expected=d.load();
  c[action]();await settle(c);const commands=d.commands().length;
  assert.equal(await c.endDdsPresetLoad(ticket,expected),false);await settle(c);
  assert.equal(d.commands().length,commands);assert.equal(c.desiredMask,0);
});
test('a poll begun before LOAD preparation cannot allocate a default KS session later',async()=>{
  const {d,c}=await ready();d.holdState=true;const poll=c.poll();await turn();const gate=d.stateGate;
  await c.beginDdsPresetLoad();const ticket=c.ddsPresetLoadTicket,expected=d.load();
  const sessions=d.requests.filter(x=>x.path==='/api/v2/session').length;
  gate();await poll;await settle(c);
  assert.equal(d.requests.filter(x=>x.path==='/api/v2/session').length,sessions);
  assert.equal(c.ddsPresetLoadTicket,ticket);assert.equal(c.playable,false);
  assert.equal(await c.endDdsPresetLoad(ticket,expected),true);assert.equal(c.webMidiChannel,0);
});

for(const kind of ['native identity','owner route','owner token','HTTP failure'])test('UNSUPPORTED exception never masks '+kind,async()=>{
  const {d,c}=await ready();
  const adopt=d.adopt.bind(d);
  d.adopt=(command,session)=>{
    adopt(command,session);
    if(command.op!==2)return;
    if(kind==='native identity')d.state.fm.session_id++;
    if(kind==='owner route')d.state.clients[session.client].midi_channel=1;
    if(kind==='owner token')d.state.clients[session.client].token=999;
  };
  d.ownerCode=2;
  if(kind==='HTTP failure'){d.failPath='/api/v2/command';d.failStatus=503;d.failCode='owner_unavailable';}
  assert.equal(c.cc(73,32),true);await settle(c);assert.equal(c.status,'offline');
  assert.equal(c.playable,false);assert.equal(c.pending.size,0);
});
test('successful LOAD matches opaque returned context without interpreting numeric order',async()=>{
 const {d,c}=await ready();await c.beginDdsPresetLoad();const ticket=c.ddsPresetLoadTicket;
 d.state.context_epoch=1;d.caps.context_epoch=1;d.state.legacy.panic=(d.state.legacy.panic+1)>>>0;
 assert.equal(await c.endDdsPresetLoad(ticket,1),true);assert.equal(c.webMidiChannel,0);
});
test('explicit reconnect after cancelled LOAD is empty and never repeats its DDS selection',async()=>{
 const {d,c}=await ready();c.keys(1);await settle(c);await c.beginDdsPresetLoad();const ticket=c.ddsPresetLoadTicket;
 await c.endDdsPresetLoad(ticket);assert.equal(c.status,'offline');const selects=d.commands().filter(x=>x.op===5).length;
 await c.connect();await settle(c);assert.equal(c.status,'online');assert.equal(c.webMidiChannel,1);assert.equal(c.desiredMask,0);
 assert.equal(d.commands().filter(x=>x.op===5).length,selects);assert.equal(d.commands().filter(x=>x.op===3&&x.a).length,1);
});

test('an in-flight idle heartbeat is retired before LOAD submits and cannot resume later',async()=>{
 const {d,c}=await ready();const oldToken=c.token;
 d.time+=2000;d.holdWhen=cmd=>cmd.op===3&&cmd.a===0;c.tick();await turn();const gate=d.gate;assert.ok(gate);
 let prepared=false;const preparing=c.beginDdsPresetLoad().then(result=>{prepared=result;return result});
 await turn();assert.equal(prepared,false);gate();assert.equal(await preparing,true);
 const ticket=c.ddsPresetLoadTicket,commands=d.commands().length,expected=d.load();
 d.time+=2500;c.tick();await c.poll();await settle(c);
 assert.equal(d.commands().length,commands);assert.equal(await c.endDdsPresetLoad(ticket,expected),true);
 assert.ok(d.commands().slice(commands).every(cmd=>cmd.token!==oldToken));assert.equal(c.webMidiChannel,0);
});
for(const action of ['suspend','panic'])test(action+' while fresh LOAD route receipt is held retires its continuation',async()=>{
 const {d,c}=await ready();const oldToken=c.token;await c.beginDdsPresetLoad();const ticket=c.ddsPresetLoadTicket,expected=d.load();
 d.holdWhen=cmd=>cmd.op===5&&cmd.token!==oldToken;const ending=c.endDdsPresetLoad(ticket,expected);
 for(let n=0;n<60&&!d.gate;n++)await turn();const gate=d.gate;assert.ok(gate);assert.equal(c.playable,false);
 c[action]();await settle(c);const commands=d.commands().length;gate();assert.equal(await ending,false);await settle(c);
 assert.equal(d.commands().length,commands);assert.equal(c.desiredMask,0);assert.equal(c.ddsPresetLoadTicket,null);
 if(action==='suspend')assert.equal(c.status,'paused');else assert.equal(c.webMidiChannel,1);
});
test('fresh LOAD session HTTP failure remains offline until explicit reconnect',async()=>{
 const {d,c}=await ready();await c.beginDdsPresetLoad();const ticket=c.ddsPresetLoadTicket,expected=d.load();
 d.failPath='/api/v2/session';d.failNetwork=true;
 assert.equal(await c.endDdsPresetLoad(ticket,expected),false);assert.equal(c.status,'offline');
 const sessions=d.requests.filter(x=>x.path==='/api/v2/session').length;
 d.time+=20000;c.tick();await settle(c);assert.equal(d.requests.filter(x=>x.path==='/api/v2/session').length,sessions);
 d.failPath=null;await c.connect();await settle(c);assert.equal(c.status,'online');assert.equal(c.webMidiChannel,1);
});
for(const field of ['result_sequence','result_context_epoch'])test('UNSUPPORTED with wrong '+field+' is a timeout, never benign parameter rejection',async()=>{
 const {d,c}=await ready();const adopt=d.adopt.bind(d);d.ownerCode=2;
 d.adopt=(command,session)=>{adopt(command,session);if(command.op===2)d.state.clients[session.client][field]++;};
 c.cc(73,33);await settle(c);assert.equal(c.status,'offline');assert.match(String(c.error),/timeout/);
});
test('queued DDS CC is discarded when an explicit channel selection is pending',async()=>{
 const {d,c}=await ready();d.holdWhen=cmd=>cmd.op===3&&cmd.a===1;c.keys(1);await turn();const gate=d.gate;assert.ok(gate);
 assert.equal(c.cc(73,45),true);assert.equal(c.pending.has(73),true);assert.equal(c.mode(1),true);
 gate();await settle(c);assert.equal(c.webMidiChannel,1);assert.equal(c.pending.size,0);
 assert.equal(d.commands().filter(cmd=>cmd.op===2).length,0);
 assert.equal(c.cc(73,30),false);assert.equal(c.mode(0),true);await settle(c);
 assert.equal(d.commands().filter(cmd=>cmd.op===2).length,0,'switching back does not restore discarded CC');
});
test('a second LOAD cannot take over a waiting ticket',async()=>{
 const {d,c}=await ready();assert.equal(await c.beginDdsPresetLoad(),true);const ticket=c.ddsPresetLoadTicket;
 assert.equal(await c.beginDdsPresetLoad(),false);assert.equal(c.ddsPresetLoadTicket,ticket);
 const expected=d.load();assert.equal(await c.endDdsPresetLoad(ticket,expected),true);
});
for(const stage of ['session','empty-readback','capability-during-session','route-during-session'])test('identity change at LOAD '+stage+' forbids even its DDS route write',async()=>{
 const {d,c}=await ready();await c.beginDdsPresetLoad();const ticket=c.ddsPresetLoadTicket,expected=d.load();
 const selects=d.commands().filter(cmd=>cmd.op===5).length,request=d.request.bind(d);let injected=false,session=false;
 d.request=async(path,options={})=>{
  const result=await request(path,options);if(path==='/api/v2/session')session=true;
  if(!injected&&((['session','capability-during-session','route-during-session'].includes(stage)&&path==='/api/v2/session')||(stage==='empty-readback'&&session&&path==='/api/v2/state'))){
   injected=true;
   if(stage==='capability-during-session')d.caps.schema_sha256='unexpected';
   else if(stage==='route-during-session')d.state.clients[c.client].midi_channel=2;
   else {d.state.fm.session_id++;if(path==='/api/v2/state')result.fm.session_id=d.state.fm.session_id;}
  }
  return result;
 };
 assert.equal(await c.endDdsPresetLoad(ticket,expected),false);assert.equal(injected,true);
 assert.equal(d.commands().filter(cmd=>cmd.op===5).length,selects,'newly observed identity cannot inherit LOAD authority');
 assert.equal(c.status,'offline');assert.equal(c.playable,false);
});
