import test from 'node:test';
import assert from 'node:assert/strict';
import {getAdditivePreset} from '../src/additive.mjs';
const load=()=>import('../mock/additive-device.mjs').catch(e=>assert.fail('additive HTTP model missing: '+e.code));
async function device(){const {AdditiveDevice}=await load();let time=10;const d=new AdditiveDevice({now:()=>time});const session=d.session('','1').body;
  d.command([session.token,1,session.context_epoch,5,3,0,0].join(' '),'1');time+=30;d.advance();time+=110;d.advance();
  const current=d.session('','1').body;
  return {d,session:current,advance:ms=>{time+=ms;d.advance();}};
}
test('mock announces the additive extension without changing legacy state shape',async()=>{
  const {d}=await device();assert.equal(d.state.mode.applied,3);assert.equal(d.state.legacy.ready,false);assert.ok([0,1].includes(d.state.legacy.engine.applied));assert.equal(d.capabilities().additive.partials,32);
});
test('patch HTTP receipt precedes owner and COMMIT confirmation',async()=>{
  const {d,session:s,advance}=await device(),patch=getAdditivePreset('bell');
  const result=d.additivePatch(JSON.stringify({contract_version:1,token:s.token,sequence:2,context_epoch:s.context_epoch,patch}),'1');
  assert.equal(result.status,202);assert.equal(d.state.additive.patch.partials[1].ratio_q16,131072);advance(30);assert.equal(d.state.additive.busy,true);
  advance(110);assert.equal(d.state.additive.busy,false);assert.deepEqual(d.state.additive.patch,patch);assert.deepEqual(d.state.additive.applied,{token:s.token,sequence:2,context_epoch:s.context_epoch,commit_id:1});
  assert.notEqual(d.state.context_epoch,s.context_epoch);
});
test('invalid patch, missing header and stale binding are independently rejected',async()=>{
  const {d,session:s}=await device(),body={contract_version:1,token:s.token,sequence:2,context_epoch:s.context_epoch,patch:getAdditivePreset('organ')};
  assert.equal(d.additivePatch(JSON.stringify(body),null).status,403);
  const bad=structuredClone(body);bad.patch.partials[31].group=4;assert.equal(d.additivePatch(JSON.stringify(bad),'1').status,400);
  assert.equal(d.additivePatch(JSON.stringify({...body,context_epoch:s.context_epoch+1}),'1').status,409);
  assert.equal(d.additivePatch(JSON.stringify({...body,token:1}),'1').status,410);assert.equal(d.sessions.find(x=>x?.token===s.token).sequence,0);
});
