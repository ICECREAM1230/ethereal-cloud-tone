import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const load=()=>import('../src/additive.mjs').catch(error=>assert.fail('additive module missing: '+error.code));
const presets=JSON.parse(readFileSync(new URL('../contracts/additive/20261019/presets.json',import.meta.url)));
test('additive examples retain exact FPGA fixed-point values, including Python rounding',async()=>{
  const m=await load();for(const id of ['organ','soft-pad','bell'])assert.deepEqual(m.getAdditivePreset(id),presets[id]);
  const first=m.getAdditivePreset('organ');first.partials[0].weight=0;assert.equal(m.getAdditivePreset('organ').partials[0].weight,65535);
  assert.equal(m.getAdditivePreset('missing'),null);assert.equal(presets.organ.partials[1].weight,45874);
});
test('patch validation covers all 32 partials, four envelopes, fixed point and sparse arrays',async()=>{
  const m=await load(),patch=m.validateAdditivePatch(presets.organ);patch.partials[0].weight=0;assert.equal(presets.organ.partials[0].weight,65535);
  for(const bad of [null,{...presets.organ,partials:Array(32)},{...presets.organ,envelopes:Array(4)},{...presets.organ,centroid_q8:8192},{...presets.organ,bend_range:128}])assert.throws(()=>m.validateAdditivePatch(bad));
  for(const [field,value] of [['ratio_q16',16777216],['weight',65536],['group',4],['weight','1']]){
    const bad=structuredClone(presets.organ);bad.partials[0][field]=value;assert.throws(()=>m.validateAdditivePatch(bad));
  }
  const bad=structuredClone(presets.organ);bad.envelopes[3].release_inc=-1;assert.throws(()=>m.validateAdditivePatch(bad));
});
test('envelope timing uses the advertised actual rate; zero holds and overflow is rejected',async()=>{
  const m=await load(),rate={numerator:91015625,denominator:948};
  assert.equal(m.envelopeIncrement(2,rate),342);assert.equal(m.envelopeIncrement(0,rate),0);
  assert.equal(m.envelopeMilliseconds(0,rate),0);assert.ok(Math.abs(m.envelopeMilliseconds(342,rate)-1.996)<0.01);
  assert.throws(()=>m.envelopeIncrement(1000,rate));assert.throws(()=>m.envelopeIncrement(2,{numerator:1,denominator:0}));
});
test('additive capability and state gates are separate from FM and reject unknown versions',async()=>{
  const m=await load(),caps={context_epoch:10,engines:[{id:3,available:true}],additive:m.ADDITIVE_CAPABILITY};
  const state={context_epoch:10,mode:{requested:3,applied:3,switching:false,recovering:false},additive:{contract_version:1,available:true,valid:true,ready:true,busy:false,instance_id:1,revision:1,patch:presets.organ,applied:null}};
  const r=m.getAdditiveRuntime(caps,state,'0:100');assert.equal(r.playable,true);assert.equal(r.editable,true);assert.deepEqual(r.patch,presets.organ);
  assert.notEqual(r.contextId,m.getAdditiveRuntime(caps,state,'0:101').contextId);
  assert.equal(m.getAdditiveRuntime({...caps,additive:{...caps.additive,contract_version:2}},state).available,false);
  assert.equal(m.getAdditiveRuntime(caps,{...state,mode:{...state.mode,recovering:true}}).playable,false);
  assert.equal(m.getAdditiveRuntime(caps,{...state,additive:{...state.additive,busy:true}}).editable,false);
  assert.equal(m.getAdditiveRuntime({...caps,engines:[]},state).available,false);
});
test('patch body preserves binding and contains no SPI address or implicit engine selection',async()=>{
  const m=await load();const body=m.encodeAdditivePatch(100,2,10,presets.bell);
  assert.deepEqual(JSON.parse(body),{contract_version:1,token:100,sequence:2,context_epoch:10,patch:presets.bell});
  assert.ok(body.length<=4096);assert.doesNotMatch(body,/address|selected/);assert.throws(()=>m.encodeAdditivePatch(0,2,10,presets.organ));
});
