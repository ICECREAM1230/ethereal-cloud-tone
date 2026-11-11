import test from 'node:test';
import assert from 'node:assert/strict';
import {RuntimeController} from '../src/runtime-controller.mjs';
import {FdcController,calibrationWords,encodeFdcWrite,validateFdcState} from '../src/fdc.mjs';

test('the shared runtime owns an FDC controller without creating another session',()=>{
  const runtime=new RuntimeController({request(){throw Error('unexpected transport');}});
  assert.equal(typeof runtime.fdc,'object');
  assert.equal(runtime.fdc.owner,runtime);
});

const observed=(extra={})=>{
  const word=(0xfdc20000|0x0203)>>>0;
  return {api_major:2,contract_version:1,available:true,valid:true,fresh:true,status_word:word,revision:2,
    eligible:true,requested_enable:false,driver_enabled:false,busy:false,driver_fault:false,local_error:false,user_config_active:false,
    pending:false,request:null,result:null,confirmation:'transport_only',...extra};
};
function fixture(){
  const calls=[];let clock=0,state=observed();
  const owner={apiMajor:2,status:'online',suspended:false,rearming:false,modeWaiting:false,transition:null,
    token:100,sequence:3,epoch:1,inputVersion:1,runtimeState:{context_epoch:9},
    cpuMode:{supported:true,valid:true,ready:true,busy:false,fault:false,pending:false,desired:0,applied:0},
    now:()=>clock,notify(){},kick(){},fail(error){this.error=error;},
    transport:{async request(path,options={}){calls.push({path,options});if(options.method==='POST')return {api_major:2,accepted:owner.sequence,context_epoch:9,request_id:77,stage:'queued',confirmation:'transport_only'};return state;}}};
  const fdc=new FdcController(owner);
  return {fdc,owner,calls,setState:v=>state=v,advance:ms=>clock+=ms};
}
test('six calibration values preserve reverse spans and float32 bit patterns',()=>{
  const words=calibrationWords({pitch_far:1000,pitch_near:936,volume_far:2000,volume_near:3000,pitch_sigma:3.2,volume_sigma:0});
  assert.deepEqual(words.slice(0,4),[1000,936,2000,3000]);assert.equal(words[4],1078774989);assert.equal(words[5],0);
  assert.equal(encodeFdcWrite({token:100,sequence:4,context:9},3,words),'100 4 9 3 1000 936 2000 3000 1078774989 0');
});
for(const changed of [{pitch_far:0},{pitch_near:1063},{pitch_sigma:Infinity},{pitch_sigma:NaN},{pitch_sigma:-0},{volume_sigma:1000},{pitch_sigma:1e99}])test('calibration rejects unsafe field '+JSON.stringify(changed),()=>{
  assert.throws(()=>calibrationWords({pitch_far:1000,pitch_near:2000,volume_far:1000,volume_near:2000,pitch_sigma:1,volume_sigma:1,...changed}));
});
test('state decoder requires real tagged capability and consistent observation flags',()=>{
  assert.equal(validateFdcState(observed()).available,true);
  assert.throws(()=>validateFdcState(observed({status_word:0})),/invalid_fdc/);
  assert.throws(()=>validateFdcState(observed({driver_enabled:true})),/invalid_fdc/);
  assert.throws(()=>validateFdcState(observed({result:{token:100,sequence:4,context_epoch:9,request_id:77,op:1,code:0,stage:'applied'}})),/invalid_fdc/);
});
test('cached capability with stale observation stays readable but cannot arm',()=>{
  const f=fixture();f.fdc.state=validateFdcState(observed({valid:false,fresh:false}));
  assert.equal(f.fdc.can(1),false);
});
test('an unsent arming draft expires rather than silently enabling after recovery',async()=>{
  const f=fixture();await f.fdc.poll();f.fdc.queue(1);f.advance(1201);f.fdc.expire();
  assert.equal(f.fdc.draft,null);assert.equal(f.fdc.phase,'canceled');assert.equal(f.fdc.due(),false);
});
test('queued receipt and enabled/revision observation cannot acknowledge request adoption',async()=>{
  const f=fixture();await f.fdc.poll();assert.equal(f.fdc.queue(1),true);await f.fdc.send(1);
  assert.equal(f.owner.sequence,4);assert.equal(f.fdc.phase,'queued');assert.equal(f.calls.filter(c=>c.options.method==='POST').length,1);
  f.setState(observed({status_word:(0xfdc20000|0x030f)>>>0,revision:3,driver_enabled:true,requested_enable:true}));
  await f.fdc.poll();assert.equal(f.fdc.phase,'queued');assert.ok(f.fdc.command);
  f.setState(observed({result:{token:100,sequence:4,context_epoch:9,request_id:77,op:1,code:0,stage:'transport_sent'}}));
  await f.fdc.poll();assert.equal(f.fdc.phase,'transport_sent');assert.equal(f.fdc.command,null);assert.match(f.fdc.message,/不代表 CPU 已采用/);assert.equal(f.owner.lastAdopted,undefined);
});
test('arming requires CPU eligible but disable remains available across mode and errors',async()=>{
  const f=fixture();await f.fdc.poll();f.fdc.state.eligible=false;
  assert.equal(f.fdc.queue(1),false);assert.equal(f.fdc.queue(3,[1,2,3,4,0,0]),false);assert.equal(f.fdc.queue(0),true);
  f.fdc.cancel();await f.fdc.poll();f.fdc.state.busy=true;f.fdc.state.driver_fault=true;
  assert.equal(f.fdc.queue(0),true);assert.equal(f.fdc.queue(2),false);
});
test('authoritative eligible permits arming without mode metadata and does not fake cpu mode',async()=>{
  const f=fixture();f.owner.cpuMode=null;await f.fdc.poll();
  assert.equal(f.fdc.queue(1),true);assert.equal(f.owner.cpuMode,null);
});
test('fault-present restart remains available without eligible or mode metadata',async()=>{
  const f=fixture();await f.fdc.poll();f.owner.cpuMode=null;f.fdc.state.eligible=false;f.fdc.state.driver_fault=true;
  assert.equal(f.fdc.queue(1),false);assert.equal(f.fdc.queue(3,[1,2,3,4,0,0]),false);
  assert.equal(f.fdc.queue(2),true);await f.fdc.send(1);assert.match(f.calls.at(-1).options.body,/ 2 0 0 0 0 0 0$/);
  assert.equal(f.fdc.phase,'queued');
});
test('disable replaces unsent arming and clearInput cancels a draft',async()=>{
  const f=fixture();await f.fdc.poll();assert.equal(f.fdc.queue(1),true);assert.equal(f.fdc.queue(0),true);
  assert.equal(f.fdc.draft.op,0);f.fdc.cancel();assert.equal(f.fdc.due(),false);assert.equal(f.calls.some(c=>c.options.method==='POST'),false);
  const runtime=new RuntimeController({request(){throw Error('unexpected');}});runtime.fdc.draft={op:1};runtime.clearInput();assert.equal(runtime.fdc.draft,null);
});
test('expired or uncertain transmission never automatically retries',async()=>{
  const f=fixture();await f.fdc.poll();f.fdc.queue(1);await f.fdc.send(1);f.advance(5001);f.fdc.expire();
  assert.equal(f.fdc.phase,'unknown');assert.equal(f.fdc.due(),false);await f.fdc.poll();assert.equal(f.calls.filter(c=>c.options.method==='POST').length,1);
});
test('stale response cannot restore a canceled session request',async()=>{
  const f=fixture();await f.fdc.poll();let done;f.owner.transport.request=()=>new Promise(resolve=>done=resolve);
  f.fdc.queue(1);const send=f.fdc.send(1);f.owner.epoch++;f.owner.inputVersion++;f.fdc.cancel();
  done({api_major:2,accepted:4,context_epoch:9,request_id:77,stage:'queued',confirmation:'transport_only'});await send;
  assert.equal(f.fdc.command,null);assert.equal(f.fdc.phase,'canceled');
});
test('old firmware missing FDC endpoint does not fail the main runtime',async()=>{
  const f=fixture();f.owner.transport.request=async()=>{const error=Error('missing');error.status=404;throw error;};
  await f.fdc.poll();assert.equal(f.fdc.phase,'unavailable');assert.equal(f.owner.error,undefined);assert.equal(f.owner.status,'online');
});
