import test from 'node:test';
import assert from 'node:assert/strict';
import {getFmRuntime,encodeV2Command} from '../src/v2-protocol.mjs';
import {createFmRuntimeView} from '../src/fm-runtime-view.mjs';
import {RuntimeController} from '../src/runtime-controller.mjs';
import {RuntimeDevice,settle} from './helpers/runtime-device.mjs';
import {currentCatalogFixture as fixture} from '../mock/fixture-catalog.mjs';
import {V2Device} from '../mock/v2-device.mjs';

// A bound READY mailbox may be BUSY while it finishes an ordinary musical
// command. SELECT/Panic/recovery have their own mode, phase and mute fences.
test('ordinary musical BUSY keeps performance available while SELECT and edits wait for idle',()=>{
  const caps=fixture('v2-live-capabilities'),state=fixture('v2-live-state');
  state.fm.flags=11; // READY | SELECTED | BUSY
  const runtime=getFmRuntime(caps,state);
  assert.equal(runtime.playable,true);
  assert.equal(runtime.selectable,false);
  assert.equal(runtime.editable,false);
  assert.equal(runtime.reason,null);
  assert.equal(encodeV2Command(100,2,10,3,3,100,0,runtime),'100 2 10 3 3 100 0');
  for(const args of [[5,2,1,0],[7,18,4294901760,30],[8,1,0,0]])
    assert.throws(()=>encodeV2Command(100,2,10,...args,runtime));
  const view=createFmRuntimeView(caps,state,{writable:true});
  assert.equal(view.model.waiting,true);
  assert.equal(view.model.editable,false);
  assert.equal(view.presetEditable,false);
});

test('rapid On Off and re-press during ordinary BUSY retain one binding and every release boundary',async()=>{
  const d=new RuntimeDevice();let clears=0;
  const c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms),onClear:()=>clears++});
  await c.connect();const token=c.token,before=clears;
  d.state.fm.flags=11;
  await c.poll();
  assert.equal(c.playable,true);
  assert.equal(c.canSelectMode(2,1),false);
  assert.equal(c.preset(1),false);
  for(const mask of [1,3,2,0,2,0,1,0]){
    assert.equal(c.keys(mask),true,'current input remains accepted during music BUSY');
    await settle(c);d.time+=25;
    await c.poll();
  }
  assert.equal(c.status,'online');assert.equal(c.error,null);assert.equal(c.token,token);
  assert.equal(clears,before);assert.equal(c.desiredMask,0);assert.equal(c.sentMask,0);
  assert.deepEqual(d.commands().filter(command=>command.sequence>1).map(command=>command.a),[1,3,2,0,2,0,1,0]);
  assert.equal(c.lastAdopted,c.lastReceived);
});

test('loopback HTTP model queues musical BUSY work while explicit SELECT needs idle',()=>{
  let now=0;const d=new V2Device({now:()=>now,ownerDelay:10});
  const session=d.session('','1').body;
  const send=(sequence,op,a,b=0,c=0)=>d.command(`${session.token} ${sequence} ${session.context_epoch} ${op} ${a} ${b} ${c}`,'1');
  assert.equal(send(1,3,0,100).status,202);now+=10;d.advance();
  d.configure({busy:true});
  assert.equal(send(2,3,1,100).status,202);
  now+=10;d.advance();assert.equal(d.state.clients[0].keys,1);
  assert.equal(send(3,5,2,1).status,503);
  assert.equal(send(3,3,0,100).status,202);
  now+=10;d.advance();assert.equal(d.state.clients[0].keys,0);
  // The real server may queue these too; the web UI chooses to wait for idle.
  assert.equal(send(4,7,18,4294901760,30).status,202);
  assert.equal(send(5,8,1).status,202);
});

for(const [name,mutate] of [
  ['MUTED',s=>s.fm.flags=15],['FAULT',s=>s.fm.flags=27],['READY lost',s=>s.fm.flags=10],
  ['switching phase',s=>s.fm.phase=3],['recovering mode',s=>s.mode.recovering=true],
  ['switching mode',s=>s.mode.switching=true],['unbound session',s=>s.fm.session_id=0],
  ['missing epoch',s=>s.fm.engine_epoch=0],['missing topology',s=>s.fm.topology_generation=0],
  ['profile mismatch',s=>s.fm.profile_sha256='wrong']
])test(`musical BUSY never bypasses ${name}`,()=>{
  const caps=fixture('v2-live-capabilities'),state=fixture('v2-live-state');state.fm.flags=11;mutate(state);
  const runtime=getFmRuntime(caps,state);
  assert.equal(runtime.playable,false);assert.equal(runtime.editable,false);
});
