// FDC1 status is an observation. Revision and transport results are never a
// CPU application receipt or a native voice acknowledgement.
export const FDC_PATH='/api/v2/fdc';
export const FDC_OPERATIONS=Object.freeze({disable:0,enable:1,restart:2,calibrate:3});
const uint=(v,max=0xffffffff)=>Number.isInteger(v)&&v>=0&&v<=max;
const positive=v=>uint(v)&&v>0;
const flags=['available','eligible','requested_enable','driver_enabled','busy','driver_fault','local_error','user_config_active'];
const terminal=new Set(['transport_sent','canceled','unknown','failed']);
const supportedTag=word=>[0xfdc1,0xfdc2].includes(word>>>16);
export function validateFdcState(value){
  if(!value||value.api_major!==2||value.contract_version!==1||value.confirmation!=='transport_only'||
    !['valid','fresh','pending',...flags].every(key=>typeof value[key]==='boolean')||
    !uint(value.status_word)||!uint(value.revision,255))throw Error('invalid_fdc_state');
  if(value.valid&&value.fresh){
    const tag=value.status_word>>>16;const tagged=tag===0xfdc1||tag===0xfdc2;
    if(value.audio_engine!==undefined&&value.audio_engine!==(tag===0xfdc2?'fm':tag===0xfdc1?'subtractive':'unknown'))throw Error('invalid_fdc_engine');
    if(value.available!==(tagged&&!!(value.status_word&1))||value.revision!==((value.status_word>>>8)&255))throw Error('invalid_fdc_capability');
    if(tagged&&!flags.every((key,bit)=>value[key]===!!(value.status_word&(1<<bit))))throw Error('invalid_fdc_flags');
  }
  for(const key of ['request','result']){
    const item=value[key];
    if(item!==null&&(!item||!['token','sequence','context_epoch','request_id'].every(k=>positive(item[k]))||!uint(item.op,3)||
      (key==='request'?item.stage!=='queued':!terminal.has(item.stage)||!uint(item.code)||item.code===0&&item.stage!=='transport_sent')))
      throw Error('invalid_fdc_identity');
  }
  return structuredClone(value);
}
export function calibrationWords(values){
  const names=['pitch_far','pitch_near','volume_far','volume_near','pitch_sigma','volume_sigma'];
  if(!values||!names.every(key=>typeof values[key]==='number'))throw Error('请填写完整的六个校准值。');
  const words=names.slice(0,4).map(key=>values[key]);
  if(!words.every(v=>uint(v,0x03ffffff)&&v>0))throw Error('远点与近点原码须为 1–67108863 的整数。');
  const buffer=new ArrayBuffer(4),view=new DataView(buffer);
  for(const key of names.slice(4)){
    const sigma=values[key];view.setFloat32(0,sigma,false);const bits=view.getUint32(0,false);
    if(!Number.isFinite(sigma)||sigma<0||!Number.isFinite(view.getFloat32(0,false))||(bits>>>31))throw Error('噪声 sigma 须为非负且可表示的有限数值。');
    words.push(bits);
  }
  for(let lane=0;lane<2;lane++){
    view.setUint32(0,words[4+lane],false);const width=Math.fround(Math.abs(words[lane*2+1]-words[lane*2]));
    if(width<64||width<Math.fround(20*view.getFloat32(0,false)))throw Error('每组近远跨度须至少 64 原码，且不少于 20 倍噪声 sigma。');
  }
  return words;
}
export function encodeFdcWrite(identity,op,words=[0,0,0,0,0,0]){
  if(![identity.token,identity.sequence,identity.context].every(positive)||!uint(op,3)||!Array.isArray(words)||words.length!==6||!words.every(v=>uint(v))||op!==3&&words.some(Boolean))throw Error('invalid_fdc_request');
  return [identity.token,identity.sequence,identity.context,op,...words].join(' ');
}
function matches(result,command){return !!result&&['token','sequence','request_id','op'].every(key=>result[key]===command[key])&&result.context_epoch===command.context;}
export class FdcController{
  constructor(owner){this.owner=owner;this.state=null;this.draft=null;this.command=null;this.polling=false;this.nextPoll=0;this.serial=0;this.uncertain=false;this.phase='loading';this.message='连接设备后读取 FDC 状态。';}
  get connected(){const o=this.owner;return o.apiMajor===2&&o.status==='online'&&!o.suspended&&!o.rearming&&!o.modeWaiting&&!o.transition&&positive(o.token)&&positive(o.runtimeState?.context_epoch);}
  get available(){return this.connected&&this.state?.available&&this.state.valid&&this.state.fresh;}
  can(op){if(!this.available)return false;if(op===0)return true;if(op!==2&&!supportedTag(this.state.status_word))return false;return !this.draft&&!this.command&&!this.uncertain&&!this.state.busy&&(op===2||!this.state.driver_fault&&this.state.eligible);}
  cancel(){this.serial++;if(this.draft||this.command){this.phase='canceled';this.message='会话、停止或引擎状态已变化；旧 FDC 请求不再确认，请刷新当前状态。';}this.draft=this.command=this.state=null;this.uncertain=false;this.nextPoll=0;}
  queue(op,words=[0,0,0,0,0,0]){
    if(!this.can(op))return false;
    encodeFdcWrite({token:this.owner.token,sequence:1,context:this.owner.runtimeState.context_epoch},op,words);
    if(op===0){this.serial++;this.command=null;this.draft=null;this.uncertain=false;}
    this.draft={op,words:[...words],queuedAt:this.owner.now(),version:this.owner.inputVersion,epoch:this.owner.epoch};this.phase='queued';this.message='等待发送 FDC 请求；尚未确认设备采用。';this.owner.notify();this.owner.kick();return true;
  }
  due(){const d=this.draft;return !!d&&!this.command&&this.available&&this.owner.now()-d.queuedAt<1200&&d.version===this.owner.inputVersion&&d.epoch===this.owner.epoch&&(d.op===0||((d.op===2||supportedTag(this.state.status_word))&&!this.state.busy&&!this.uncertain&&(d.op===2||!this.state.driver_fault&&this.state.eligible)));}
  expire(){if(this.draft&&this.owner.now()-this.draft.queuedAt>=1200){this.draft=null;this.phase='canceled';this.message='FDC 请求等待已过期；请核对状态后重新操作。';this.owner.notify();}if(this.command&&this.owner.now()-this.command.started>=5000){this.command=null;this.uncertain=true;this.phase='unknown';this.message='FDC 传输结果未知；请刷新核对，不会自动重发。';this.owner.notify();}}
  async send(epoch){
    if(!this.due())return;
    const o=this.owner,draft=this.draft,command={...draft,token:o.token,sequence:++o.sequence,context:o.runtimeState.context_epoch,started:o.now(),version:o.inputVersion,request_id:0};
    this.draft=null;this.command=command;const serial=++this.serial;o.notify();
    try{
      const receipt=await o.transport.request(FDC_PATH,{method:'POST',headers:{'Content-Type':'text/plain','X-FPGA-Control':'1'},body:encodeFdcWrite(command,command.op,command.words)},202);
      if(this.command!==command||epoch!==o.epoch||command.version!==o.inputVersion||serial!==this.serial)return;
      if(receipt?.api_major!==2||receipt.accepted!==command.sequence||receipt.context_epoch!==command.context||!positive(receipt.request_id)||receipt.stage!=='queued'||receipt.confirmation!=='transport_only')throw Error('invalid_fdc_receipt');
      command.request_id=receipt.request_id;this.phase='queued';this.message='请求已排队；等待传输结果，未确认 CPU 采用。';this.nextPoll=0;
    }catch(error){
      if(this.command!==command||epoch!==o.epoch||command.version!==o.inputVersion||serial!==this.serial)return;
      this.command=null;this.uncertain=true;this.phase='unknown';this.message='FDC 请求未获传输确认；请刷新核对，不会自动重发。';this.nextPoll=0;
      if(error.status===410||error.code==='stale_context')o.fail(error);
    }finally{o.notify();}
  }
  async poll({explicit=false}={}){
    if(!this.connected||this.polling)return;
    const o=this.owner,epoch=o.epoch,version=o.inputVersion,serial=this.serial;this.polling=true;
    try{
      const state=validateFdcState(await o.transport.request(FDC_PATH));
      if(epoch!==o.epoch||version!==o.inputVersion||serial!==this.serial)return;
      this.state=state;
      if(!state.available){this.phase='unavailable';this.message='设备未提供可用的 FDC 控制；原有演奏仍可使用。';}
      else if(this.command&&matches(state.result,this.command)){
        this.command=null;this.phase=state.result.stage;this.uncertain=state.result.stage==='unknown';
        this.message=state.result.code===0?'传输已发送；不代表 CPU 已采用、声部已生效或已发声。':state.result.stage==='canceled'?'FDC 请求已取消；实际状态以下方观察值为准。':'FDC 传输未获确认；请核对当前观察状态。';
      }else if(!this.command&&!this.draft&&!['transport_sent','canceled','unknown','failed'].includes(this.phase)){
        this.phase=state.valid&&state.fresh?'observed':'unknown';this.message=state.valid&&state.fresh?'已读取设备观察状态；它不是某条命令的完成回执。':'设备状态尚未获得新鲜有效读回。';
      }
      if(explicit&&!this.command&&!this.draft&&state.valid&&state.fresh&&!state.busy)this.uncertain=false;
      this.expire();
    }catch(error){if(epoch!==o.epoch||version!==o.inputVersion||serial!==this.serial)return;this.state=null;this.phase=[404,405,501].includes(error.status)?'unavailable':'unknown';this.message=this.phase==='unavailable'?'此固件尚未提供 FDC 控制；原有演奏仍可使用。':'FDC 状态读取失败；不会自动发送或重发控制。';}
    finally{this.polling=false;this.nextPoll=serial!==this.serial?0:o.now()+(this.command?150:this.phase==='unavailable'?10000:1000);o.notify();}
  }
}
