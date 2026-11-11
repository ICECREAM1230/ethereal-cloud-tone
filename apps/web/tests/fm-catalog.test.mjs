import assert from 'node:assert/strict';
import test from 'node:test';
import {getProfile} from '../src/generated/fm-contract.mjs';
import * as contract from '../src/generated/fm-contract.mjs';

let catalog;
try { catalog = await import('../src/fm-catalog.mjs'); }
catch(error) {
  if(error.code!=='ERR_MODULE_NOT_FOUND'||error.url!==new URL('../src/fm-catalog.mjs',import.meta.url).href)throw error;
}
const preview=(...args)=>catalog?.createFmCatalogPreview(...args);
const metadata=(...args)=>catalog?.getFmCatalogMetadata(...args);

test('JT51 catalog keeps thirty-two voice layouts separate from four shared patch operators',()=>{
  const view=preview(1);
  assert.equal(view?.voices?.length,32);
  assert.deepEqual(view.voices.map(voice=>voice.operators.length),Array(32).fill(4));
  assert.equal(view.patchOperators.length,4);
  assert.deepEqual(view.patchOperators.map(operator=>operator.label),['M1','C1','M2','C2']);
  assert.deepEqual(view.parameters.map(parameter=>parameter.id),['1','2']);
  assert.deepEqual(view.patchOperators[0].parameters.map(parameter=>parameter.id),['16','17','18','19','20','21','22','23','24','25','26']);
  assert(view.voices.every(voice=>voice.parameters.length===0&&voice.algorithm===undefined&&voice.operators.every(operator=>operator.parameters.length===0)));
  assert.equal(view.commonLabel,'共享音色（Patch）');
});
test('every catalog parameter stays unread, numeric and read-only with truthful code units',()=>{
  const view=preview(1);assert.equal(view?.parameters?.length,2);
  const parameters=[...view.parameters,...view.patchOperators.flatMap(operator=>operator.parameters)];
  assert.equal(parameters.length,46);
  assert(parameters.every(parameter=>parameter.value===null&&parameter.editable===false&&parameter.kind==='number'&&parameter.step===1&&parameter.unit==='芯片代码'&&parameter.hint.includes('next_note')));
  assert.deepEqual(parameters.filter(parameter=>parameter.id==='1').map(parameter=>[parameter.min,parameter.max]),[[0,7]]);
  assert.deepEqual(view.patchOperators[0].parameters.map(parameter=>[parameter.min,parameter.max]),[[0,15],[0,127],[0,31],[0,31],[0,31],[0,15],[0,15],[0,7],[0,3],[0,3],[0,1]]);
  assert.equal(view.parameters[0].options,undefined,'algorithm codes have no invented topology names');
  assert.equal(view.patchOperators[0].parameters.at(-1).kind,'number','u32 am_enable is not silently converted to bool');
  assert.equal(view.editable,false);assert.equal(view.available,true,'offline catalog can be browsed without granting runtime capability');
  assert(view.reason.includes('当前参数值尚未读取')&&view.reason.includes('FM 演奏尚未启用'));
});
test('changing catalog voice leaves all shared patch identities and values unchanged',()=>{
  const first=preview(1,0);const last=preview(1,7);
  assert.equal(first?.selectedVoiceId,'voice:0');assert.equal(last.selectedVoiceId,'voice:7');
  assert.equal(first.contextId,last.contextId);
  assert.deepEqual(first.patchOperators,last.patchOperators);
  assert.deepEqual(first.parameters,last.parameters);
  assert(first.contextId.startsWith('catalog:'),'catalog identity cannot impersonate a live generation');
});
test('MSFA eight-by-two reference has independent patch count and no parameter or preset values',()=>{
  const view=preview(2);assert.equal(view?.voices?.length,8);
  assert.deepEqual(view.voices.map(voice=>voice.operators.length),[2,2,2,2,2,2,2,2]);
  assert.equal(view.patchOperators.length,2);assert.deepEqual(view.parameters,[]);
  assert(view.patchOperators.every(operator=>operator.parameters.length===0));
  assert(view.reason.includes('离线参考'));assert.equal(view.available,true);assert.equal(view.editable,false);
  assert.deepEqual(metadata(2).presets,[]);
});
test('unknown and malformed profile IDs reject rather than selecting a default backend',()=>{
  for(const id of [0,3,-1,1.5,'1',true,null,NaN,Infinity])assert.throws(()=>preview(id),RangeError);
});
test('invalid selected voice index rejects rather than clamping or inferring another target',()=>{
  for(const index of [-1,32,1.5,'0',false,null,NaN,Infinity])assert.throws(()=>preview(1,index),RangeError);
});
test('metadata lists five presets as documentation without patch values or apply commands',()=>{
  const info=metadata(1);assert.equal(info?.parameterCount,13);
  assert.equal(info.schemaVersion,'1.0.1');assert.equal(info.profileId,1);assert.equal(info.runtimeAvailable,false);
  assert.deepEqual(info.presets.map(preset=>[preset.id,preset.name]),[[0,'diagnostic_sine'],[1,'two_op_keys'],[2,'four_op_bell'],[3,'electric_piano_tine'],[4,'metal_bell']]);
  assert(info.presets.every(preset=>typeof preset.description==='string'&&!('values' in preset)&&!('command' in preset)&&!('key_on_logical_mask' in preset)));
  assert.equal(info.profileSha256,getProfile(1).profile_sha256);
  assert.throws(()=>metadata('1'),RangeError);
});
test('consumer edits to preview copies cannot alter generated catalog or a fresh unread preview',()=>{
  const view=preview(1);assert.equal(view?.patchOperators?.length,4);
  const before=JSON.stringify(getProfile(1));
  view.patchOperators[0].parameters[0].value=9;
  view.voices[0].operators[0].label='local view edit';
  assert.equal(preview(1).patchOperators[0].parameters[0].value,null);
  assert.equal(preview(1).voices[0].operators[0].label,'M1');
  assert.equal(JSON.stringify(getProfile(1)),before);
});
test('runtime masks and static dimensions come from the revised generated contract',()=>{
  const flags=['READY','SELECTED','MUTED','BUSY','FAULT'].map(name=>contract['FM_FLAG_'+name]);
  const features=['NOTE_ON','NOTE_OFF','SET_PITCH','SET_GAIN_PAN','SET_PARAMETER','APPLY_PRESET','RELEASE_ALL','PRIORITY_PANIC','NEXT_NOTE_PARAMETERS','NATIVE_STEREO_PAN'].map(name=>contract['FM_FEATURE_'+name]);
  for(const masks of [flags,features]){
    assert(masks.every(value=>Number.isInteger(value)&&value>0&&value<=0xffffffff&&(value&(value-1))===0));
    assert.equal(new Set(masks).size,masks.length);
  }
  assert.notEqual(contract.FM_FLAG_READY,contract.FM_FLAG_SELECTED,'ready and selected remain distinct capabilities');
  for(const [profileId,prefix] of [[1,'JT51_OPM'],[2,'MSFA_TWO_OP_REFERENCE']]){
    const profile=getProfile(profileId);
    assert.equal(contract['FM_PROFILE_'+prefix+'_VOICE_COUNT'],profile.voice_count);
    assert.equal(contract['FM_PROFILE_'+prefix+'_PATCH_OPERATOR_COUNT'],profile.patch_operator_count);
    assert.equal(contract['FM_PROFILE_'+prefix+'_MAX_OPERATOR_COUNT'],Math.max(...profile.operators_per_voice));
  }
  assert.equal(contract.FM_COMMAND_WORD_COUNT,12,'the normalized masks do not change the frozen command layout');
});
