import test from 'node:test';
import assert from 'node:assert/strict';
import {RuntimeController,createRuntimeTransport,HttpError} from '../src/runtime-controller.mjs';
import {RuntimeDevice,settle,turn} from './helpers/runtime-device.mjs';
function fixture(configure=()=>{}) {
  const d=new RuntimeDevice();configure(d);d.clearCount=0;d.notifications=[];
  const c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms),onClear:()=>d.clearCount++,onChange:value=>d.notifications.push(value)});
  return {d,c};
}
async function connected(configure){const f=fixture(configure);await f.c.connect();assert.equal(f.c.status,'online');return f;}
const edit=c=>({contextId:c.fmView.contextId,target:{backendId:'1',voiceId:null,operatorId:'patch-operator:0'},parameterId:'18',value:30});
test('realtime keys submit the next bitmap without waiting for owner state',async()=>{
  const {d,c}=await connected();d.ownerDelay=500;
  const states=d.requests.filter(r=>r.path==='/api/v2/state').length;
  c.keys(1);await settle(c);c.keys(3);await settle(c);
  assert.deepEqual(d.commands().filter(v=>v.op===3&&v.a).map(v=>v.a),[1,3]);
  assert.equal(d.requests.filter(r=>r.path==='/api/v2/state').length,states);
  assert.equal(c.lastAdopted,1);assert.equal(c.sentMask,3);
  await c.poll();assert.equal(c.desiredMask,3,'unprocessed snapshot is not a lost lease');
  d.time+=500;await c.poll();assert.equal(c.lastAdopted,3);
});
test('realtime keys are not blocked by an already pending background poll',async()=>{
  const {d,c}=await connected();d.holdState=true;
  const poll=c.poll();await turn();assert.ok(d.stateGate);
  try {c.keys(1);await turn();assert.equal(d.commands().at(-1).a,1);}
  finally {d.stateGate();await poll;await settle(c);}
});
test('background keys observed before their HTTP receipt are not a lost lease',async()=>{
  const {d,c}=await connected();c.keys(1);await settle(c);d.time+=25;await c.poll();
  const token=c.token,clears=d.clearCount;d.holdWhen=v=>v.op===3&&v.a===3;
  c.keys(3);await turn();const gate=d.gate;assert.ok(gate);d.time+=25;
  try {await c.poll();assert.equal(c.token,token);assert.equal(c.desiredMask,3);assert.equal(d.clearCount,clears);}
  finally {gate();await settle(c);}
  await c.poll();assert.equal(c.lastAdopted,3);assert.equal(c.sentMask,3);
});
test('realtime chord additions coalesce without removing release boundaries',async()=>{
  const {d,c}=await connected();d.holdWhen=v=>v.op===3&&v.a===1;
  c.keys(1);await turn();const gate=d.gate;c.keys(3);c.keys(7);c.keys(6);c.keys(14);
  gate();await settle(c);
  assert.deepEqual(d.commands().filter(v=>v.op===3&&v.sequence>1).map(v=>v.a),[1,7,6,14]);
});
test('fresh context binding clears old receipt counters before new empty adoption',async()=>{
  const {d,c}=await connected();c.keys(1);await settle(c);d.time+=25;await c.poll();assert.equal(c.lastAdopted,2);
  const token=c.token;d.moveContext();d.holdWhen=v=>v.op===3&&v.token!==token;
  const poll=c.poll();for(let i=0;i<40&&!d.gate;i++)await turn();assert.ok(d.gate);
  try {
    assert.equal(c.playable,false);assert.equal(c.desiredMask,0);assert.equal(c.frames.length,0);assert.equal(c.parameters.size,0);
    assert.equal(c.lastReceived,0);assert.equal(c.lastAdopted,0,'prior context adoption must not appear in the new binding');
  } finally {d.gate();await poll;await settle(c);}
  assert.notEqual(c.token,token);assert.equal(c.lastReceived,1);assert.equal(c.lastAdopted,1);
  assert.equal(d.commands().filter(v=>v.op===3&&v.a).length,1,'old held input is never replayed');
});
test('transport retains error JSON code and tolerates absent error body',async()=>{
  for(const json of [async()=>({api_major:2,code:'session_full'}),async()=>{throw Error('empty');}]) {
    const t=createRuntimeTransport(async()=>({ok:false,status:503,json}));
    await assert.rejects(t.request('/api/v2/session'),e=>e instanceof HttpError&&e.status===503&&(e.code==='session_full'||e.code===null));
  }
});
test('transport request and release enforce finite abort, no-store and old control header',async()=>{
  const calls=[];const t=createRuntimeTransport(async(path,options)=>{calls.push({path,options});return {ok:true,json:async()=>({ok:true})};},15);
  assert.deepEqual(await t.request('/api/state'),{ok:true});await t.release('body','/api/v2/command');
  assert.equal(calls[0].options.cache,'no-store');assert.ok(calls[0].options.signal);assert.equal(calls[1].options.keepalive,true);assert.equal(calls[1].options.headers['X-FPGA-Control'],'1');assert.ok(calls[1].options.signal);
});
test('connect capability state session and adopted empty seven-number bitmap before input',async()=>{
  const {d,c}=await connected();assert.equal(c.apiMajor,2);assert.equal(c.state.ready,false);assert.equal(c.state.engine.applied,0);assert.equal(c.modeState.applied,2);assert.equal(c.playable,true);
  assert.deepEqual(d.commands().map(v=>[v.op,v.a,v.c]),[[3,0,0]]);assert.equal(c.lastReceived,1);assert.equal(c.lastAdopted,1);assert.equal(c.token,100);
  assert.deepEqual(d.requests.slice(0,3).map(r=>r.path),['/api/v2/capabilities','/api/v2/state','/api/v2/session']);assert.ok(d.notifications.every(value=>value===c));
});
test('received before owner adoption keeps new input closed',async()=>{
  const {d,c}=fixture();d.holdWhen=()=>true;const work=c.connect();await turn();assert.equal(c.playable,false);assert.equal(c.lastAdopted,0);d.gate();await work;assert.equal(c.playable,true);
});
for(const mode of [0,1])test('v2 legacy mode '+mode+' works with unavailable FM',async()=>{
  const {c}=await connected(d=>{d.state.mode.requested=d.state.mode.applied=mode;d.state.legacy.ready=true;d.state.fm.available=false;d.caps.engines[2].available=false;});
  assert.equal(c.playable,true);assert.equal(c.fmRuntime.playable,false);assert.equal(c.modeState.applied,mode);assert.equal(c.cc(74,5),mode===0);assert.equal(c.decay(200),mode===1);await settle(c);
});
for(const status of [404,405,501])test('first explicit missing v2 endpoint '+status+' falls back to v1 facade',async()=>{
  const {d,c}=await connected(d=>d.missing=status);assert.equal(c.apiMajor,1);assert.equal(c.runtimeState,null);assert.equal(c.playable,true);c.retryAt=Infinity;c.message='file notice';assert.equal(c.retryAt,Infinity);assert.equal(c.message,'file notice');
  assert.equal(c.cc(7,20),true);await settle(c);assert.equal(c.state.volume,40);assert.ok(d.notifications.every(value=>value===c));
});
for(const kind of ['503','timeout','malformed capabilities','malformed state'])test(kind+' cannot downgrade v2',async()=>{
  const {d,c}=fixture(d=>{if(kind==='503'||kind==='timeout'){d.failPath='/api/v2/capabilities';d.failStatus=503;d.failNetwork=kind==='timeout';}if(kind==='malformed capabilities')d.caps.api_major=1;if(kind==='malformed state')d.state.mode.applied=3;});
  await c.connect();assert.equal(c.status,'offline');assert.ok(!d.requests.some(r=>r.path==='/api/state'));
});
test('parameter owner adoption is distinct from changed completed cache and native status',async()=>{
  const {d,c}=await connected();d.cacheDelay=200;const original=structuredClone(c.nativeStatus);assert.equal(c.fmParameter(edit(c)),true);await settle(c);
  assert.equal(c.lastAdopted,2);assert.equal(c.pending.size,1);assert.equal(c.fmView.model.patchOperators[0].parameters.find(p=>p.id==='18').value,31);
  assert.deepEqual(d.commands().at(-1),{token:100,sequence:2,context:10,op:7,a:18,b:4294901760,c:30,stage:'received',at:35});
  d.time+=200;await c.poll();await settle(c);assert.equal(c.pending.size,0);assert.equal(c.fmView.model.patchOperators[0].parameters.find(p=>p.id==='18').value,30);assert.deepEqual(c.nativeStatus,original);assert.equal(c.nativeStatus.completed.sequence_id,6);
});
test('same confirmed value is a no-op and UI navigation rejects stale view without resetting held keys',async()=>{
  const {d,c}=await connected();assert.equal(c.fmParameter({...edit(c),value:31}),true);assert.equal(d.commands().length,1);const event=edit(c);c.keys(1);await settle(c);const clear=d.clearCount;c.selectVoice(3);assert.equal(c.selectedVoiceIndex,3);assert.equal(c.fmParameter(event),false);assert.equal(c.desiredMask,1);assert.equal(d.clearCount,clear);
});
for(const action of ['mode','preset','panic'])test(action+' adopts new context and token empty rearm without replaying keys',async()=>{
  const {d,c}=await connected();c.keys(3);await settle(c);const token=c.token;
  assert.equal(action==='mode'?c.mode(1):action==='preset'?c.preset(1):c.panic(),true);await settle(c);assert.equal(c.status,'online');assert.notEqual(c.token,token);assert.equal(c.runtimeState.context_epoch,11);assert.equal(c.playable,true);assert.equal(c.desiredMask,0);
  const commands=d.commands();assert.deepEqual(commands.at(-1).a,0);assert.equal(commands.at(-1).token,c.token);assert.ok(!commands.slice(2).some(v=>v.op===3&&v.a));
  if(action!=='panic'){const operation=d.log.findIndex(v=>v.stage==='received'&&v.op===(action==='mode'?5:8));const zero=d.log.findIndex(v=>v.stage==='owner'&&v.op===3&&v.a===0&&v.sequence>1);assert.ok(zero<operation);}
});
test('READY MUTED unselected nonce zero can only explicit SELECT to create native instance',async()=>{
  const {d,c}=await connected(d=>{d.state.fm.flags=5;d.state.fm.session_id=0;d.state.fm.engine_epoch=0;});assert.equal(c.playable,false);assert.equal(c.canSelectMode(2,1),true);assert.equal(c.keys(1),false);assert.equal(c.fmParameter(edit(c)),false);assert.equal(c.preset(1),false);
  assert.equal(c.mode(2,1),true);await settle(c);assert.equal(c.playable,true);assert.equal(d.commands().filter(v=>v.op===5).length,1);
});
test('musical BUSY preserves contacts heartbeat new On and subset releases while edits wait',async()=>{
  const {d,c}=await connected();c.keys(3);await settle(c);const clears=d.clearCount,token=c.token;d.state.fm.flags=11;await c.poll();assert.equal(c.playable,true);assert.equal(c.desiredMask,3);assert.equal(d.clearCount,clears);assert.equal(c.token,token);assert.equal(c.keys(7),true);await settle(c);assert.equal(c.fmParameter(edit(c)),false);
  d.time+=250;c.tick();await settle(c);assert.equal(d.commands().at(-1).a,7);assert.equal(c.keys(1),true);await settle(c);assert.equal(d.commands().at(-1).a,1);assert.equal(c.keys(0),true);await settle(c);assert.equal(c.desiredMask,0);assert.equal(d.clearCount,clears);
});
test('Panic preempts held ordinary response and old finally cannot release new running ticket',async()=>{
  const {d,c}=await connected();d.holdWhen=v=>v.op===3&&v.a===1;c.keys(1);await turn();const old=d.gate;d.holdWhen=v=>v.op===4;assert.equal(c.panic(),true);await turn();assert.equal(d.commands().at(-1).op,4);assert.ok(d.commands().at(-1).sequence>2);const panicGate=d.gate,active=c.running;
  old();await turn();assert.equal(c.running,active);assert.equal(c.playable,false);panicGate();await settle(c);assert.equal(c.playable,true);assert.equal(c.desiredMask,0);assert.equal(d.commands().filter(v=>v.op===3&&v.a).length,1);
});
for(const [name,mutate] of Object.entries({
  'external context':d=>d.moveContext(),
  'native nonce':d=>d.state.fm.session_id++,
  'native engine epoch':d=>d.state.fm.engine_epoch++,
  'topology':d=>d.state.fm.topology_generation++,
  'digest':d=>d.caps.schema_sha256='bad',
  'FM dimensions':d=>d.state.fm.patch_operator_count=3,
  'FM unavailable':d=>d.state.fm.available=false,
  'READY lost':d=>d.state.fm.flags=2,
  'token replaced':d=>d.state.clients[0].token=888,
  'mode recovery':d=>d.state.mode.recovering=true
}))test(name+' clears intents, rebinds once and never repeats old On',async()=>{
  const {d,c}=await connected();c.keys(1);await settle(c);const token=c.token;mutate(d);await c.poll();await settle(c);
  assert.equal(c.status,'online');assert.notEqual(c.token,token);assert.equal(c.desiredMask,0);assert.equal(d.commands().filter(v=>v.op===3&&v.a).length,1);assert.equal(d.commands().at(-1).a,0);
  const sessions=d.requests.filter(r=>r.path==='/api/v2/session').length;await c.poll();await c.poll();await settle(c);assert.equal(d.requests.filter(r=>r.path==='/api/v2/session').length,sessions);
});
test('PLL reset keeps requested FM recovery closed until new explicit SELECT',async()=>{
  const {d,c}=await connected();c.keys(1);await settle(c);d.state.fm.session_id=0;d.state.fm.engine_epoch=0;d.state.fm.flags=5;
  d.state.mode={requested:2,applied:0,switching:true,recovering:true};await c.poll();await settle(c);assert.equal(c.modeState.requested,2);assert.equal(c.modeState.applied,0);assert.equal(c.playable,false);assert.equal(c.canSelectMode(2,1),true);assert.equal(d.commands().filter(v=>v.op===5).length,0);
  assert.equal(c.mode(2,1),true);await settle(c);assert.equal(c.playable,true);assert.ok(c.runtimeState.fm.session_id>0);assert.equal(c.desiredMask,0);
});
test('ordinary voice-generation changes do not fence or clear held contacts',async()=>{
  const {d,c}=await connected();c.keys(1);await settle(c);const token=c.token,clears=d.clearCount;d.state.fm.voice_generations[0]++;await c.poll();assert.equal(c.token,token);assert.equal(d.clearCount,clears);assert.equal(c.desiredMask,1);assert.equal(c.runtimeState.fm.voice_generations[0],1);
});
test('expired performance lease cannot be renewed with the old held mask',async()=>{
  const {d,c}=await connected();c.keys(3);await settle(c);d.time+=1201;await c.poll();await settle(c);d.time+=250;c.tick();await settle(c);
  assert.equal(c.desiredMask,0);assert.equal(d.commands().filter(v=>v.op===3&&v.a===3).length,1);assert.equal(c.runtimeState.clients[0].keys,0);
});
for(const action of ['suspend','fail'])test(action+' fences late response and reconnect never repeats old contacts',async()=>{
  const {d,c}=await connected();d.holdWhen=v=>v.op===3&&v.a===1;c.keys(1);await turn();const gate=d.gate;c.keys(3);
  if(action==='suspend')c.suspend();else c.fail(Error('socket lost'));
  assert.equal(c.desiredMask,0);assert.equal(c.pending.size,0);assert.ok(d.requests.some(r=>r.path==='release'&&r.target==='/api/v2/command'));
  gate();await turn();if(action==='suspend')await c.resume();else await c.connect();await settle(c);assert.equal(c.status,'online');assert.equal(c.desiredMask,0);assert.equal(d.commands().filter(v=>v.op===3&&v.a===3).length,0);
});
test('background resume reuses evidenced current slot only after fresh empty owner adoption',async()=>{
  const {d,c}=await connected();c.keys(1);await settle(c);const token=c.token;c.suspend();await turn();await c.resume();await settle(c);assert.equal(c.token,token);assert.equal(c.sequence,4);assert.equal(c.playable,true);
  assert.equal(d.requests.filter(r=>r.path==='/api/v2/session').length,1);assert.equal(d.commands().at(-1).a,0);
});
for(const change of ['context','expired','nonce','replacement'])test('resume '+change+' requires a new token',async()=>{
  const {d,c}=await connected();const token=c.token;c.suspend();await turn();if(change==='context')d.moveContext();if(change==='expired')d.time+=10001;if(change==='nonce')d.state.fm.session_id++;if(change==='replacement')d.state.clients[0].token=888;await c.resume();await settle(c);assert.notEqual(c.token,token);assert.equal(c.desiredMask,0);
});
test('old delayed state cannot overwrite a newer Panic state or unlock input',async()=>{
  const {d,c}=await connected();d.holdState=true;const poll=c.poll();await turn();const old=d.stateGate;d.holdWhen=v=>v.op===4;c.panic();await turn();const stop=d.gate;old();await poll;assert.equal(c.playable,false);stop();await settle(c);assert.equal(c.runtimeState.context_epoch,11);assert.equal(c.playable,true);
});
test('stale-context Panic converges to a legal fresh session and only continues STOP',async()=>{
  const {d,c}=await connected();c.keys(1);await settle(c);d.moveContext();c.panic();await settle(c);assert.equal(c.status,'online');assert.equal(c.runtimeState.context_epoch,12);assert.equal(d.commands().filter(v=>v.op===4).length,1);assert.equal(d.commands().filter(v=>v.op===3&&v.a).length,1);
});
test('Panic preempts pending SELECT response and old mode cannot start another transition',async()=>{
  const {d,c}=await connected();d.holdWhen=v=>v.op===5;c.mode(1);for(let i=0;i<20&&!d.gate;i++)await turn();assert.ok(d.gate);const old=d.gate;c.panic();await settle(c);assert.equal(c.status,'online');assert.equal(c.error,null);const successful=d.log.filter(v=>v.stage==='owner'&&v.op===4&&v.code===0);assert.equal(successful.length,1);assert.ok(c.runtimeState.clients[0].stop_processed>=successful[0].sequence);const token=c.token,state=c.runtimeState;old();await turn();assert.equal(c.token,token);assert.deepEqual(c.runtimeState,state);assert.equal(c.status,'online');assert.equal(c.desiredMask,0);assert.equal(d.commands().filter(v=>v.op===5).length,1);
});
test('queue age and congestion clear new On, retaining only a safe zero',async()=>{
  for(const congestion of [false,true]) {
    const {d,c}=await connected();d.holdWhen=v=>v.op===3&&v.a===1;c.keys(1);await turn();const old=d.gate;
    if(congestion)for(let i=0;i<33;i++)c.keys(i%2?1:3);else{c.keys(3);d.time+=400;}
    old();await settle(c);assert.equal(c.desiredMask,0);assert.equal(d.commands().filter(v=>v.op===3&&v.a===3).length,0);assert.equal(d.commands().at(-1).a,0);
  }
});
test('HTTP sequence exhaustion binds a new token rather than wrapping or replaying On',async()=>{
  const {d,c}=await connected();c.sequence=4294967295;c.keys(1);await settle(c);assert.equal(c.status,'online');assert.equal(c.token,101);assert.equal(c.sequence,1);assert.equal(c.desiredMask,0);assert.ok(!d.commands().some(v=>v.sequence===0||v.a===1));
});
for(const [status,code] of [[410,'session_expired'],[503,'fm_unavailable']])test('HTTP '+status+' retains '+code+' and fails without repeating operation',async()=>{
  const {d,c}=await connected();d.failPath='/api/v2/command';d.failStatus=status;d.failCode=code;c.keys(1);await settle(c);assert.equal(c.status,'offline');assert.equal(c.error,code);assert.match(c.message,new RegExp(code));assert.equal(c.desiredMask,0);
});
test('owner failure is displayed even when processed advanced; no false success',async()=>{
  const {d,c}=await connected();d.ownerCode=7;d.state.clients[0].processed=99;c.keys(1);await settle(c);d.time+=25;await c.poll();assert.equal(c.status,'offline');assert.equal(c.error,7);assert.equal(c.lastAdopted,1);assert.equal(d.commands().filter(v=>v.op===3&&v.a).length,1);
});
test('background owner timeout clears intent without retrying received keys',async()=>{
  const {d,c}=await connected();d.ownerDelay=1000;c.keys(1);c.keys(3);await settle(c);
  assert.equal(c.status,'online');d.time+=700;await c.poll();await settle(c);
  assert.equal(c.status,'offline');assert.equal(c.desiredMask,0);
  assert.equal(d.commands().filter(v=>v.op===3&&v.a===1).length,1);
  assert.equal(d.commands().filter(v=>v.op===3&&v.a===3).length,1);
});
test('parameter cache wrong target or same native sequence cannot settle pending',async()=>{
  const {d,c}=await connected();d.noCache=true;c.fmParameter(edit(c));await settle(c);assert.equal(c.pending.size,1);d.state.fm.parameter_values[0].value_u32=30;await c.poll();assert.equal(c.pending.size,1);d.state.fm.parameter_values[0].sequence_id++;d.state.fm.parameter_values[0].target_word=4294901761;await c.poll();assert.equal(c.pending.size,1);
});
test('parameter coalesces by target and waits for owner before sending latest value',async()=>{
  const {d,c}=await connected();d.holdWhen=v=>v.op===7;c.fmParameter({...edit(c),value:29});await turn();const release=d.gate;c.fmParameter({...edit(c),value:28});c.fmParameter({...edit(c),value:27});release();await settle(c);assert.deepEqual(d.commands().filter(v=>v.op===7).map(v=>v.c),[29,27]);d.time+=100;await c.poll();assert.equal(c.pending.size,0);
});
test('queued FM callback is re-gated when runtime feature changed before send',async()=>{
  const {d,c}=await connected();d.holdWhen=v=>v.op===3&&v.a===1;c.keys(1);await turn();const gate=d.gate;c.fmParameter(edit(c));d.state.fm.features=1007;gate();await settle(c);assert.equal(c.desiredMask,0);assert.equal(d.commands().filter(v=>v.op===7).length,0);
});
test('already recognized v2 never falls back after later missing endpoint',async()=>{
  const {d,c}=await connected();c.fail(Error('disconnected'));d.missing=404;await c.connect();assert.equal(c.apiMajor,2);assert.equal(c.status,'offline');assert.ok(!d.requests.some(r=>r.path==='/api/state'));
});
test('context discovery retries are bounded and session context must match',async()=>{
  const {d,c}=fixture();d.onCaps=()=>{d.caps.context_epoch++;};await c.connect();assert.equal(c.status,'offline');assert.ok(d.requests.filter(r=>r.path==='/api/v2/capabilities').length<=4);assert.equal(d.requests.filter(r=>r.path==='/api/v2/session').length,0);
  const f=fixture();const request=f.d.transport.request;f.d.transport.request=async(path,options)=>{const body=await request(path,options);if(path==='/api/v2/session')body.context_epoch++;return body;};await f.c.connect();assert.equal(f.c.status,'offline');assert.equal(f.d.commands().filter(v=>v.op===3).length,0,'mismatching session is never used for initial input');
});
test('malformed or timed-out first discovery cannot later fall back on a missing endpoint',async()=>{
  for(const kind of ['malformed','timeout']) {
    const {d,c}=fixture(d=>{if(kind==='malformed')d.caps.api_major=1;else{d.failPath='/api/v2/capabilities';d.failNetwork=true;}});await c.connect();d.failPath=null;d.missing=404;await c.connect();assert.equal(c.status,'offline');assert.ok(!d.requests.some(r=>r.path==='/api/state'));
  }
});
test('a throttled heartbeat past lease expiry sends empty bitmap and never renews stale On',async()=>{
  const {d,c}=await connected();c.keys(1);await settle(c);d.time+=1201;c.tick();await settle(c);assert.equal(c.desiredMask,0);assert.equal(d.commands().filter(v=>v.op===3&&v.a===1).length,1);assert.equal(d.commands().at(-1).a,0);
});
test('legacy ready loss clears held input while retaining v2 mode identity',async()=>{
  const {d,c}=await connected(d=>{d.state.mode.requested=d.state.mode.applied=0;d.state.legacy.ready=true;});c.keys(1);await settle(c);d.state.legacy.ready=false;await c.poll();await settle(c);assert.equal(c.playable,false);assert.equal(c.desiredMask,0);assert.equal(c.modeState.requested,0);assert.equal(d.commands().at(-1).a,0);
});
test('abort-aware request times out and release settles as finite best effort',async()=>{
  const t=createRuntimeTransport((path,options)=>new Promise((resolve,reject)=>options.signal.addEventListener('abort',()=>reject(Error('abort')),{once:true})),10);
  await assert.rejects(t.request('/api/state'),/abort/);await t.release('body');
});
test('v1 facade retains old keyboard, CC, mode, decay, suspend/resume and panic calls',async()=>{
  const {c}=await connected(d=>d.missing=404);assert.equal(c.keys(1),true);await settle(c);assert.equal(c.desiredMask,1);c.releaseOwn();await settle(c);assert.equal(c.desiredMask,0);assert.equal(c.mode(1),true);await settle(c);assert.equal(c.decay(200),true);await settle(c);assert.equal(c.state.ks_decay_ms,200);c.suspend();assert.equal(c.suspended,true);c.resume();await settle(c);c.panic();await settle(c);assert.equal(c.playable,true);assert.equal(c.state.keys[c.client],0);
});
test('queued FM event from previous UI voice navigation is rejected against the current view',async()=>{
  const {d,c}=await connected();d.holdWhen=v=>v.op===3&&v.a===1;c.keys(1);await turn();const release=d.gate;assert.equal(c.fmParameter(edit(c)),true);c.selectVoice(4);release();await settle(c);assert.equal(d.commands().filter(v=>v.op===7).length,0);assert.equal(c.pending.size,0);assert.equal(c.desiredMask,1);
});
test('stale-context Panic stays gated throughout prerequisite fresh session empty adoption',async()=>{
  const {d,c}=await connected();d.moveContext();const samples=[];const prior=c.onChange;c.onChange=value=>{prior(value);samples.push(value.playable);};d.holdWhen=v=>v.op===4;c.panic();for(let i=0;i<50&&!d.gate;i++)await turn();assert.ok(d.gate);assert.ok(samples.every(value=>value===false));d.gate();await settle(c);assert.equal(c.playable,true);
});
test('FM performance and parameter edit gates remain distinct from preset and owner CC support',async()=>{
  const {d,c}=await connected(d=>d.state.fm.features=1007);assert.equal(c.playable,true);assert.equal(c.editable,false);assert.equal(c.fmRuntime.editable,false);assert.equal(c.fmParameter(edit(c)),false);assert.equal(c.cc(7,20),true);await settle(c);assert.equal(c.preset(1),true);await settle(c);assert.equal(d.commands().filter(v=>v.op===8).length,1);
});
test('expired before suspend cannot regain lease evidence from an unconfirmed keepalive',async()=>{
  const {d,c}=await connected();const token=c.token;d.time+=10001;c.suspend();await turn();await c.resume();await settle(c);assert.equal(c.status,'online');assert.notEqual(c.token,token);assert.equal(c.desiredMask,0);
});
test('resume with exhausted HTTP sequence rebuilds instead of failing same token',async()=>{
  const {c}=await connected();const token=c.token;c.sequence=4294967295;c.suspend();await c.resume();await settle(c);assert.equal(c.status,'online');assert.notEqual(c.token,token);assert.equal(c.sequence,1);
});
test('Panic at HTTP sequence exhaustion still stops all sources using fresh legal token',async()=>{
  const {d,c}=await connected();c.sequence=4294967295;c.panic();await settle(c);assert.equal(c.status,'online');assert.equal(d.commands().filter(v=>v.op===4).length,1);assert.equal(c.runtimeState.context_epoch,11);assert.equal(c.desiredMask,0);
});
test('BUSY permits read-only voice navigation without clearing held input',async()=>{
  const {d,c}=await connected();c.keys(1);await settle(c);d.state.fm.flags=11;await c.poll();const clears=d.clearCount;assert.equal(c.selectVoice(5),true);assert.equal(c.selectedVoiceIndex,5);assert.equal(c.fmView.model.selectedVoiceId,'voice:5');assert.equal(c.desiredMask,1);assert.equal(d.clearCount,clears);
});
test('25-bit masks and clamped velocity retain full HTTP width without extra writes',async()=>{
  const {d,c}=await connected();for(const mask of [-1,33554432,1.5,NaN])assert.equal(c.keys(mask),false);c.setVelocity(500);assert.equal(c.velocity,127);assert.equal(c.keys(33554431),true);await settle(c);assert.equal(d.commands().at(-1).a,33554431);assert.equal(d.commands().at(-1).b,127);c.setVelocity(-4);assert.equal(c.velocity,1);c.keys(0);await settle(c);assert.equal(d.commands().at(-1).b,1);
});
test('native observations retain original identities even after current instance changes',async()=>{
  const {d,c}=await connected();const old=structuredClone(c.nativeStatus);d.state.fm.session_id=99;d.state.fm.engine_epoch=77;await c.poll();await settle(c);assert.equal(c.runtimeState.fm.session_id,99);assert.deepEqual(c.nativeStatus,old);assert.equal(c.nativeStatus.completed.session_id,42);
});
test('a late rejected ordinary response cannot overwrite the completed newer Panic session',async()=>{
  const {d,c}=await connected();const request=d.transport.request;d.transport.request=async(path,options)=>{const response=await request(path,options);if(path==='/api/v2/command'&&options.body.split(' ')[4]==='1'&&options.body.split(' ')[3]==='3')throw d.error(409,'stale_sequence');return response;};d.holdWhen=v=>v.op===3&&v.a===1;c.keys(1);await turn();const old=d.gate;c.panic();await settle(c);const token=c.token;old();await turn();assert.equal(c.status,'online');assert.equal(c.token,token);assert.equal(c.error,null);assert.equal(c.desiredMask,0);
});
for(const action of ['mode','preset'])test('failed prerequisite release prevents publishing '+action,async()=>{
  const {d,c}=await connected();c.keys(1);await settle(c);d.ownerDelay=1000;if(action==='mode')c.mode(1);else c.preset(1);await settle(c);assert.equal(c.status,'offline');assert.equal(d.commands().filter(v=>v.op===(action==='mode'?5:8)).length,0);assert.equal(c.desiredMask,0);
});
test('capability/state context movement is reread finitely before creating a matched session',async()=>{
  const {d,c}=fixture();const request=d.transport.request;let moved=false;d.transport.request=async(path,options)=>{if(path==='/api/v2/state'&&!moved){moved=true;d.moveContext();}return request(path,options);};await c.connect();assert.equal(c.status,'online');assert.equal(c.runtimeState.context_epoch,11);assert.equal(d.commands()[0].context,11);assert.equal(d.requests.filter(r=>r.path==='/api/v2/capabilities').length,2);
});
test('session capacity refusal preserves the explicit server code without any input',async()=>{
  const {d,c}=fixture(d=>{d.failPath='/api/v2/session';d.failStatus=503;d.failCode='session_full';});await c.connect();assert.equal(c.status,'offline');assert.equal(c.error,'session_full');assert.equal(d.commands().length,0);
});
test('new keys submit during an old background poll and ignore its unprocessed bitmap',async()=>{
  const {d,c}=await connected();const token=c.token,clears=d.clearCount;
  d.holdState=true;const poll=c.poll();await turn();const gate=d.stateGate;
  c.keys(1);await turn();c.keys(3);await turn();
  assert.deepEqual(d.commands().filter(v=>v.op===3&&v.a).map(v=>v.a),[1,3]);
  gate();await poll;await settle(c);assert.equal(c.token,token);assert.equal(c.desiredMask,3);assert.equal(c.sentMask,3);assert.equal(d.clearCount,clears);
});
test('native-change background poll fences received old input before fresh binding',async()=>{
  const {d,c}=await connected();const epoch=c.epoch;d.state.fm.session_id++;
  d.holdState=true;const poll=c.poll();await turn();const gate=d.stateGate;c.keys(1);await turn();
  assert.equal(d.commands().filter(v=>v.op===3&&v.a).length,1);
  gate();await poll;await settle(c);assert.ok(c.epoch>epoch);assert.equal(c.token,101);assert.equal(c.desiredMask,0);
  c.keys(2);await settle(c);assert.equal(c.status,'online');
  assert.deepEqual(d.commands().filter(v=>v.op===3&&v.a).map(v=>[v.token,v.a]),[[100,1],[101,2]]);
});
test('a delayed background snapshot cannot undo a current subset release',async()=>{
  const {d,c}=await connected();c.keys(3);await settle(c);d.time+=25;await c.poll();const token=c.token;
  d.holdState=true;const poll=c.poll();await turn();const gate=d.stateGate;c.keys(1);await turn();
  assert.equal(d.state.clients[0].keys,3);gate();await poll;await settle(c);
  assert.equal(c.token,token);assert.equal(c.sentMask,1);assert.equal(c.desiredMask,1);assert.equal(c.status,'online');
  d.time+=25;await c.poll();assert.equal(c.runtimeState.clients[0].keys,1);
});
for(const [name,configure,match] of [
  ['selection required',d=>{d.state.fm.flags=5;d.state.fm.session_id=0;d.state.fm.engine_epoch=0;},/显式选择/],
  ['owner recovery',d=>{d.state.fm.flags=5;d.state.fm.session_id=0;d.state.mode={requested:2,applied:0,switching:true,recovering:true};},/显式选择/],
  ['mismatched dimensions',d=>d.state.fm.patch_operator_count=3,/不匹配/],
  ['unavailable capability',d=>d.state.fm.available=false,/不可用/]
])test('online blocked FM feedback describes '+name+' without promising input',async()=>{
  const {c}=await connected(configure);assert.equal(c.status,'online');assert.equal(c.playable,false);assert.match(c.message,match);assert.doesNotMatch(c.message,/可以重新输入/);
});
test('owner STALE_EPOCH STOP recovery keeps input fenced until a fresh STOP and final zero are adopted',async()=>{
  const {d,c}=await connected();d.holdWhen=v=>v.op===5;c.mode(1);for(let i=0;i<20&&!d.gate;i++)await turn();const old=d.gate;
  d.holdWhen=v=>v.op===3&&v.token!==100;c.panic();for(let i=0;i<200&&(!d.gate||d.gate===old);i++)await turn();assert.ok(d.gate!==old,'explicit stale owner result must start fresh-session empty confirmation');const recovery=d.gate;assert.equal(c.playable,false);assert.equal(c.keys(7),false);recovery();await settle(c);assert.equal(c.status,'online');assert.ok(d.log.some(v=>v.stage==='owner'&&v.op===4&&v.code===5));assert.ok(d.log.some(v=>v.stage==='owner'&&v.op===4&&v.code===0));assert.equal(d.commands().filter(v=>v.op===3&&v.a===7).length,0);old();await turn();assert.equal(c.status,'online');
});
test('repeated explicit owner STALE_EPOCH STOP remains bounded and safely offline',async()=>{
  const {d,c}=await connected();const adopt=d.adopt.bind(d);d.adopt=(command,session)=>{if(command.op===4)d.moveContext();adopt(command,session);};c.panic();await settle(c);assert.equal(c.status,'offline');assert.equal(c.error,5);assert.equal(d.commands().filter(v=>v.op===4).length,2);assert.equal(c.desiredMask,0);assert.equal(c.token,0);
});
test('ambiguous STOP owner timeout never uses the explicit stale-epoch recovery branch',async()=>{
  const {d,c}=await connected();d.ownerDelay=1000;c.panic();await settle(c);assert.equal(c.status,'offline');assert.equal(d.commands().filter(v=>v.op===4).length,1);assert.equal(d.requests.filter(v=>v.path==='/api/v2/session').length,1);assert.match(String(c.error),/timeout/);
});
test('musical BUSY poll feedback keeps performance available without resetting the binding',async()=>{
  const {d,c}=await connected();const token=c.token,messages=[];c.onChange=value=>messages.push(value.message);d.state.fm.flags=11;await c.poll();await settle(c);assert.equal(c.playable,true);assert.equal(c.editable,false);assert.equal(c.token,token);assert.ok(messages.length);for(const message of messages)assert.match(message,/已连接|已采用/);
});
test('a stale rebind session response and its old poll finally cannot overwrite Panic recovery',async()=>{
  const {d,c}=await connected();const request=d.transport.request;let sessionGate,held=false;d.transport.request=async(path,options)=>{const body=await request(path,options);if(path==='/api/v2/session'&&!held){held=true;await new Promise(resolve=>sessionGate=resolve);}return body;};d.state.fm.session_id++;const poll=c.poll();for(let i=0;i<30&&!sessionGate;i++)await turn();assert.ok(sessionGate);const bindingEpoch=c.epoch;c.panic();await settle(c);const token=c.token,state=c.runtimeState,epoch=c.epoch;assert.equal(c.status,'online');assert.ok(epoch>bindingEpoch);assert.ok(d.log.some(v=>v.stage==='owner'&&v.op===4&&v.code===0));sessionGate();await poll;await turn();assert.equal(c.token,token);assert.equal(c.epoch,epoch);assert.deepEqual(c.runtimeState,state);assert.equal(c.status,'online');assert.equal(c.polling,false);
});
for(const action of ['mode','preset','panic','sequence','resume','poll'])test('every '+action+' binding establishes a new local lifecycle epoch',async()=>{
  const {d,c}=await connected();const epoch=c.epoch;
  if(action==='mode')c.mode(1);if(action==='preset')c.preset(1);if(action==='panic')c.panic();if(action==='sequence'){c.sequence=4294967295;c.keys(1);}if(action==='resume'){c.suspend();await c.resume();}if(action==='poll'){d.state.fm.session_id++;await c.poll();}await settle(c);assert.ok(c.epoch>epoch);assert.equal(c.status,'online');assert.equal(c.desiredMask,0);
});
test('suspend from a rebind callback cannot leave an old exhausted-sequence Panic queued',async()=>{
  const {d,c}=await connected();let paused=false;c.onChange=()=>{if(!paused&&c.token!==100&&c.status==='online'&&c.rearming&&c.message.startsWith('正在继续全来源停止')){paused=true;c.suspend();}};c.sequence=4294967295;c.panic();await settle(c);assert.equal(paused,true);assert.equal(c.status,'paused');assert.equal(c.transition,null);assert.equal(c.queued,0);assert.equal(d.commands().filter(v=>v.op===4).length,0);
});
test('Panic from a background owner-state callback fences its superseded adoption',async()=>{
  const {d,c}=await connected();let stopped=false;
  c.onChange=()=>{if(!stopped&&c.desiredMask===1&&c.runtimeState.clients[0].processed===2){stopped=true;d.holdWhen=v=>v.op===4;c.panic();}};
  c.keys(1);await settle(c);d.time+=25;const poll=c.poll();
  for(let i=0;i<60&&!d.gate;i++)await turn();assert.equal(stopped,true);assert.ok(d.gate);
  assert.equal(c.lastAdopted,1);assert.equal(c.playable,false);d.gate();await poll;await settle(c);
  assert.equal(c.status,'online');assert.equal(c.desiredMask,0);
});
test('early rebind onChange suspension prevents new session POST instead of merely discarding it',async()=>{
  const {d,c}=await connected();const sessions=d.requests.filter(v=>v.path==='/api/v2/session').length,commands=d.commands().length;let paused=false;c.onChange=()=>{if(!paused&&c.rearming&&c.message==='正在确认空按键，演奏暂停。'){paused=true;c.suspend('early callback paused');}};d.state.fm.session_id++;await c.poll();await settle(c);assert.equal(paused,true);assert.equal(c.status,'paused');assert.equal(c.message,'early callback paused');assert.equal(d.requests.filter(v=>v.path==='/api/v2/session').length,sessions);assert.equal(d.commands().length,commands+1,'only suspend own release is allowed');assert.equal(d.commands().at(-1).token,100);assert.equal(d.commands().at(-1).a,0);
});
test('initial connection cancelled by early onChange creates no session or command',async()=>{
  const {d,c}=fixture();let paused=false;c.onChange=()=>{if(!paused&&c.rearming&&c.message==='正在确认空按键，演奏暂停。'){paused=true;c.suspend('initial callback paused');}};await c.connect();await settle(c);assert.equal(c.status,'paused');assert.equal(d.requests.filter(v=>v.path==='/api/v2/session').length,0);assert.equal(d.commands().length,0);
});
test('initial onClear cancellation prevents old state installation and session side effects',async()=>{
  const {d,c}=fixture();let paused=false;c.onClear=()=>{if(!paused){paused=true;c.suspend('onClear paused');}};await c.connect();await settle(c);assert.equal(c.status,'paused');assert.equal(c.message,'onClear paused');assert.equal(c.runtimeState,null);assert.equal(d.requests.filter(v=>v.path==='/api/v2/session').length,0);assert.equal(d.commands().length,0);
});
test('rebind onClear fail prevents ghost session and preserves the current failure feedback',async()=>{
  const {d,c}=await connected();let failed=false;const beforeEpoch=c.epoch;const sessions=d.requests.filter(v=>v.path==='/api/v2/session').length;c.onClear=()=>{if(!failed&&c.epoch>beforeEpoch){failed=true;c.fail(Error('callback cancelled binding'));}};d.state.fm.session_id++;await c.poll();await settle(c);assert.equal(c.status,'offline');assert.equal(c.token,0);assert.match(c.message,/callback cancelled binding/);assert.equal(d.requests.filter(v=>v.path==='/api/v2/session').length,sessions);assert.equal(c.runtimeState.fm.session_id,42,'cancelled bind must not install its new native state');
});
for(const callback of ['onClear','onChange'])test('early '+callback+' Panic creates only the current STOP recovery session',async()=>{
  const {d,c}=await connected();const beforeEpoch=c.epoch;const request=d.transport.request,contexts=[];d.transport.request=async(path,options)=>{if(path==='/api/v2/session')contexts.push(d.state.context_epoch);return request(path,options);};let stopped=false;c[callback]=()=>{if(!stopped&&(callback==='onClear'?c.epoch>beforeEpoch:c.rearming&&c.message==='正在确认空按键，演奏暂停。')){stopped=true;assert.equal(c.panic(),true);}};d.state.fm.session_id++;await c.poll();await settle(c);assert.equal(stopped,true);assert.equal(c.status,'online');assert.equal(c.error,null);assert.deepEqual(contexts,[11],'only the new current Panic lifecycle may allocate its post-STOP session');assert.equal(d.requests.filter(v=>v.path==='/api/v2/session').length,2);assert.ok(d.log.some(v=>v.stage==='owner'&&v.op===4&&v.code===0));assert.equal(c.token,101);assert.equal(c.desiredMask,0);
});
test('new user On after an idle empty bitmap is not mistaken for expired held intent',async()=>{
  const {d,c}=await connected();d.time+=1600;assert.equal(c.sentMask,0);assert.equal(c.keys(1),true);await settle(c);assert.equal(c.desiredMask,1);assert.equal(c.sentMask,1);d.time+=25;await c.poll();assert.equal(d.state.clients[0].keys,1);assert.deepEqual(d.commands().map(v=>v.a),[0,1]);
});
test('idle empty heartbeats keep the ten-second HTTP session usable without any NoteOn',async()=>{
  const {d,c}=await connected();const token=c.token;
  for(let i=0;i<6;i++){d.time+=2000;c.tick();await settle(c);await c.poll();}
  assert.equal(c.token,token);assert.equal(c.status,'online');
  assert.ok(d.commands().filter(v=>v.op===3&&v.a===0).length>=7);
  assert.equal(d.commands().filter(v=>v.op===3&&v.a!==0).length,0);
  c.keys(1);await settle(c);assert.equal(c.status,'online');assert.equal(c.sentMask,1);
  d.time+=25;await c.poll();assert.equal(c.lastAdopted,c.lastReceived);
});
test('fresh On after release and another idle interval remains a current user action',async()=>{
  const {d,c}=await connected();c.keys(1);await settle(c);c.keys(0);await settle(c);d.time+=1600;c.selectVoice(2);assert.equal(c.keys(2),true);await settle(c);assert.equal(c.sentMask,2);assert.equal(c.desiredMask,2);assert.equal(d.commands().at(-1).a,2);
});
test('a genuinely queued new On still enforces its 300ms age boundary',async()=>{
  const {d,c}=await connected();d.time+=1600;
  d.holdWhen=v=>v.op===3&&v.a===0&&v.sequence>1;c.releaseOwn();await turn();const gate=d.gate;
  c.keys(1);d.time+=301;gate();await settle(c);
  assert.equal(c.desiredMask,0);assert.equal(d.commands().filter(v=>v.op===3&&v.a).length,0);
});
for(const action of ['preset','panic'])for(const nativeDelay of [0,10,25,80,700])test(action+' native completion delayed '+nativeDelay+'ms converges before current empty handshake',async()=>{
  const {d,c}=await connected(d=>d.nativeDelay=nativeDelay);const token=c.token,received=[];c.onChange=value=>{if(value.runtimeState.mode.switching)received.push({playable:value.playable,adopted:value.lastAdopted,native:value.nativeStatus});};
  assert.equal(action==='preset'?c.preset(1):c.panic(),true);await settle(c);assert.equal(c.status,'online');assert.equal(c.error,null);assert.equal(c.playable,true);assert.notEqual(c.token,token);assert.equal(c.desiredMask,0);assert.equal(c.sentMask,0);assert.equal(c.runtimeState.mode.switching,false);assert.ok(d.nativeLog.some(v=>v.stage==='accepted'));assert.ok(d.nativeLog.some(v=>v.stage==='completed'));if(nativeDelay>0)assert.ok(received.length>0);assert.ok(received.every(v=>!v.playable));assert.equal(d.commands().filter(v=>v.op===3&&v.a).length,0);assert.equal(d.commands().at(-1).token,c.token);assert.equal(d.commands().at(-1).a,0);assert.ok(d.commands().at(-1).at>=d.nativeLog.find(v=>v.stage==='completed').at);assert.doesNotMatch(c.message,/显式选择/);
});
for(const action of ['preset','panic'])test(action+' actual root mode-only 25ms stage cannot invalidate a healthy bind',async()=>{
  const {d,c}=await connected(d=>{d.nativeModeOnly=true;d.nativeDelay=25;});assert.equal(action==='preset'?c.preset(1):c.panic(),true);await settle(c);assert.equal(c.status,'online');assert.equal(c.error,null);assert.equal(c.playable,true);assert.equal(d.commands().filter(v=>v.op===3&&v.a).length,0);
});
for(const blocked of ['session zero','capability unavailable','needs select recovery'])test('safe Panic is adopted without requiring playable FM in '+blocked,async()=>{
  const {d,c}=await connected(d=>{d.nativeDelay=Infinity;if(blocked==='capability unavailable')d.state.fm.available=false;else{d.state.fm.flags=5;d.state.fm.session_id=0;d.state.fm.engine_epoch=0;}if(blocked==='needs select recovery')d.state.mode={requested:2,applied:0,switching:true,recovering:true};});const started=d.time;assert.equal(c.playable,false);assert.equal(c.panic(),true);await settle(c);assert.equal(c.status,'online');assert.equal(c.error,null);assert.equal(c.playable,false);assert.ok(d.log.some(v=>v.stage==='owner'&&v.op===4&&v.code===0));assert.ok(d.time-started<200);assert.equal(d.commands().filter(v=>v.op===5).length,0);
});
test('healthy native transition timeout is bounded and never claims success or replays On',async()=>{
  const {d,c}=await connected(d=>d.nativeDelay=Infinity);const start=d.time;c.preset(1);await settle(c);assert.equal(c.status,'offline');assert.match(String(c.error),/timeout/);assert.ok(d.time-start<3000);assert.equal(c.desiredMask,0);assert.equal(d.commands().filter(v=>v.op===8).length,1);
});
for(const blocked of ['catalog mismatch','dimension mismatch','selected MUTED'])test('Panic remains safe during a native switch from blocked '+blocked,async()=>{
  const {d,c}=await connected(d=>{if(blocked==='catalog mismatch')d.caps.schema_sha256='bad';if(blocked==='dimension mismatch')d.state.fm.patch_operator_count=3;if(blocked==='selected MUTED')d.state.fm.flags=7;});assert.equal(c.playable,false);assert.equal(c.panic(),true);await settle(c);assert.equal(c.status,'online');assert.equal(c.error,null);assert.equal(d.commands().filter(v=>v.op===5).length,0);assert.ok(d.nativeLog.some(v=>v.stage==='completed'&&v.op===4));assert.ok(d.log.some(v=>v.stage==='owner'&&v.op===4&&v.code===0));assert.equal(c.desiredMask,0);if(blocked!=='selected MUTED')assert.equal(c.playable,false);
});
test('background native identity change clears received intent and never replays old On',async()=>{
  const {d,c}=await connected();const token=c.token;
  d.holdWhen=v=>v.op===3&&v.a===1;c.keys(1);await turn();const gate=d.gate;d.state.fm.engine_epoch++;
  gate();await settle(c);await c.poll();await settle(c);
  assert.equal(c.status,'online');assert.notEqual(c.token,token);assert.equal(c.desiredMask,0);
  assert.equal(d.commands().filter(v=>v.op===3&&v.a===1).length,1);
});
test('suspend during explicit native waiting cancels before any new token is allocated',async()=>{
  const {d,c}=await connected(d=>d.nativeDelay=500);let paused=false;c.onChange=()=>{if(!paused&&c.lastAdopted===3&&c.runtimeState.mode.switching){paused=true;c.suspend('native wait paused');}};c.preset(1);await settle(c);assert.equal(paused,true);assert.equal(c.status,'paused');assert.equal(c.message,'native wait paused');assert.equal(d.requests.filter(v=>v.path==='/api/v2/session').length,1);assert.equal(d.commands().filter(v=>v.op===8).length,1);
});
for(const first of ['preset','panic'])for(const nativeDelay of [25,500])test('new Panic preempts pending '+first+' native '+nativeDelay+'ms without losing fresh empty confirmation',async()=>{
  const {d,c}=await connected(d=>d.nativeDelay=nativeDelay);let stopped=false;c.onChange=()=>{if(!stopped&&c.runtimeState.context_epoch===11&&c.runtimeState.mode.switching&&c.lastAdopted>=(first==='preset'?3:2)){stopped=true;assert.equal(c.panic(),true);}};assert.equal(first==='preset'?c.preset(1):c.panic(),true);await settle(c);assert.equal(stopped,true);assert.equal(c.status,'online');assert.equal(c.error,null);assert.equal(c.runtimeState.context_epoch,12);const stops=d.log.filter(v=>v.stage==='owner'&&v.op===4&&v.code===0);assert.equal(stops.length,first==='panic'?2:1);const current=stops.at(-1);assert.equal(current.token,101);assert.equal(current.context,11);assert.equal(c.token,102);assert.equal(c.desiredMask,0);assert.equal(c.playable,true);assert.equal(d.commands().filter(v=>v.op===3&&v.a).length,0);if(nativeDelay===500){const began=d.nativeLog[0].at;const received=d.commands().find(v=>v.op===4&&v.token===101);assert.ok(received.at-began<100,'current safe STOP must not wait 500ms for previous native completion');}
});
test('Panic native completed retains its original epoch while current runtime epoch advances',async()=>{
  const {d,c}=await connected();const epoch=c.runtimeState.fm.engine_epoch;c.panic();await settle(c);assert.equal(c.status,'online');assert.equal(c.runtimeState.fm.engine_epoch,epoch+1);assert.equal(c.nativeStatus.panic.accepted.engine_epoch,epoch);assert.equal(c.nativeStatus.panic.completed.engine_epoch,epoch);assert.equal(c.nativeStatus.panic.result.engine_epoch,epoch);assert.equal(c.nativeStatus.panic.result.code,0);assert.equal(d.commands().filter(v=>v.op===4).length,1);
});
for(const [name,change] of [
  ['native session',state=>state.fm.session_id++],
  ['native epoch',state=>state.fm.engine_epoch++],
  ['topology',state=>state.fm.topology_generation++],
  ['features',state=>state.fm.features=1007],
  ['profile digest',state=>state.fm.profile_sha256='unknown'],
  ['requested engine',state=>state.mode.requested=0]
])test('fresh empty switch convergence still rejects changed '+name,async()=>{
  const {d,c}=fixture(d=>{d.state.mode.switching=true;d.state.fm.flags=15;d.events.push({at:d.time+25,apply:()=>{d.state.mode.switching=false;d.state.fm.flags=3;change(d.state);}});});await c.connect();await settle(c);assert.equal(c.status,'offline');assert.equal(c.error,'runtime identity changed');assert.equal(c.desiredMask,0);assert.ok(d.commands().every(v=>v.op===3&&v.a===0),'failure may send its own best-effort zero, never new input');assert.equal(d.requests.filter(v=>v.path==='/api/v2/session').length,1);
});
