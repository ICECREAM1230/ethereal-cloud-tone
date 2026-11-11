import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {currentCatalogFixture as fixture} from '../mock/fixture-catalog.mjs';
// A missing export is asserted as behavior so the first RED is an assertion, not an import error.
const module = await import('../src/v2-protocol.mjs').catch(() => ({}));
const { validateV2Capabilities, validateV2State, validateV2Session, validateV2Receipt, encodeV2Command, matchCatalogProfile, getFmRuntime } = module;
const pair = () => [fixture('v2-live-capabilities'), fixture('v2-live-state')];
const runtime = () => getFmRuntime(...pair());
const copy = value => structuredClone(value);

test('exports the complete v2 boundary', () => {
  for (const name of ['validateV2Capabilities','validateV2State','validateV2Session','validateV2Receipt','encodeV2Command','matchCatalogProfile','getFmRuntime']) assert.equal(typeof module[name], 'function', name);
});
test('historical catalog captures stay incompatible with the updated presets', () => {
  const raw=name=>JSON.parse(readFileSync(new URL('../artifacts/fm-v2-runtime-20261011/inputs/'+name+'.json',import.meta.url)));
  assert.equal(getFmRuntime(raw('v2-live-capabilities'),raw('v2-live-state')).playable,false);
  assert.equal(runtime().playable,true);
});
test('real handler fixtures parse and live FM supports play/edit despite offline local constant', () => {
  for (const live of ['', 'live-']) {
    const caps = validateV2Capabilities(fixture('v2-' + live + 'capabilities'));
    const state = validateV2State(fixture('v2-' + live + 'state'));
    assert.equal(getFmRuntime(caps, state).playable, live === 'live-');
  }
  const fm = runtime();
  assert.equal(fm.selectable, true); assert.equal(fm.editable, true);
  assert.equal(fm.profile.id, 1); assert.equal(fm.voiceCount, 32);
  assert.deepEqual(fm.operatorsPerVoice, Array(32).fill(4)); assert.equal(fm.patchOperatorCount, 4);
  assert.deepEqual(fm.identity, {contextEpoch:10,sessionId:42,engineEpoch:4,topologyGeneration:1,backendId:1,profileId:1});
});
test('validated data and runtime snapshots cannot alias their mutable input', () => {
  const [caps, state] = pair(); const vc = validateV2Capabilities(caps); const vs = validateV2State(state); const fm = getFmRuntime(vc, vs);
  caps.engines[0].available = false; state.legacy.osc[0][0] = 111; vc.catalog_profiles[0].id = 88; vs.fm.operators_per_voice[0] = 1;
  assert.equal(vs.legacy.osc[0][0], 0); assert.equal(vc.engines[0].available, true); assert.equal(fm.profile.id, 1); assert.equal(fm.operatorsPerVoice[0], 4);
});
test('capabilities require the HTTP envelope but FM digest mismatch preserves DDS', () => {
  const [caps] = pair(); caps.schema_sha256 = 'wrong'; assert.equal(validateV2Capabilities(caps).engines[0].available, true);
  for (const mutate of [c=>c.api_major=1,c=>c.api_minor=1,c=>c.context_epoch=0,c=>c.context_epoch=4294967296,c=>c.limits.clients=5,c=>c.limits.key_count=24,c=>c.limits.lease_ms=1199,c=>c.limits.max_body_bytes=96,c=>c.limits.catalog_page_max=9,c=>c.engines.push(copy(c.engines[0])),c=>c.engines[0].available=1,c=>c.catalog_profiles=Array(17).fill(c.catalog_profiles[0])]) {
    const c = pair()[0]; mutate(c); assert.throws(()=>validateV2Capabilities(c));
  }
});
test('state validates mode, HTTP client receipts and legacy without conflating FM corruption', () => {
  for (const mutate of [s=>s.api_major=1,s=>s.context_epoch=0,s=>s.mode.applied=3,s=>s.mode.switching=1,s=>s.clients.pop(),s=>s.clients[0].token=-1,s=>s.clients[0].keys=33554432,s=>s.clients[0].result_code=14,s=>s.legacy.engine.applied=2]) {
    const s = pair()[1]; mutate(s); assert.throws(()=>validateV2State(s));
  }
  const [caps, state] = pair(); state.fm.operators_per_voice = 'bad'; state.mode={requested:0,applied:0,switching:false,recovering:false}; state.legacy.ready=true;
  assert.equal(validateV2State(state).legacy.ready, true); assert.equal(getFmRuntime(caps,state).playable,false);
});
test('201 session and 202 received receipts require HTTP context and exact accepted sequence', () => {
  const session = {api_major:2,api_minor:0,token:4294967295,client:3,lease_ms:1200,context_epoch:10};
  assert.deepEqual(validateV2Session(session,10),session);
  for (const patch of [{token:0},{token:4294967296},{client:4},{lease_ms:100},{context_epoch:9},{api_minor:1}]) assert.throws(()=>validateV2Session({...session,...patch},10));
  const receipt={api_major:2,accepted:4294967295,stage:'received',context_epoch:10,fpga:'unverified'};
  assert.deepEqual(validateV2Receipt(receipt,4294967295,10),receipt);
  for(const patch of [{accepted:0},{accepted:7},{stage:'completed'},{context_epoch:9},{fpga:'verified'},{api_major:1}]) assert.throws(()=>validateV2Receipt({...receipt,...patch},4294967295,10));
});
test('real READY and MUTED flags support first explicit SELECT before native nonce, but not playing', () => {
  const [caps,state]=pair(); state.mode.requested=0;state.mode.applied=0;state.fm.flags=5;state.fm.session_id=0;state.fm.engine_epoch=0;
  const fm=getFmRuntime(caps,state);assert.equal(fm.selectable,true);assert.equal(fm.playable,false);assert.equal(fm.editable,false);
  assert.equal(fm.reason,'selection_required');
  assert.equal(encodeV2Command(1,1,10,5,2,1,0,fm),'1 1 10 5 2 1 0');
});
test('selected MUTED runtime allows explicit reselect while all performance and edit gates stay closed',()=>{
  const [caps,state]=pair();state.fm.flags=7;const fm=getFmRuntime(caps,state);
  assert.equal(fm.selectable,true);assert.equal(fm.playable,false);assert.equal(fm.editable,false);assert.equal(fm.reason,'engine_muted');
  assert.equal(encodeV2Command(1,1,10,5,2,1,0,fm),'1 1 10 5 2 1 0');
  assert.throws(()=>encodeV2Command(1,2,10,7,18,4294901760,30,fm));assert.throws(()=>encodeV2Command(1,2,10,8,0,0,0,fm));
  state.fm.flags=3;const unmuted=getFmRuntime(caps,state);assert.equal(unmuted.playable,true);assert.equal(unmuted.editable,true);
  state.fm.session_id=0;assert.equal(getFmRuntime(caps,state).playable,false);
});
const gateCases = {
  'schema digest':(c,s)=>c.schema_sha256='bad','profile digest':(c,s)=>c.profiles_sha256='bad','contract digest':(c,s)=>c.contract_sha256='bad',
  'API minor':(c,s)=>c.api_minor=1,'HTTP context':(c,s)=>s.context_epoch=11,'unavailable advertised engine':(c,s)=>c.engines[2].available=false,
  'unavailable advertised profile':(c,s)=>c.catalog_profiles[0].runtime_available=false,'unavailable runtime':(c,s)=>s.fm.available=false,'invalid runtime':(c,s)=>s.fm.valid=false,
  'phase switching':(c,s)=>s.fm.phase=3,'ready absent':(c,s)=>s.fm.flags=2,'fault':(c,s)=>s.fm.flags=19,
  'NoteOn absent':(c,s)=>s.fm.features=1022,'NoteOff absent':(c,s)=>s.fm.features=1021,
  'backend unknown':(c,s)=>s.fm.backend_id=99,'wrong profile':(c,s)=>s.fm.profile_id=2,'wrong version':(c,s)=>s.fm.profile_version=1,'runtime SHA':(c,s)=>s.fm.profile_sha256='bad',
  'wrong voice count':(c,s)=>s.fm.voice_count=7,'wrong operator count':(c,s)=>s.fm.operators_per_voice[7]=3,'wrong patch count':(c,s)=>s.fm.patch_operator_count=3,
  'generation length':(c,s)=>s.fm.voice_generations.pop(),'generation type':(c,s)=>s.fm.voice_generations[0]=-1,'source denominator':(c,s)=>s.fm.source_rate.denominator=0,
  'output denominator':(c,s)=>s.fm.output_rate.denominator=0,'invalid result identity':(c,s)=>s.fm.accepted.sequence_id=-1
};
for(const [name,mutate] of Object.entries(gateCases)) test('closes FM for '+name+' while DDS state remains parseable',()=>{
  const [caps,state]=pair(); mutate(caps,state);state.legacy.ready=true;assert.equal(validateV2State(state).legacy.ready,true);
  const fm=getFmRuntime(caps,state);assert.equal(fm.selectable,false);assert.equal(fm.playable,false);assert.ok(fm.reason);
});
for(const [name, mutate] of Object.entries({selected:(c,s)=>s.fm.flags=1,session:(c,s)=>s.fm.session_id=0,epoch:(c,s)=>s.fm.engine_epoch=0,topology:(c,s)=>s.fm.topology_generation=0,requested:(c,s)=>s.mode.requested=0,applied:(c,s)=>s.mode.applied=0,switching:(c,s)=>s.mode.switching=true,recovering:(c,s)=>s.mode.recovering=true})) test('new playing is fenced by '+name,()=>{const [c,s]=pair();mutate(c,s);assert.equal(getFmRuntime(c,s).playable,false);});
test('MSFA is offline even if a host falsely advertises runtime availability',()=>{
 const [c,s]=pair();c.catalog_profiles[1].runtime_available=true;Object.assign(s.fm,{backend_id:2,profile_id:2,patch_operator_count:2,operators_per_voice:[2,2,2,2,2,2,2,2]});assert.equal(getFmRuntime(c,s).selectable,false);
});
test('features gate parameters and presets independently, and result triples retain old native identity',()=>{
 const [c,s]=pair(); s.fm.accepted={session_id:12,engine_epoch:2,sequence_id:99};s.fm.panic={accepted:{session_id:4,engine_epoch:1,sequence_id:8},completed:null,result:null};
 const validated=validateV2State(s);assert.deepEqual(validated.fm.accepted,{session_id:12,engine_epoch:2,sequence_id:99});assert.equal(getFmRuntime(c,validated).playable,true);
 s.fm.features=1007;const noParameter=getFmRuntime(c,s);assert.equal(noParameter.playable,true);assert.equal(noParameter.editable,false);assert.throws(()=>encodeV2Command(1,1,10,7,1,4294967295,7,noParameter));assert.equal(encodeV2Command(1,1,10,8,0,0,0,noParameter),'1 1 10 8 0 0 0');
 s.fm.features=991;const noPreset=getFmRuntime(c,s);assert.equal(noPreset.editable,true);assert.throws(()=>encodeV2Command(1,1,10,8,0,0,0,noPreset));
});
test('profile IDs are independent of backend IDs and matching returns a stable snapshot',()=>{
 const profiles=[{id:99,backend_id:1,profile_version:65536,profile_sha256:'digest',voice_count:8}];const matched=matchCatalogProfile(profiles,1,65536,'digest');assert.equal(matched.id,99);profiles[0].id=1;assert.equal(matched.id,99);
 assert.equal(matchCatalogProfile(profiles,99,65536,'digest'),null);assert.equal(matchCatalogProfile(profiles,1,1,'digest'),null);assert.equal(matchCatalogProfile(profiles,1,65536,'bad'),null);
});
test('seven decimal uint32 HTTP commands enforce reserved fields and musical limits',()=>{
 const valid=[[2,7,0,0],[2,7,127,0],[3,33554431,127,0],[3,0,1,0],[4,0,0,0],[5,0,0,0],[5,1,0,0],[6,200,0,0],[6,8000,0,0]];
 for(const fields of valid)assert.equal(encodeV2Command(4294967295,4294967295,4294967295,...fields),[4294967295,4294967295,4294967295,...fields].join(' '));
 for(const fields of [[2,1,1,0],[2,7,128,0],[2,7,1,1],[3,33554432,1,0],[3,1,0,0],[3,1,128,0],[3,1,1,1],[4,1,0,0],[4,0,1,0],[4,0,0,1],[5,0,1,0],[5,1,0,1],[5,3,0,0],[5,2,0,0],[6,199,0,0],[6,8001,0,0],[6,3000,1,0],[6,3000,0,1],[1,0,0,0]])assert.throws(()=>encodeV2Command(1,1,10,...fields));
 for(const fields of [[0,1,10,4,0,0,0],[1,0,10,4,0,0,0],[1,1,0,4,0,0,0],[1,1,10,4,-1,0,0],[1,1,10,4,0,0,4294967296],[1,1,10,4,0.5,0,0],['1',1,10,4,0,0,0]])assert.throws(()=>encodeV2Command(...fields));
});
test('FM encoding uses runtime profile, actual patch dimensions and generated parameter ranges',()=>{
 const fm=runtime(); assert.equal(encodeV2Command(1,7,10,7,1,4294967295,7,fm),'1 7 10 7 1 4294967295 7');assert.equal(encodeV2Command(1,8,10,7,18,4294901763,31,fm),'1 8 10 7 18 4294901763 31');assert.equal(encodeV2Command(1,9,10,8,2,0,0,fm),'1 9 10 8 2 0 0');
 for(const fields of [[7,1,4294967295,8],[7,1,0,1],[7,18,4294901764,31],[7,18,4294967295,31],[7,18,3,31],[7,999,4294967295,0],[8,99,0,0],[8,0,1,0],[8,0,0,1],[5,2,2,0]])assert.throws(()=>encodeV2Command(1,1,10,...fields,fm));
 for(const fields of [[7,1,4294967295,1],[8,0,0,0],[5,2,1,0]])assert.throws(()=>encodeV2Command(1,1,10,...fields));
 assert.throws(()=>encodeV2Command(1,1,11,7,1,4294967295,1,fm));const smaller={...fm,patchOperatorCount:3};assert.throws(()=>encodeV2Command(1,1,10,7,18,4294901763,31,smaller));
});
test('ratios permit uint32 zero numerators, but require nonzero denominators',()=>{
 const [c,s]=pair();s.fm.source_rate.numerator=0;s.fm.output_rate.numerator=0;assert.equal(getFmRuntime(c,s).playable,true);
});
test('sparse client and voice topology arrays cannot hide absent entries',()=>{
 const s=pair()[1]; delete s.clients[2];assert.throws(()=>validateV2State(s));
 for(const field of ['operators_per_voice','voice_generations']) { const [c,state]=pair();delete state.fm[field][3];assert.equal(getFmRuntime(c,state).playable,false); }
});
test('missing or non-string FM reason closes the descriptor without losing DDS',()=>{
 for(const reason of [undefined,8,{}]) {const [c,s]=pair();s.fm.reason=reason;assert.equal(validateV2State(s).legacy.engine.applied,0);assert.equal(getFmRuntime(c,s).playable,false);}
});
test('ABI 1.1 reserved flags and feature bits cannot advertise unknown capability',()=>{
 for(const [field,value] of [['flags',35],['features',2047]]) {const [c,s]=pair();s.fm[field]=value;assert.equal(getFmRuntime(c,s).playable,false);}
});
test('NEXT_NOTE parameter feature is required for this generated next-note catalog',()=>{
 const [c,s]=pair();s.fm.features=767;const fm=getFmRuntime(c,s);assert.equal(fm.playable,true);assert.equal(fm.editable,false);assert.throws(()=>encodeV2Command(1,1,10,7,1,4294967295,7,fm));
});
