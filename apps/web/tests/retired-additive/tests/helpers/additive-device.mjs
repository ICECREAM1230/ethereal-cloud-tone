import {RuntimeDevice} from './runtime-device.mjs';
import {readFileSync} from 'node:fs';
const presets=JSON.parse(readFileSync(new URL('../../contracts/additive/20261019/presets.json',import.meta.url)));
export class AdditiveTestDevice extends RuntimeDevice {
  constructor(){super();this.caps.engines.push({id:3,available:true});this.caps.additive={contract_version:1,voices:8,partials:32,envelope_groups:4,max_patch_bytes:4096,patch_path:'/api/v2/additive/patch',sample_rate:{numerator:91015625,denominator:948}};
    this.state.additive={contract_version:1,available:true,valid:true,ready:true,busy:false,instance_id:1,revision:1,patch:structuredClone(presets.organ),applied:null};this.patchRequests=[];this.commitId=1;this.patchDelay=100;}
  parse(body){
    if(/^\d+(?: \d+){6}$/.test(body)){const [token,sequence,context,op,a,b,c]=body.split(' ').map(Number);if(op===5&&a===3&&b===0&&c===0&&[token,sequence,context].every(v=>Number.isInteger(v)&&v>0&&v<=4294967295))return {token,sequence,context,op,a,b,c};}
    return super.parse(body);
  }
  adopt(command,session){super.adopt(command,session);if(this.state.mode.applied===3)this.state.legacy.ready=false;if(command.op===4)this.state.additive.busy=false;}
  async request(path,options={}){
    if(path!=='/api/v2/additive/patch')return super.request(path,options);
    this.flush();const body=JSON.parse(options.body);this.patchRequests.push(structuredClone(body));
    if(this.patchError)throw this.error(this.patchError,'additive_rejected');
    const session=this.sessions.get(body.token);if(!session)throw this.error(410,'unknown_session');
    if(body.context_epoch!==this.state.context_epoch||body.sequence<=session.sequence)throw this.error(409,'stale_context');
    if(options.headers?.['X-FPGA-Control']!=='1'||options.headers?.['Content-Type']!=='application/json'||body.contract_version!==1)throw this.error(400,'invalid_patch');
    session.sequence=body.sequence;session.lease=this.time;this.state.clients[session.client].received=body.sequence;
    this.log.push({stage:'patch-received',...structuredClone(body)});
    this.events.push({at:this.time+this.ownerDelay,apply:()=>{
      if(this.state.context_epoch!==body.context_epoch)return;
      const slot=this.state.clients[session.client];Object.assign(slot,{token:body.token,parameter_processed:body.sequence,result_sequence:body.sequence,result_code:0,result_context_epoch:body.context_epoch});
      this.state.additive.busy=true;this.log.push({stage:'patch-owner',sequence:body.sequence});
      if(!this.noCommit)this.events.push({at:this.time+this.patchDelay,apply:()=>{
        if(this.state.context_epoch!==body.context_epoch)return;
        Object.assign(this.state.additive,{busy:false,revision:this.state.additive.revision+1,patch:structuredClone(body.patch),applied:{token:body.token,sequence:body.sequence,context_epoch:body.context_epoch,commit_id:++this.commitId}});
        if(this.wrongCommit)this.state.additive.applied.token++;
        this.moveContext();this.log.push({stage:'patch-committed',sequence:body.sequence});
      }});
    }});
    if(this.holdPatch){this.holdPatch=false;await new Promise(resolve=>this.patchGate=resolve);}
    return {api_major:2,accepted:body.sequence,context_epoch:body.context_epoch,stage:'received',fpga:'unverified'};
  }
}
