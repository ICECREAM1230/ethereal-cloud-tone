import {readFileSync} from 'node:fs';
import {V2Device} from './v2-device.mjs';
const examples=JSON.parse(readFileSync(new URL('../contracts/additive/20261019/presets.json',import.meta.url)));
const copy=value=>structuredClone(value);
const integer=(value,max=4294967295)=>Number.isInteger(value)&&value>=0&&value<=max;
// Independent HTTP-side validation. This model does not import the client
// encoder or pretend its scheduled COMMIT is a physical FPGA observation.
function validPatch(p){return p&&typeof p.bypass_lowpass==='boolean'&&integer(p.centroid_q8,8191)&&integer(p.slope,65535)&&integer(p.comb_period,31)&&integer(p.comb_depth,65535)&&integer(p.master_gain,65535)&&integer(p.master_pan,65535)&&integer(p.bend_range,127)&&
  Array.isArray(p.partials)&&p.partials.length===32&&Array.from(p.partials).every(v=>v&&integer(v.ratio_q16,16777215)&&integer(v.weight,65535)&&integer(v.group,3))&&
  Array.isArray(p.envelopes)&&p.envelopes.length===4&&Array.from(p.envelopes).every(v=>v&&['attack_inc','decay_inc','sustain','release_inc'].every(key=>integer(v[key],65535)));}
export class AdditiveDevice extends V2Device {
  constructor(options={}){super(options);this.caps.engines.push({id:3,available:true});
    this.caps.additive={contract_version:1,voices:8,partials:32,envelope_groups:4,max_patch_bytes:4096,patch_path:'/api/v2/additive/patch',sample_rate:{numerator:91015625,denominator:948}};
    this.state.additive={contract_version:1,available:true,valid:true,ready:true,busy:false,instance_id:1,revision:1,patch:copy(examples.organ),applied:null};
    this.state.mode={requested:0,applied:0,switching:false,recovering:false};this.state.legacy.ready=true;
    this.state.fm.flags=5;Object.assign(this.state.legacy.engine,{requested:0,applied:0,switching:false,recovering:false});this.commitId=0;
  }
  parse(body){const regular=super.parse(body);if(regular)return regular;
    if(typeof body!=='string'||body.length>95||!/^\d+(?: \d+){6}$/.test(body))return null;
    const [token,sequence,context,op,a,b,c]=body.split(' ').map(Number);
    return [token,sequence,context].every(v=>integer(v)&&v>0)&&op===5&&a===3&&b===0&&c===0?{token,sequence,context,op,a,b,c}:null;
  }
  performanceReady(){return this.state.mode.applied===3?this.state.additive.ready&&!this.state.additive.busy:super.performanceReady();}
  gate(command,slot){
    if(command.op===5&&command.a===3&&(!this.state.additive.valid||this.state.additive.busy))return 'additive_not_ready';
    if(this.state.mode.applied===3&&command.op!==4&&command.op!==5&&!(command.op===3&&(command.a&~slot.keys)===0)&&!this.performanceReady())return 'additive_not_ready';
    return super.gate(command,slot);
  }
  adopt(command,session){
    if(command.op===4||command.op===5)this.state.additive.busy=false;
    if(command.op!==5||command.a!==3){super.adopt(command,session);return;}
    const slot=this.state.clients[command.client];if(this.state.context_epoch!==command.context||this.sessions[command.client]!==session)return;
    Object.assign(slot,{token:command.token,processed:command.sequence,parameter_processed:command.sequence,result_sequence:command.sequence,result_code:0,result_context_epoch:command.context});
    this.log.push({...command,stage:'owner',at:this.now()});this.moveContext();const context=this.state.context_epoch;
    this.state.mode={requested:3,applied:this.state.mode.applied,switching:true,recovering:false};this.state.legacy.ready=false;this.state.fm.flags=5;
    this.schedule(this.nativeDelay,()=>{if(this.state.context_epoch!==context)return;this.state.mode={requested:3,applied:3,switching:false,recovering:false};});
  }
  additivePatch(text,header){
    this.advance();if(header!=='1')return this.error(403,'header_required');
    let body;try{if(Buffer.byteLength(text)>4096)throw Error();body=JSON.parse(text);}catch{return this.error(400,'invalid_additive_patch');}
    if(!body||body.contract_version!==1||!['token','sequence','context_epoch'].every(key=>integer(body[key])&&body[key]>0)||!validPatch(body.patch))return this.error(400,'invalid_additive_patch');
    const client=this.sessions.findIndex(s=>s?.token===body.token);if(client<0)return this.error(410,'unknown_session');const session=this.sessions[client];
    if(this.now()-session.seen>=10000)return this.error(410,'session_expired');
    if(body.context_epoch!==this.state.context_epoch||body.context_epoch!==session.context)return this.error(409,'stale_context');
    if(body.sequence<=session.sequence)return this.error(409,'stale_sequence');
    if(this.patchStatus)return this.error(this.patchStatus,'additive_rejected');
    if(this.state.mode.requested!==3||this.state.mode.applied!==3||this.state.mode.switching||this.state.mode.recovering||!this.performanceReady())return this.error(409,'additive_not_ready');
    session.sequence=body.sequence;session.seen=this.now();this.state.clients[client].received=body.sequence;
    const captured=copy(body),context=body.context_epoch;this.log.push({stage:'patch-received',...captured,at:this.now()});
    this.schedule(this.ownerDelay,()=>{
      if(this.state.context_epoch!==context||this.sessions[client]!==session)return;
      this.state.clients.forEach(c=>c.keys=0);this.state.additive.busy=true;
      Object.assign(this.state.clients[client],{parameter_processed:body.sequence,result_sequence:body.sequence,result_code:0,result_context_epoch:context});this.log.push({stage:'patch-owner',sequence:body.sequence,at:this.now()});
      if(!this.noCommit)this.schedule(this.nativeDelay,()=>{
        if(this.state.context_epoch!==context||this.sessions[client]!==session)return;
        Object.assign(this.state.additive,{patch:captured.patch,busy:false,revision:(this.state.additive.revision+1)>>>0,applied:{token:body.token,sequence:body.sequence,context_epoch:context,commit_id:this.commitId=(this.commitId+1)&2147483647||1}});
        this.moveContext();this.log.push({stage:'patch-committed',sequence:body.sequence,at:this.now()});
      });
    });
    return this.response(202,{api_major:2,accepted:body.sequence,context_epoch:context,stage:'received',fpga:'unverified'});
  }
}
