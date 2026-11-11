import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {RuntimeController} from '../src/runtime-controller.mjs';
import {getPreset} from '../src/generated/fm-contract.mjs';
import {RuntimeDevice,settle} from './helpers/runtime-device.mjs';

// The prepared CPU keeps its epoch on a preset; a new HTTP session defaults
// to KS. Model those server rules independently from the client controller.
class PreparedDevice extends RuntimeDevice {
  constructor(delay=80) {
    super();
    for(const [field,name] of [['caps','capabilities'],['state','state']])
      this[field]=JSON.parse(readFileSync(new URL('./fixtures/fm32/'+name+'.json',import.meta.url)));
    this.nativeDelay=delay;this.nativeSequence=0;this.nativeResult=0;
  }
  async request(path,options={}) {
    const result=await super.request(path,options);
    if(path==='/api/v2/session')this.state.clients[result.client].midi_channel=1;
    return result;
  }
  parse(body) {
    const n=typeof body==='string'?body.split(' ').map(Number):[];
    if(n.length===7&&n.every(Number.isInteger)&&n[3]===5&&n[4]===2&&n[5]===0&&n[6]===0)
      return {token:n[0],sequence:n[1],context:n[2],op:5,a:2,b:0,c:0};
    return super.parse(body);
  }
  adopt(command,session) {
    if(![5,8].includes(command.op))return super.adopt(command,session);
    const slot=this.state.clients[session.client];
    Object.assign(slot,{token:command.token,result_sequence:command.sequence,result_code:0,
      result_context_epoch:command.context,parameter_processed:command.sequence});
    this.log.push({...command,stage:'owner',code:0});
    if(command.op===5){slot.midi_channel=command.a;return;}
    const identity={session_id:this.state.fm.session_id,engine_epoch:this.state.fm.engine_epoch,
      sequence_id:++this.nativeSequence};
    this.state.fm.flags|=4;this.state.fm.accepted={...identity};
    this.nativeLog.push({stage:'accepted',at:this.time,...identity});
    if(this.onNativeStart)this.onNativeStart();
    this.events.push({at:this.time+this.nativeDelay,apply:()=>{
      this.state.fm.flags&=~4;this.state.fm.completed={...identity};
      this.state.fm.result={...identity,code:this.nativeResult};
      if(!this.nativeResult)this.state.fm.parameter_values=getPreset(1,command.a).values.map(v=>({
        parameter_id:v.parameter_id,target_word:(0xffff0000+v.operator_index)>>>0,
        value_u32:v.value_u32,sequence_id:identity.sequence_id}));
      this.nativeLog.push({stage:'completed',at:this.time,...identity});
    }});
  }
}
async function connected(delay) {
  const d=new PreparedDevice(delay),c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms)});
  await c.connect();assert.equal(c.status,'online');assert.equal(c.webMidiChannel,1);
  assert.equal(c.mode(2),true);await settle(c);assert.equal(c.webMidiChannel,2);
  return {d,c};
}
for(const preset of [3,4])test('prepared preset '+preset+' retains FM session and waits for CPU completion',async()=>{
  const {d,c}=await connected(120),token=c.token,context=c.runtimeState.context_epoch;
  assert.equal(c.preset(preset),true);await settle(c);
  assert.equal(c.status,'online',String(c.error));
  assert.equal(c.token,token,'a preset must not allocate a new default-KS session');
  assert.equal(c.webMidiChannel,2);assert.equal(c.runtimeState.context_epoch,context);
  assert.equal(c.nativeStatus.result.code,0);assert.equal(c.nativeStatus.completed.sequence_id,1);
  assert.equal(d.requests.filter(r=>r.path==='/api/v2/session').length,1);
  assert.equal(c.fmView.presetEditable,true);
  assert.deepEqual(c.runtimeState.mode,{requested:0,applied:0,switching:false,recovering:false});
});
for(const delay of [0,500])test('repeated presets keep one FM session with completion delay '+delay,async()=>{
  const {d,c}=await connected(delay),token=c.token;
  for(const preset of [3,4,3,4,3]) {
    assert.equal(c.preset(preset),true);await settle(c);
    assert.equal(c.token,token);assert.equal(c.webMidiChannel,2);assert.equal(c.playable,true);
    assert.equal(c.nativeStatus.completed.sequence_id,d.nativeSequence);
  }
  assert.equal(d.requests.filter(r=>r.path==='/api/v2/session').length,1);
  assert.equal(d.commands().filter(c=>c.op===5).length,1,'no compensating engine reselection');
});
test('failed native preset does not claim success or create a new KS session',async()=>{
  const {d,c}=await connected(80);d.nativeResult=10;
  assert.equal(c.preset(3),true);await settle(c);
  assert.equal(c.status,'offline');assert.match(String(c.error),/native preset result/);
  assert.equal(d.requests.filter(r=>r.path==='/api/v2/session').length,1);
  assert.equal(d.state.clients[0].midi_channel,2);
});
test('FM reset during preset invalidates the old request without rebinding to KS',async()=>{
  const {d,c}=await connected(80);d.onNativeStart=()=>d.state.fm.session_id++;
  assert.equal(c.preset(3),true);await settle(c);
  assert.equal(c.status,'offline');assert.match(String(c.error),/FM identity changed/);
  assert.equal(d.requests.filter(r=>r.path==='/api/v2/session').length,1);
});
test('missing native completion times out without creating a new KS session',async()=>{
  const {d,c}=await connected(Infinity);
  assert.equal(c.preset(3),true);await settle(c);
  assert.equal(c.status,'offline');assert.match(String(c.error),/native preset completion timeout/);
  assert.equal(c.token,0,'timeout clears the local session safely');assert.equal(d.state.clients[0].midi_channel,2);
  assert.equal(d.requests.filter(r=>r.path==='/api/v2/session').length,1);
});
