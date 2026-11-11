import test from 'node:test';
import assert from 'node:assert/strict';
import {CC_ALLOWED,KEY_MASK,encodeCommand,decodeCommand,validateState,engineBlocked,createTransport,postOptions} from '../src/protocol.mjs';
import {CONTROLS,readControl} from '../src/controls.mjs';
import {initialState,applyCC} from '../mock/device.mjs';
import {KeyContacts} from '../src/keyboard.mjs';

test('strict five-uint32 encoding and frozen CC whitelist',()=>{
  assert.equal(encodeCommand(4294967295,4294967295,3,KEY_MASK,127),'4294967295 4294967295 3 33554431 127');
  for(const cc of CC_ALLOWED)assert.equal(decodeCommand(encodeCommand(4,1,2,cc,127)).a,cc);
  for(const values of [[0,1,3,0,100],[1,0,3,0,100],[1,1,2,64,127],[1,1,2,120,0],[1,1,3,KEY_MASK+1,100],[1,1,3,0,0],[1,1,3,0,100.1],[1,1,5,2,0],[1,1,6,199,0],[1,1,6,8001,0],[1,1,4,1,0],[1,1,1,0,0],[4294967296,1,3,0,100]])assert.throws(()=>encodeCommand(...values));
});
test('reject extra spaces, newline, NUL, suffix, exponent, signs and overflow',()=>{
  for(const body of ['1 1 3 0 100\n',' 1 1 3 0 100','1 1 3 0 100 ','1  1 3 0 100','1\t1 3 0 100','1 1 3 0 100\0','1 1 3 0 100a','1 1 3 -1 100','+1 1 3 0 100','1e2 1 3 0 100','4294967296 1 3 0 100','1 1 3 0 100 1'])assert.throws(()=>decodeCommand(body));
  assert.equal(decodeCommand('0001 0001 3 0 100').token,1,'firmware accepts leading zeroes');
});
test('parameter inventory exactly covers whitelist and inverses preserve representative mappings',()=>{
  assert.deepEqual(CONTROLS.map(c=>c.cc).sort((a,b)=>a-b),[...CC_ALLOWED]);
  for(const cc of CC_ALLOWED)for(const value of [0,32,64,96,127]){
    const state=initialState();applyCC(state,cc,value);
    const control=CONTROLS.find(c=>c.cc===cc);const view=readControl(control,state);
    assert(Number.isInteger(view.value)&&view.value>=0&&view.value<=127,cc+' inverse');
    assert(!/NaN|undefined|Infinity/.test(view.text),cc+' display');
    const check=structuredClone(state);applyCC(check,cc,view.value);
    if(![76,109].includes(cc))assert.deepEqual(check,state,cc+' representative inverse');
  }
  const state=initialState();applyCC(state,76,127);
  assert.equal(state.lfo[0][1],10486,'30 Hz converts to a rounded phase increment');
  assert.equal(readControl(CONTROLS.find(c=>c.cc===76),state).text,'30.00 Hz');
});
test('state schema and legacy DDS capability fallback',()=>{
  const state=initialState();assert.equal(validateState(state),state);assert.equal(engineBlocked(state),false);
  for(const flag of ['switching','recovering']){const blocked=initialState();blocked.engine[flag]=true;assert.equal(engineBlocked(blocked),true);}
  state.engine.available=false;state.engine.valid=false;assert.equal(engineBlocked(state),false);delete state.engine;assert.equal(engineBlocked(state),false);
  assert.throws(()=>validateState({...state,tokens:[1]}));assert.throws(()=>validateState({...state,mod:null}));assert.throws(()=>validateState({...state,keys:[33554432,0,0,0]}));
  const malformed=initialState();malformed.engine.available='false';assert.throws(()=>validateState(malformed));
});
test('same-key contacts, distinct pointers and full 25-bit mask',()=>{
  const contacts=new KeyContacts();contacts.press('p1',0);contacts.press('p2',0);contacts.release('p1');assert.equal(contacts.mask,1);contacts.press('p3',24);assert.equal(contacts.mask,(1<<24)|1);contacts.release('p2');assert.equal(contacts.mask,1<<24);contacts.clear();assert.equal(contacts.mask,0);
});
test('transport control header, empty session and keepalive release',async()=>{
  const requests=[];const transport=createTransport(async(path,options)=>{requests.push({path,options});return{ok:true,json:async()=>({})};});
  await transport.request('/api/session',postOptions(''));await transport.release('1 1 3 0 100');
  assert.equal(requests[0].options.body,'');assert.equal(requests[0].options.headers['X-FPGA-Control'],'1');assert.equal(requests[1].options.keepalive,true);assert.equal(requests[1].options.headers['X-FPGA-Control'],'1');
});
