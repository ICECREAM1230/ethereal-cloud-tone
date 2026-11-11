import {RuntimeDevice} from './runtime-device.mjs';

// Independent model of the firmware's five-decimal HTTP body and cached RVR1
// reply. Native completion is deliberately separate from HTTP acceptance.
export class ReverbDevice extends RuntimeDevice {
  constructor(){
    super();this.reverbDelay=400;this.reverbPosts=[];this.reverbReads=0;this.reverbId=0;
    this.effect={api_major:2,contract_version:1,available:true,valid:true,pending:false,fault:0,
      wet_q15:8192,damp_q15:8192,applied_sequence:0,generation:1,request:null,result:null,applied:null,confirmation:'synth_parameters'};
  }
  async request(path,options={}){
    if(path!=='/api/v2/effects/reverb')return super.request(path,options);
    this.flush();
    if(this.reverbMissing)throw this.error(404,'not_found');
    if(options.method!=='POST'){
      this.reverbReads++;const value=structuredClone(this.effect);
      if(this.holdReverbRead){this.holdReverbRead=false;await new Promise(resolve=>this.reverbReadGate=resolve);}
      return value;
    }
    if(options.headers?.['X-FPGA-Control']!=='1')throw this.error(403,'header_required');
    if(!/^[0-9]+(?: [0-9]+){4}$/.test(options.body))throw this.error(400,'invalid_reverb');
    const [token,sequence,context,wet,damp]=options.body.split(' ').map(Number);
    if(![token,sequence,context].every(n=>Number.isInteger(n)&&n>0&&n<=0xffffffff)||wet>32768||damp>28672)throw this.error(400,'invalid_reverb');
    const session=this.sessions.get(token);
    if(!session)throw this.error(410,'unknown_session');
    if(context!==this.state.context_epoch||context!==session.context)throw this.error(409,'stale_context');
    if(sequence<=session.sequence)throw this.error(409,'stale_sequence');
    if(this.reverbError)throw this.error(this.reverbError,this.reverbError===503?'reverb_unavailable':'reverb_busy');
    if(!this.effect.available||!this.effect.valid)throw this.error(503,'reverb_unavailable');
    if(this.effect.pending)throw this.error(409,'reverb_busy');
    session.sequence=sequence;session.lease=this.time;
    const identity={token,sequence,context_epoch:context,request_id:++this.reverbId};
    this.reverbPosts.push({body:options.body,headers:structuredClone(options.headers),at:this.time,identity});
    this.effect.pending=true;this.effect.request=identity;
    this.events.push({at:this.time+this.reverbDelay,apply:()=>{
      if(this.noReverbAck)return;
      this.effect.pending=false;this.effect.request=null;this.effect.generation++;
      this.effect.result={...identity,code:0};
      this.effect.applied={...identity,applied_sequence:++this.effect.applied_sequence};
      this.effect.wet_q15=wet;this.effect.damp_q15=damp;
    }});
    if(this.holdReverbPost){this.holdReverbPost=false;await new Promise(resolve=>this.reverbPostGate=resolve);}
    return {api_major:2,accepted:sequence,context_epoch:context,stage:'received',fpga:'unverified'};
  }
}
