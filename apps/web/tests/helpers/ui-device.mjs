import {V2Device} from '../../mock/v2-device.mjs';

// Loopback-only UI model. This does not synthesize sound or represent an attached
// device. Performance and owner/native fences use the existing independent v2
// model; file/effect jobs have their own slots, as in the established tests.
export class UiDevice extends V2Device {
  constructor({mode=1,...options}={}) {
    super(options);
    this.state.mode={requested:mode,applied:mode,switching:false,recovering:false};
    this.state.legacy.ready=mode!==2;Object.assign(this.state.legacy.engine,this.state.mode);
    this.ddsCalls=[];this.commandErrors=[];this.jobId=0;this.ddsDelay=100;this.mounted=true;
    this.files=new Map([
      ['Patches/Factory/Pads/柔和铺底.toml','format = 1\nname = "柔和铺底"\n'],
      ['Patches/Factory/Keys/明亮琴声.toml','format = 1\nname = "明亮琴声"\n']
    ]);
    this.reverbDelay=400;this.reverbPosts=[];this.reverbId=0;
    this.effect={api_major:2,contract_version:1,available:true,valid:true,pending:false,fault:0,
      wet_q15:8192,damp_q15:8192,applied_sequence:0,generation:1,request:null,result:null,applied:null,confirmation:'synth_parameters'};
  }
  exception(status,code){const e=Error(code);e.status=status;e.code=code;return e;}
  unwrap(result){if(result.status>=400)throw this.exception(result.status,result.body.code||result.body.error||'request_failed');return result.body;}
  async request(path,options={}) {
    this.advance();
    if(this.offline)throw Error('mock offline');
    const url=new URL(path,'http://localhost'),header=options.headers?.['X-FPGA-Control'];
    if(url.pathname==='/api/v2/state')return this.snapshot();
    if(url.pathname==='/api/v2/capabilities')return this.capabilities();
    if(url.pathname==='/api/v2/session')return this.unwrap(this.session(options.body,header));
    if(url.pathname==='/api/v2/command'){
      const result=this.command(options.body,header);
      if(this.receiptDelay)await new Promise(r=>setTimeout(r,this.receiptDelay));
      return this.unwrap(result);
    }
    if(url.pathname==='/api/v1/midi/raw')return this.unwrap(this.rawMidi(options.body,header));
    if(url.pathname==='/api/v2/effects/reverb') {
      if(options.method!=='POST')return structuredClone(this.effect);
      if(header!=='1')throw this.exception(403,'header_required');
      if(!/^[0-9]+(?: [0-9]+){4}$/.test(options.body))throw this.exception(400,'invalid_reverb');
      const [token,sequence,context,wet,damp]=options.body.split(' ').map(Number);
      const session=this.sessions.find(s=>s?.token===token);
      if(!session)throw this.exception(410,'unknown_session');
      if(context!==this.state.context_epoch||session.context!==context)throw this.exception(409,'stale_context');
      if(sequence<=session.sequence)throw this.exception(409,'stale_sequence');
      if(wet>32768||damp>28672)throw this.exception(400,'invalid_reverb');
      if(this.effect.pending)throw this.exception(409,'reverb_busy');
      session.sequence=sequence;session.seen=this.now();
      const identity={token,sequence,context_epoch:context,request_id:++this.reverbId};
      this.reverbPosts.push({identity,at:this.now(),body:options.body});this.effect.pending=true;this.effect.request=identity;
      this.schedule(this.reverbDelay,()=>{if(this.state.context_epoch!==context){this.effect.pending=false;this.effect.request=null;this.effect.result={...identity,code:5};this.effect.generation++;return;}this.effect.pending=false;this.effect.request=null;this.effect.generation++;this.effect.result={...identity,code:0};this.effect.applied={...identity,applied_sequence:++this.effect.applied_sequence};this.effect.wet_q15=wet;this.effect.damp_q15=damp;});
      return {api_major:2,accepted:sequence,context_epoch:context,stage:'received',fpga:'unverified'};
    }
    if(header!=='1')throw this.exception(403,'header_required');
    if(url.pathname==='/api/dds/presets') {
      const operation=url.searchParams.get('op')||'list',p=url.searchParams.get('path')||'';
      this.ddsCalls.push({path,method:options.method||'GET',body:options.body||'',at:this.now()});
      if(this.job&&this.now()<this.jobDue)throw this.exception(409,'busy');
      this.job={dds_api:1,job_id:++this.jobId,phase:'working',operation:{list:1,load:2,save:3,read:4,write:5,default:6,import:8}[operation],mounted:this.mounted,context_epoch:this.state.context_epoch,warnings:0,next_offset:0,path:p,error:'',completion_scope:'owner_or_file_only'};
      this.jobDue=this.now()+this.ddsDelay;this.jobOp=operation;this.jobBody=options.body;
      if(operation==='load')this.schedule(60,()=>{this.moveContext();this.job.context_epoch=this.state.context_epoch;});
      return {dds_api:1,job_id:this.jobId,phase:'wait_owner'};
    }
    if(url.pathname==='/api/dds/status'||url.pathname==='/api/dds/result') {
      if(Number(url.searchParams.get('id'))!==this.job?.job_id)throw this.exception(404,'retired');
      if(this.now()>=this.jobDue&&this.job.phase!=='done'&&this.job.phase!=='failed') {
        const exists=this.jobOp==='import'&&this.files.has(this.job.path);
        this.job.phase=exists?'failed':'done';this.job.error=exists?'target exists (or recoverable .new/.bak); explicit replacement required':'';
        if(!exists){
          if(this.jobOp==='save')this.files.set(this.job.path,'format = 1\nname = "saved"\n');
          if(this.jobOp==='write'||this.jobOp==='import')this.files.set(this.job.path,this.jobBody);
          if(this.jobOp==='default')this.defaultPath=this.job.path;
          if(this.jobOp==='load')this.loadedPath=this.job.path;
        }
      }
      if(url.pathname==='/api/dds/status')return {...this.job};
      return this.jobOp==='list'?[...this.files.keys()].join('\n')+'\n':this.files.get(this.job.path)||'';
    }
    throw this.exception(404,'not_found');
  }
}
