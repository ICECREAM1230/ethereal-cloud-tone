import {UiDevice} from './ui-device.mjs';

// Loopback transport fixture only. Sending never changes observed sensor or
// native state; browser checks must keep those facts separate.
export class FdcUiDevice extends UiDevice{
  constructor({available=true,mode=0,modeMetadata=true,...options}={}){
    super({mode,...options});this.fdcAvailable=available;this.fdcPosts=[];this.fdcId=0;this.fdcResult=null;this.fdcRequest=null;
    this.fdcFlags=mode===0?3:1;this.fdcRevision=1;this.fdcDelay=250;this.holdFdc=false;
    this.caps.cpu_mode={version:1,route_only:true,modes:[0,1,2]};
    this.state.cpu_mode={version:1,route_only:true,valid:true,sequence:1,desired:mode,applied:mode,ready:true,busy:false,fault:false,pending:false};
    if(!modeMetadata){delete this.caps.cpu_mode;delete this.state.cpu_mode;}
    this.state.legacy.inputs={version:1,available:true,initialized:true,enabled:true,ready:true,capabilities_known:true,
      independent_usb:true,usb_configured:false,usb_error:false,stop_pending:false,fault:0,stop_requested:0,stop_completed:0,stop_fault:0};
  }
  fdcSnapshot(){
    const word=this.fdcAvailable?(0xfdc10000|(this.fdcRevision<<8)|this.fdcFlags)>>>0:0;
    return {api_major:2,contract_version:1,available:this.fdcAvailable,valid:true,fresh:true,status_word:word,revision:this.fdcAvailable?this.fdcRevision:0,
      ...Object.fromEntries(['available','eligible','requested_enable','driver_enabled','busy','driver_fault','local_error','user_config_active'].map((key,bit)=>[key,this.fdcAvailable&&!!(this.fdcFlags&(1<<bit))])),
      pending:!!this.fdcRequest,request:this.fdcRequest,result:this.fdcResult,confirmation:'transport_only'};
  }
  async request(path,options={}){
    if(path!=='/api/v2/fdc')return super.request(path,options);
    if(!this.fdcAvailable)throw this.exception(404,'not_found');
    this.advance();if(options.method!=='POST')return structuredClone(this.fdcSnapshot());
    if(options.headers?.['X-FPGA-Control']!=='1')throw this.exception(403,'header_required');
    if(!/^\d+(?: \d+){9}$/.test(options.body))throw this.exception(400,'invalid_fdc_body');
    const [token,sequence,context,op,...words]=options.body.split(' ').map(Number);
    if(words.length!==6||op>3||words.some(v=>!Number.isInteger(v)||v<0||v>0xffffffff))throw this.exception(400,'invalid_fdc_body');
    const client=this.sessions.findIndex(s=>s?.token===token),session=this.sessions[client];
    if(!session)throw this.exception(410,'unknown_session');
    if(context!==this.state.context_epoch||session.context!==context)throw this.exception(409,'stale_context');
    if(sequence<=session.sequence)throw this.exception(409,'stale_sequence');
    session.sequence=sequence;session.seen=this.now();
    const identity={token,sequence,context_epoch:context,request_id:++this.fdcId,op};
    this.fdcPosts.push({identity,words,body:options.body});this.fdcRequest={...identity,stage:'queued'};
    if(this.holdFdc)await new Promise(resolve=>this.releaseFdc=resolve);
    this.schedule(this.fdcDelay,()=>{this.fdcRequest=null;this.fdcResult={...identity,code:0,stage:'transport_sent'};});
    return {api_major:2,accepted:sequence,context_epoch:context,request_id:identity.request_id,stage:'queued',confirmation:'transport_only'};
  }
}
