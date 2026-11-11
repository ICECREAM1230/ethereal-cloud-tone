import test from 'node:test';
import assert from 'node:assert/strict';
import {readPresetImport,presetImportTarget} from '../src/dds-import.mjs';
const file=(name,bytes)=>({name,size:bytes.length,arrayBuffer:async()=>bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength)});

test('local import reads strict UTF-8 and keeps TOML parameters verbatim',async()=>{
  const text='format = 1\n# 外部音色\n[filter]\ncutoff = 1234.5\n';
  const result=await readPresetImport(file('温暖.TOML',Buffer.from(text)));
  assert.deepEqual(result,{name:'温暖.TOML',bytes:Buffer.byteLength(text),text});
});
test('local import accepts a 16384-byte LF-terminated file and rejects an unpersistable boundary',async()=>{
  const bytes=Buffer.from('format = 1\n#'+'x'.repeat(16371)+'\n');assert.equal(bytes.length,16384);
  assert.equal((await readPresetImport(file('Full.toml',bytes))).bytes,16384);
  await assert.rejects(readPresetImport(file('Full.toml',Buffer.alloc(16384,120))),/换行/);
  await assert.rejects(readPresetImport(file('Large.toml',Buffer.alloc(16385,120))),/16 KiB/);
});
test('local import rejects wrong extension, empty files, NUL and invalid UTF-8 before a request',async()=>{
  for(const [name,bytes] of [['a.sf2',Buffer.from('format=1')],['a.toml',Buffer.alloc(0)],['a.toml',Buffer.from([0xc3,0x28])],['a.toml',Buffer.from('format=1\0')]])await assert.rejects(readPresetImport(file(name,bytes)));
});
test('target is always User with firmware UTF-8 byte and name validation',()=>{
  assert.equal(presetImportTarget('我的音色','外部.TOML'),'Patches/User/我的音色/外部.toml');
  for(const [category,name] of [['../Factory','a'],['User','a/b'],['User',''],['User','弦'.repeat(22)],['User','a.']])assert.throws(()=>presetImportTarget(category,name));
});
