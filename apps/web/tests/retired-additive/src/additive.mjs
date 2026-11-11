import {uint32} from './protocol.mjs';
import {ADDITIVE_PRESET_DATA} from './generated/additive-presets.mjs';

export const ADDITIVE_ENGINE_ID=3;
export const ADDITIVE_PATCH_PATH='/api/v2/additive/patch';
export const ADDITIVE_CAPABILITY=Object.freeze({contract_version:1,voices:8,partials:32,envelope_groups:4,
  max_patch_bytes:4096,patch_path:ADDITIVE_PATCH_PATH,sample_rate:Object.freeze({numerator:91015625,denominator:948})});
export const ADDITIVE_PRESETS=Object.freeze([{id:'organ',name:'管风琴'},{id:'soft-pad',name:'柔和铺底'},{id:'bell',name:'非谐波钟声'}]);
function clone(value){return value===null?null:JSON.parse(JSON.stringify(value));}
function object(value){return !!value&&typeof value==='object'&&!Array.isArray(value);}
function bounded(value,max){if(!Number.isInteger(value)||value<0||value>max)throw new RangeError('invalid additive value');return value;}
function positive(value){return uint32(value)&&value>0;}
export function additiveSampleRate(rate){
  if(!object(rate)||!positive(rate.numerator)||!positive(rate.denominator))throw new RangeError('invalid additive sample rate');
  return rate.numerator/rate.denominator;
}
export function envelopeIncrement(milliseconds,rate){
  const hz=additiveSampleRate(rate),max=65535*1000/hz;
  if(!Number.isFinite(milliseconds)||milliseconds<0||milliseconds>max)throw new RangeError('invalid additive envelope duration');
  return milliseconds===0?0:Math.min(65535,Math.max(1,Math.ceil(65535/(milliseconds*hz/1000))));
}
export function envelopeMilliseconds(increment,rate){bounded(increment,65535);const hz=additiveSampleRate(rate);return increment===0?0:65535*1000/(increment*hz);}
export function validateAdditivePatch(value){
  if(!object(value)||typeof value.bypass_lowpass!=='boolean'||!Array.isArray(value.envelopes)||value.envelopes.length!==4||!Array.isArray(value.partials)||value.partials.length!==32)throw new RangeError('invalid additive patch');
  return {bypass_lowpass:value.bypass_lowpass,centroid_q8:bounded(value.centroid_q8,8191),slope:bounded(value.slope,65535),
    comb_period:bounded(value.comb_period,31),comb_depth:bounded(value.comb_depth,65535),master_gain:bounded(value.master_gain,65535),
    master_pan:bounded(value.master_pan,65535),bend_range:bounded(value.bend_range,127),
    envelopes:Array.from(value.envelopes,item=>{if(!object(item))throw new RangeError('invalid additive envelope');return {
      attack_inc:bounded(item.attack_inc,65535),decay_inc:bounded(item.decay_inc,65535),sustain:bounded(item.sustain,65535),release_inc:bounded(item.release_inc,65535)};}),
    partials:Array.from(value.partials,item=>{if(!object(item))throw new RangeError('invalid additive partial');return {
      ratio_q16:bounded(item.ratio_q16,16777215),weight:bounded(item.weight,65535),group:bounded(item.group,3)};})};
}
export function getAdditivePreset(id){return ADDITIVE_PRESET_DATA[id]?validateAdditivePatch(ADDITIVE_PRESET_DATA[id]):null;}
export function encodeAdditivePatch(token,sequence,contextEpoch,patch){
  if(![token,sequence,contextEpoch].every(positive))throw new RangeError('invalid additive binding');
  const body=JSON.stringify({contract_version:1,token,sequence,context_epoch:contextEpoch,patch:validateAdditivePatch(patch)});
  if(body.length>4096)throw new RangeError('additive patch too large');return body;
}
export function getAdditiveRuntime(capabilities,state,sessionIdentity=''){
  const unavailable=reason=>({kind:'additive',available:false,selectable:false,playable:false,editable:false,reason,contextId:null,identity:null,revision:0,patch:null,applied:null,busy:false,sampleRate:null});
  const cap=capabilities?.additive,observed=state?.additive;
  if(!capabilities?.engines?.some(e=>e.id===ADDITIVE_ENGINE_ID&&e.available)||!object(cap)||!object(observed))return unavailable('additive_unavailable');
  if(cap.contract_version!==1||observed.contract_version!==1||cap.voices!==8||cap.partials!==32||cap.envelope_groups!==4||cap.patch_path!==ADDITIVE_PATCH_PATH||cap.max_patch_bytes!==4096)return unavailable('additive_contract_mismatch');
  try{additiveSampleRate(cap.sample_rate);}catch{return unavailable('additive_contract_mismatch');}
  if(!positive(capabilities.context_epoch)||capabilities.context_epoch!==state.context_epoch)return unavailable('stale_context');
  if(!['available','valid','ready','busy'].every(key=>typeof observed[key]==='boolean')||!positive(observed.instance_id)||!uint32(observed.revision))return unavailable('invalid_additive_state');
  if(!observed.available||!observed.valid)return unavailable('additive_unavailable');
  let patch=null;try{if(observed.patch!==null)patch=validateAdditivePatch(observed.patch);}catch{return unavailable('invalid_additive_patch');}
  const applied=observed.applied;
  if(applied!==null&&(!object(applied)||!['token','sequence','context_epoch','commit_id'].every(key=>positive(applied[key]))||applied.commit_id>2147483647))return unavailable('invalid_additive_state');
  const mode=state.mode,settled=mode?.requested===ADDITIVE_ENGINE_ID&&mode.applied===ADDITIVE_ENGINE_ID&&!mode.switching&&!mode.recovering;
  const playable=settled&&observed.ready&&!observed.busy;
  const identity={contextEpoch:state.context_epoch,instanceId:observed.instance_id};
  return {kind:'additive',available:true,selectable:!observed.busy,playable,editable:playable,reason:observed.busy?'additive_busy':!observed.ready?'additive_not_ready':!settled?'mode_not_settled':null,
    identity,contextId:[identity.contextEpoch,identity.instanceId,sessionIdentity].join(':'),revision:observed.revision,patch,applied:clone(applied),busy:observed.busy,sampleRate:clone(cap.sample_rate)};
}
