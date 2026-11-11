import {currentCatalogFixture as fixture} from './fixture-catalog.mjs';
import {applyCC} from './device.mjs';
import {CC_ALLOWED} from '../src/protocol.mjs';
import {FM_HTTP_API_MAJOR,FM_HTTP_API_MINOR,FM_HTTP_COMMAND_MAX_BYTES,FM_PERFORMANCE_LEASE_MS,
  FM_HTTP_OPCODE_CC as CC,FM_HTTP_OPCODE_KEYS_UPDATE as KEYS,FM_HTTP_OPCODE_STOP as STOP,
  FM_HTTP_OPCODE_ENGINE_SELECT as SELECT,FM_HTTP_OPCODE_KS_DECAY as DECAY,FM_HTTP_OPCODE_FM_PARAMETER as PARAM,FM_HTTP_OPCODE_FM_PRESET as PRESET,
  FM_ENGINE_DDS as DDS,FM_ENGINE_KS as KS,FM_ENGINE_FM as FM,
  FM_SCHEMA_SHA256,FM_PROFILES_SHA256,FM_CONTRACT_SHA256,FM_PROFILE_IDS,
  FM_FLAG_READY as READY,FM_FLAG_SELECTED as SELECTED,FM_FLAG_MUTED as MUTED,FM_FLAG_BUSY as BUSY,FM_FLAG_FAULT as FAULT,
  FM_PHASE_READY,FM_PHASE_RECOVERING,FM_FEATURE_NOTE_ON,FM_FEATURE_NOTE_OFF,FM_FEATURE_SET_PARAMETER,FM_FEATURE_APPLY_PRESET,FM_FEATURE_NEXT_NOTE_PARAMETERS,
  FM_RESULT_OK as OK,FM_RESULT_STALE_EPOCH as STALE,FM_RESULT_BACKEND_FAULT,
  FM_TARGET_NONE,packTarget,getProfile,getParameter,getPreset,validateParameter,validateTarget,unpackTarget} from '../src/generated/fm-contract.mjs';
const clone=value=>structuredClone(value);
const next=value=>(value+1)>>>0||1;
const has=(value,mask)=>(value&mask)===mask;
// A loopback simulation anchored to real C-handler JSON structure. This model
// parses its own HTTP input; no client encoder, runtime gate or controller is used.
export class V2Device {
  constructor(options={}) {
    this.now=options.now||(()=>performance.now());this.ownerDelay=options.ownerDelay??25;this.nativeDelay=options.nativeDelay??100;
    this.receiptDelay=0;this.offline=false;this.receivedOnly=options.receivedOnly??false;
    this.caps=fixture(options.noFM?'v2-capabilities':'v2-live-capabilities');this.state=fixture(options.noFM?'v2-state':'v2-live-state');
    this.state.simulation=true;this.state.legacy.simulation=true;
    this.sessions=Array.from({length:4},()=>null);this.nextToken=100000;this.nextNonce=42;this.nativeSequence=7;this.panicSequence=300;
    this.events=[];this.log=[];this.externalNotes=[];
  }
  response(status,body){return {status,body};}
  error(status,code){return this.response(status,{api_major:FM_HTTP_API_MAJOR,code});}
  schedule(delay,action){this.events.push({at:this.now()+delay,action});}
  advance(){
    const time=this.now();const due=this.events.filter(e=>e.at<=time);this.events=this.events.filter(e=>e.at>time);
    for(const e of due)e.action();
    this.sessions.forEach((session,index)=>{if(session&&time>=session.expires&&this.state.clients[index].token===session.token&&this.state.clients[index].keys){if(this.state.clients[index].keys)this.log.push({stage:'lease-expired',token:session.token,at:time});this.state.clients[index].keys=0;session.armed=false;}});
    this.state.clients.forEach((slot,index)=>{this.state.legacy.keys[index]=slot.keys;});
  }
  snapshot(){this.advance();return clone(this.state);}
  capabilities(){this.advance();return clone(this.caps);}
  moveContext(){this.state.context_epoch=next(this.state.context_epoch);this.caps.context_epoch=this.state.context_epoch;this.state.clients.forEach(c=>c.keys=0);this.externalNotes=[];this.sessions.forEach(s=>{if(s)s.armed=false;});}
  session(body='',header=null){
    this.advance();if(header!=='1')return this.error(403,'header_required');if(body!=='')return this.error(400,'empty_body_required');
    const context=this.state.context_epoch;const client=this.sessions.findIndex(s=>!s||s.context!==context||this.now()-s.seen>=10000);
    if(client<0)return this.error(503,'session_full');const token=++this.nextToken;
    this.sessions[client]={token,context,sequence:0,seen:this.now(),expires:0,armed:false};
    this.log.push({stage:'session',token,client,context,at:this.now()});
    return this.response(201,{api_major:FM_HTTP_API_MAJOR,api_minor:FM_HTTP_API_MINOR,token,client,lease_ms:FM_PERFORMANCE_LEASE_MS,context_epoch:context});
  }
  rawMidi(body,header=null){
    this.advance();if(header!=='1')return this.error(403,'header_required');
    let value;try{value=JSON.parse(body);}catch{return this.error(400,'invalid_midi');}
    if(!value||value.source!=='web'||!Array.isArray(value.bytes)||value.bytes.length<1||value.bytes.length>256||!Array.from(value.bytes).every(b=>Number.isInteger(b)&&b>=0&&b<=255))return this.error(400,'invalid_midi');
    if(![value.token,value.sequence,value.context_epoch].every(n=>Number.isInteger(n)&&n>0&&n<=0xffffffff))return this.error(400,'invalid_binding');
    const session=this.sessions.find(s=>s?.token===value.token);if(!session)return this.error(410,'unknown_session');
    if(this.now()-session.seen>=10000)return this.error(410,'session_expired');
    if(value.context_epoch!==this.state.context_epoch||session.context!==value.context_epoch)return this.error(409,'stale_context');
    if(value.sequence<=session.sequence)return this.error(409,'stale_sequence');
    if(this.rawStatus)return this.error(this.rawStatus,this.rawStatus===503?'midi_queue_full':'engine_not_ready');
    if(this.state.mode.switching||this.state.mode.recovering)return this.error(409,'engine_not_ready');
    if(this.state.mode.requested!==this.state.mode.applied||!this.performanceReady())return this.error(409,'engine_not_ready');
    session.sequence=value.sequence;session.seen=this.now();
    this.log.push({stage:'midi-queued',...clone(value),at:this.now()});
    return this.response(202,{accepted:true,queued_bytes:value.bytes.length});
  }
  parse(body){
    if(typeof body!=='string'||Buffer.byteLength(body)>FM_HTTP_COMMAND_MAX_BYTES||!/^[0-9]+(?: [0-9]+){6}$/.test(body))return null;
    const numbers=body.split(' ').map(Number);if(!numbers.every(n=>Number.isInteger(n)&&n>=0&&n<=0xffffffff)||!numbers.slice(0,3).every(Boolean))return null;
    const [token,sequence,context,op,a,b,c]=numbers;
    const valid=op===CC?CC_ALLOWED.includes(a)&&b<=127&&c===0:op===KEYS?a<=33554431&&b>=1&&b<=127&&c===0:op===STOP?a===0&&b===0&&c===0:op===SELECT?[DDS,KS,FM].includes(a)&&c===0&&(a===FM?b>0:b===0):op===DECAY?a>=200&&a<=8000&&b===0&&c===0:op===PARAM?a>0:op===PRESET?b===0&&c===0:false;
    return valid?{token,sequence,context,op,a,b,c}:null;
  }
  profile(){const f=this.state.fm;return FM_PROFILE_IDS.map(getProfile).find(p=>p.backend_id===f.backend_id&&p.profile_version===f.profile_version&&p.id===f.profile_id&&p.role!=='offline_reference');}
  selectable(allowMusicalBusy=false){
    const f=this.state.fm,p=this.profile();return !!p&&this.caps.schema_sha256===FM_SCHEMA_SHA256&&this.caps.profiles_sha256===FM_PROFILES_SHA256&&this.caps.contract_sha256===FM_CONTRACT_SHA256&&f.profile_sha256===FM_PROFILES_SHA256&&f.available&&f.valid&&f.phase===FM_PHASE_READY&&has(f.flags,READY)&&!(f.flags&(FAULT|(allowMusicalBusy?0:BUSY)))&&f.voice_count===p.voice_count&&f.patch_operator_count===p.patch_operator_count&&JSON.stringify(f.operators_per_voice)===JSON.stringify(p.operators_per_voice)&&this.caps.engines.some(e=>e.id===FM&&e.available)&&this.caps.catalog_profiles.some(c=>c.id===p.id&&c.backend_id===p.backend_id&&c.runtime_available);
  }
  // Mirrors fm_engine.h: bound performance queues through ordinary mailbox
  // BUSY, while explicit SELECT continues to require idle.
  playable(){const f=this.state.fm,m=this.state.mode;return this.selectable(true)&&has(f.flags,SELECTED)&&!(f.flags&MUTED)&&f.session_id>0&&f.engine_epoch>0&&f.topology_generation>0&&m.requested===FM&&m.applied===FM&&!m.switching&&!m.recovering&&has(f.features,FM_FEATURE_NOTE_ON|FM_FEATURE_NOTE_OFF);}
  performanceReady(){return this.state.mode.applied===FM?this.playable():this.state.legacy.ready;}
  gate(command,slot){
    const {op,a,b,c}=command,m=this.state.mode,f=this.state.fm;
    if(op===STOP||op===KEYS&&(a&~slot.keys)===0)return null;
    if(op===SELECT){if(a===FM&&(!this.selectable()||b!==this.profile()?.backend_id))return 'fm_unavailable';if(!this.caps.engines.some(e=>e.id===a&&e.available))return a===KS?'ks_unavailable':'engine_not_ready';return null;}
    if(m.switching||m.recovering)return 'engine_not_ready';
    if(op===PARAM||op===PRESET){if(!this.playable())return 'engine_not_ready';if(!has(f.features,op===PARAM?FM_FEATURE_SET_PARAMETER|FM_FEATURE_NEXT_NOTE_PARAMETERS:FM_FEATURE_APPLY_PRESET))return 'fm_unavailable';}
    if(m.requested===FM&&!this.playable())return 'engine_not_ready';
    if(op===CC&&m.requested!==DDS&&a!==7&&a!==10)return 'engine_not_ready';
    if(op===DECAY&&m.requested!==KS)return 'ks_unavailable';
    return null;
  }
  command(body,header=null){
    this.advance();if(header!=='1')return this.error(403,'header_required');const cmd=this.parse(body);if(!cmd)return this.error(400,'invalid_command');
    const client=this.sessions.findIndex(s=>s?.token===cmd.token);if(client<0)return this.error(410,'unknown_session');const session=this.sessions[client];
    if(this.now()-session.seen>=10000)return this.error(410,'session_expired');if(cmd.context!==this.state.context_epoch||session.context!==cmd.context)return this.error(409,'stale_context');if(cmd.sequence<=session.sequence)return this.error(409,'stale_sequence');
    if(cmd.op===PARAM){const p=getParameter(this.state.fm.profile_id,cmd.a),f=this.state.fm,v=unpackTarget(cmd.b).voice;if(!p||!validateParameter(f.profile_id,cmd.a,p.scope_id,cmd.b,cmd.c)||!validateTarget(p.scope_id,cmd.b,f.voice_count,f.operators_per_voice[v]??0,f.patch_operator_count))return this.error(400,'invalid_command');}
    if(cmd.op===PRESET&&!getPreset(this.state.fm.profile_id,cmd.a))return this.error(400,'invalid_command');
    const reason=this.gate(cmd,this.state.clients[client]);if(reason)return this.error(503,reason);
    session.sequence=cmd.sequence;session.seen=this.now();cmd.client=client;cmd.expires=this.now()+FM_PERFORMANCE_LEASE_MS;
    this.state.clients[client].received=cmd.sequence;this.log.push({...cmd,body,stage:'received',at:this.now()});
    if(!this.receivedOnly)this.schedule(this.ownerDelay,()=>this.adopt(cmd,session));
    return this.response(202,{api_major:FM_HTTP_API_MAJOR,accepted:cmd.sequence,stage:'received',context_epoch:cmd.context,fpga:'unverified'});
  }
  native(command,context,apply,panic=false){
    const f=this.state.fm;if(!f.available||(panic&&!f.session_id)){this.schedule(this.nativeDelay,()=>{if(this.state.context_epoch===context)apply(0);});return;}const identity={session_id:f.session_id,engine_epoch:f.engine_epoch,sequence_id:panic?++this.panicSequence:++this.nativeSequence};
    const target=panic?f.panic:f;target.accepted=clone(identity);
    if(panic&&f.session_id){f.engine_epoch=next(f.engine_epoch);f.flags=READY|SELECTED|MUTED|BUSY;f.phase=FM_PHASE_RECOVERING;}
    if(command.op===SELECT&&command.a===FM){f.session_id=++this.nextNonce;f.engine_epoch=next(f.engine_epoch);f.flags=READY|SELECTED|MUTED;}
    const expectedSession=f.session_id,expectedEpoch=f.engine_epoch,expectedTopology=f.topology_generation;this.log.push({stage:panic?'panic-accepted':'native-accepted',...identity,op:command.op,at:this.now()});
    this.schedule(this.nativeDelay,()=>{
      if(this.state.context_epoch!==context||this.state.fm.session_id!==expectedSession||this.state.fm.engine_epoch!==expectedEpoch||this.state.fm.topology_generation!==expectedTopology){this.log.push({stage:'native-discarded',...identity,op:command.op});return;}
      target.completed=clone(identity);target.result={...identity,code:this.nativeCode||OK};this.log.push({stage:panic?'panic-completed':'native-completed',...identity,op:command.op,code:target.result.code,at:this.now()});
      // S3 SELECT completion invalidates the prior instance cache even on error.
      // Keep this after the identity fence: a late completion owns no new cache.
      if(command.op===SELECT)f.parameter_values=[];
      if(!target.result.code)apply(identity.sequence_id);
    });
  }
  cache(parameter_id,target_word,value_u32,sequence_id){const values=this.state.fm.parameter_values;const old=values.find(v=>v.parameter_id===parameter_id&&v.target_word===target_word);const item={parameter_id,target_word,value_u32,sequence_id};if(old)Object.assign(old,item);else values.push(item);}
  adopt(cmd,session){
    const slot=this.state.clients[cmd.client];const code=cmd.context!==this.state.context_epoch?STALE:this.gate(cmd,slot)?FM_RESULT_BACKEND_FAULT:this.ownerCode||OK;
    if(this.sessions[cmd.client]!==session){this.log.push({...cmd,stage:'owner-discarded'});return;}
    Object.assign(slot,{token:cmd.token,result_sequence:cmd.sequence,result_code:code,result_context_epoch:cmd.context});this.log.push({...cmd,stage:'owner',code,at:this.now()});if(code)return;
    slot.processed=cmd.sequence;const f=this.state.fm;
    if(cmd.op===KEYS){session.expires=cmd.expires;if(cmd.a===0)session.armed=true;slot.keys=session.armed&&this.now()<cmd.expires?cmd.a:0;}
    else if(cmd.op===STOP){slot.stop_processed=cmd.sequence;this.moveContext();this.state.legacy.test_tone=false;this.state.legacy.panic++;const bound=f.available&&f.session_id>0;if(!this.state.mode.recovering)this.state.mode.requested=this.state.mode.applied;this.state.mode.switching=!!bound||this.state.mode.recovering||this.state.mode.requested!==this.state.mode.applied;this.native(cmd,this.state.context_epoch,()=>{if(bound){this.state.mode.switching=this.state.mode.recovering||this.state.mode.requested!==this.state.mode.applied;f.flags=this.state.mode.recovering?READY|MUTED:READY|SELECTED;f.phase=FM_PHASE_READY;}},true);}
    else {
      slot.parameter_processed=cmd.sequence;
      if(cmd.op===CC)applyCC(this.state.legacy,cmd.a,cmd.b);
      if(cmd.op===DECAY)this.state.legacy.ks_decay_ms=cmd.a;
      if(cmd.op===PARAM)this.native(cmd,this.state.context_epoch,seq=>this.cache(cmd.a,cmd.b,cmd.c,seq));
      if(cmd.op===SELECT){
        this.moveContext();const context=this.state.context_epoch;this.state.mode={requested:cmd.a,applied:this.state.mode.applied,switching:true,recovering:false};this.state.legacy.ready=false;
        this.native(cmd,context,()=>{this.state.mode={requested:cmd.a,applied:cmd.a,switching:false,recovering:false};this.state.legacy.ready=cmd.a!==FM;if(cmd.a!==FM)Object.assign(this.state.legacy.engine,{requested:cmd.a,applied:cmd.a,switching:false,recovering:false});if(f.available)f.flags=cmd.a===FM?READY|SELECTED:READY|MUTED;});
      }
      if(cmd.op===PRESET){
        const preset=getPreset(f.profile_id,cmd.a);this.moveContext();f.flags|=MUTED;this.state.mode.switching=true;
        this.native(cmd,this.state.context_epoch,seq=>{f.parameter_values=[];for(const item of preset.values)this.cache(item.parameter_id,packTarget(FM_TARGET_NONE,item.operator_index),item.value_u32,seq);f.flags=READY|SELECTED;this.state.mode.switching=false;});
      }
    }
    this.state.legacy.revision++;
  }
  nativeReset(recovering=true){this.moveContext();Object.assign(this.state.fm,{session_id:0,flags:READY|MUTED,phase:FM_PHASE_READY,parameter_values:[]});this.state.mode={requested:FM,applied:recovering?DDS:FM,switching:recovering,recovering};this.state.legacy.ready=false;}
  configure(values){for(const key of ['ownerDelay','nativeDelay','receiptDelay'])if(Number.isFinite(values[key])&&values[key]>=0&&values[key]<=10000)this[key]=values[key];for(const key of ['offline','receivedOnly'])if(typeof values[key]==='boolean')this[key]=values[key];if(values.nativeReset)this.nativeReset();if(values.externalContext)this.moveContext();if(values.busy!==undefined)this.state.fm.flags=values.busy?this.state.fm.flags|BUSY:this.state.fm.flags&~BUSY;}
}
