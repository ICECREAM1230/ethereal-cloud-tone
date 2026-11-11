import test from 'node:test';
import assert from 'node:assert/strict';
const load=()=>import('../src/raw-midi.mjs').catch(error=>assert.fail('raw MIDI module unavailable: '+error.code));
test('channel messages retain velocity zero, CC and fourteen-bit bend bytes',async()=>{
  const m=await load();
  assert.deepEqual(m.midiNoteOn(15,60,0),[159,60,0]);
  assert.deepEqual(m.midiNoteOff(7,60,64),[135,60,64]);
  assert.deepEqual(m.midiCC(3,64,127),[179,64,127]);
  assert.deepEqual(m.midiPitchBend(0,0),[224,0,0]);
  assert.deepEqual(m.midiPitchBend(0,8192),[224,0,64]);
  assert.deepEqual(m.midiPitchBend(15,16383),[239,127,127]);
  for(const args of [[16,60,100],[0,-1,1],[0,60,128],[0,60,1.5]])assert.throws(()=>m.midiNoteOn(...args));
  assert.throws(()=>m.midiNoteOn(0,60));assert.throws(()=>m.midiCC(0,7));
  for(const value of [-1,16384,1.5])assert.throws(()=>m.midiPitchBend(0,value));
});
test('raw JSON binds web source and context without rewriting running status, realtime or SysEx',async()=>{
  const m=await load(),bytes=[144,60,0,62,100,248,240,125,1,247];
  const body=m.encodeRawMidi(42,9,5,bytes);bytes[0]=0;
  assert.deepEqual(JSON.parse(body),{source:'web',bytes:[144,60,0,62,100,248,240,125,1,247],token:42,sequence:9,context_epoch:5});
  assert.equal(JSON.parse(m.encodeRawMidi(42,10,5,Array(256).fill(255))).bytes.length,256);
});
test('malformed or oversized bytes and invalid bindings never become a request body',async()=>{
  const m=await load();
  for(const bytes of [[],Array(257).fill(0),[256],[-1],[1.2],[NaN],['144'],Array(3),new Uint8Array([144,60,100]),null])assert.throws(()=>m.encodeRawMidi(1,1,1,bytes));
  for(const args of [[0,1,1],[1,0,1],[1,1,0],[1,4294967296,1]])assert.throws(()=>m.encodeRawMidi(...args,[248]));
});
test('raw receipt confirms only the complete queued byte count',async()=>{
  const m=await load();assert.deepEqual(m.validateRawMidiReceipt({accepted:true,queued_bytes:3},3),{accepted:true,queued_bytes:3});
  for(const value of [{accepted:1,queued_bytes:3},{accepted:true,queued_bytes:2},{accepted:true,queued_bytes:0},{accepted:false,queued_bytes:3},null])assert.throws(()=>m.validateRawMidiReceipt(value,3));
});
