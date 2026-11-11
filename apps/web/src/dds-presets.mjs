// The preset API has its own asynchronous job slot. It must not use the
// performance command queue: STOP, key releases and heartbeats stay independent.
const DDS_OPERATIONS=Object.freeze({list:1,load:2,save:3,read:4,write:5,default:6,'import':8});
const DDS_PHASES=new Set(['wait_owner','working','ready','closing','done','failed']);
const DDS_TEXT_BYTES=16384;
const ddsEncoder=new TextEncoder();
const ddsUint32=value=>Number.isInteger(value)&&value>=0&&value<=4294967295;
const ddsString=value=>typeof value==='string'&&!/[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?<![\uD800-\uDBFF])[\uDC00-\uDFFF]/u.test(value);
const ddsBytes=value=>ddsEncoder.encode(value).length;

export class DdsPresetError extends Error{
  constructor(code,message,{resultUnknown=false,jobId=0}={}){
    super(message);this.name='DdsPresetError';this.code=code;
    this.resultUnknown=resultUnknown;this.jobId=jobId;
  }
}

// Keep this boundary identical to dds_name_valid/dds_path_valid, including byte
// lengths (not JavaScript UTF-16 lengths). The original path is preserved.
export function validatePresetPath(path,{userOnly=false}={}){
  const invalid=()=>{throw new DdsPresetError('invalid_path','音色路径须为 Patches/'+(userOnly?'User':'Factory 或 User')+'/<分类>/<名称>.toml；分类和名称各不超过 64 字节。');};
  if(!ddsString(path)||ddsBytes(path)>=192)invalid();
  const match=/^Patches\/(User|Factory)\/([^/]+)\/([^/]+)\.toml$/i.exec(path);
  if(!match||(userOnly&&match[1].toLowerCase()!=='user'))invalid();
  for(const name of [match[2],match[3]]){
    if(!name||ddsBytes(name)>64||name.startsWith(' ')||name.endsWith(' ')||name.endsWith('.')||/[\x00-\x1f\x7f\\/:*?"<>|]/.test(name))invalid();
  }
  return path;
}

export class DdsPresetClient{
  constructor({fetch=globalThis.fetch,timeoutMs=12000,pollMs=250,delay=ms=>new Promise(resolve=>setTimeout(resolve,ms)),now=()=>Date.now()}={}){
    if(typeof fetch!=='function'||typeof delay!=='function'||typeof now!=='function'||!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(pollMs)||pollMs<=0)throw new DdsPresetError('invalid_request','音色客户端配置无效。');
    this._fetch=(...args)=>fetch(...args);this._timeoutMs=timeoutMs;this._pollMs=pollMs;this._delay=delay;this._now=now;this._busy=false;
  }
  get busy(){return this._busy;}
  _error(code,message,context,resultUnknown=context.sent){
    return new DdsPresetError(code,message,{resultUnknown,jobId:context.id});
  }
  _check(context){
    let valid=false;try{valid=context.valid()===true;}catch{}
    if(!valid)throw this._error('context_changed',context.sent?'设备或引擎状态已变化，音色操作结果未知；设备已提交的任务不会被取消。':'设备或引擎状态已变化，未提交音色操作。',context);
  }
  _remaining(context){
    this._check(context);
    const remaining=context.deadline-this._now();
    if(!Number.isFinite(remaining)||remaining<=0)throw this._error('timeout','音色操作超时，结果尚未确认。',context);
    return remaining;
  }
  async _request(url,{method='GET',body='',initial=false,text=false}={},context){
    const remaining=this._remaining(context);const controller=new AbortController();let timer;
    const headers={'X-FPGA-Control':'1'};
    if(method==='POST')headers['Content-Type']='text/plain; charset=utf-8';
    const options={method,headers,cache:'no-store',signal:controller.signal};
    if(method==='POST')options.body=body;
    // There are deliberately no retries. A lost response does not establish
    // whether the device accepted a save/write/load request.
    if(initial)context.sent=true;
    const work=(async()=>{
      const response=await this._fetch(url,options);this._check(context);this._remaining(context);
      if(!response||!Number.isInteger(response.status))throw this._error('protocol','音色接口返回了无效的 HTTP 响应。',context);
      if(response.status!==(initial?202:200)){
        const knownRejected=initial;
        if([404,405,501].includes(response.status)){
          if(response.status===404&&!initial)throw this._error('result_unavailable','音色任务或结果已失效，无法确认此次操作。',context);
          throw this._error('unsupported','当前固件不支持此音色接口。',context,!knownRejected);
        }
        if(response.status===409)throw this._error('busy','设备正在处理另一项音色任务，请稍后重新操作。',context,!knownRejected);
        if(response.status>=200&&response.status<300)throw this._error('protocol','音色接口未返回预期的任务确认。',context);
        throw this._error('http','音色请求失败（HTTP '+response.status+'）。',context,!knownRejected);
      }
      const decode=text?response.text:response.json;
      if(typeof decode!=='function')throw this._error('protocol','音色接口响应缺少数据读取方法。',context);
      let value;
      try{value=await decode.call(response);}
      catch(error){if(error instanceof SyntaxError)throw this._error('protocol','无法解析音色接口返回的数据。',context);throw error;}
      this._check(context);this._remaining(context);return value;
    })();
    const expired=new Promise((_,reject)=>{timer=setTimeout(()=>{
      let error;try{this._check(context);error=this._error('timeout','音色请求超时，结果尚未确认；请勿自动重发保存或导入。',context);}catch(changed){error=changed;}
      controller.abort();reject(error);
    },Math.min(this._timeoutMs,remaining));});
    try{return await Promise.race([work,expired]);}
    catch(error){if(error instanceof DdsPresetError)throw error;this._check(context);throw this._error('network','音色请求连接失败，操作结果尚未确认。',context);}
    finally{clearTimeout(timer);}
  }
  _status(value,operation,path,offset,context){
    if(!value||typeof value!=='object'||Array.isArray(value)||value.dds_api!==1||value.job_id!==context.id||value.operation!==DDS_OPERATIONS[operation]||!DDS_PHASES.has(value.phase)||typeof value.mounted!=='boolean'||!ddsUint32(value.context_epoch)||!ddsUint32(value.warnings)||!ddsUint32(value.next_offset)||value.path!==path||!ddsString(value.error)||ddsBytes(value.error)>=160||value.completion_scope!=='owner_or_file_only'||(operation!=='list'&&value.next_offset!==0)||(operation==='list'&&value.next_offset!==0&&value.next_offset<=offset)||(value.phase==='done'&&value.error))throw this._error('protocol','音色任务状态不完整或与提交的任务不一致。',context);
    return value;
  }
  async execute(operation,{path='',text='',offset=0,valid=()=>true}={}){
    if(this._busy)throw new DdsPresetError('busy','已有音色操作正在等待完成。');
    const upload=operation==='write'||operation==='import';
    if(!Object.hasOwn(DDS_OPERATIONS,operation)||typeof path!=='string'||typeof text!=='string'||!ddsUint32(offset)||typeof valid!=='function'||(operation!=='list'&&offset!==0)||(operation==='list'&&path!=='')||(!upload&&text!==''))throw new DdsPresetError('invalid_request','音色操作或参数无效。');
    if(operation!=='list'&&!(operation==='default'&&path===''))validatePresetPath(path,{userOnly:operation==='save'||upload});
    if(upload&&(!ddsString(text)||!text||text.includes('\0')||ddsBytes(text)>DDS_TEXT_BYTES))throw new DdsPresetError('invalid_request','导入须为非空 UTF-8 TOML，大小不超过 16384 字节。');
    const context={sent:false,id:0,valid,deadline:this._now()+this._timeoutMs};
    this._busy=true;
    try{
      this._check(context);
      let url='/api/dds/presets';let method='GET';
      if(operation!=='list'||offset!==0){
        const query=new URLSearchParams({op:operation});
        if(operation==='list')query.set('offset',String(offset));else query.set('path',path);
        url+='?'+query;method='POST';
      }
      const acknowledgement=await this._request(url,{method,body:text,initial:true},context);
      if(!acknowledgement||typeof acknowledgement!=='object'||Array.isArray(acknowledgement)||acknowledgement.dds_api!==1||!ddsUint32(acknowledgement.job_id)||acknowledgement.job_id===0||acknowledgement.phase!=='wait_owner')throw this._error('protocol','设备未返回有效的音色任务编号。',context);
      context.id=acknowledgement.job_id;
      for(;;){
        const status=this._status(await this._request('/api/dds/status?id='+context.id,{},context),operation,path,offset,context);
        if(status.phase==='failed')throw this._error('failed',status.error||'设备未能完成音色操作。',context,false);
        if(status.phase==='done'){
          const result={status};
          if(operation==='list'||operation==='read'){
            const output=await this._request('/api/dds/result?id='+context.id,{text:true},context);
            if(!ddsString(output)||ddsBytes(output)>DDS_TEXT_BYTES)throw this._error('protocol','音色结果格式或长度无效。',context);
            result.text=output;
            if(operation==='list'){
              result.paths=output?output.split('\n'):[];
              if(result.paths.at(-1)==='')result.paths.pop();
              try{for(const entry of result.paths)validatePresetPath(entry);}
              catch{throw this._error('protocol','音色列表含有无效路径。',context);}
            }
          }
          this._check(context);return result;
        }
        await this._delay(Math.min(this._pollMs,this._remaining(context)));this._check(context);
      }
    }finally{this._busy=false;}
  }
}
