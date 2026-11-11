import {currentCatalogFixture as fixture} from '../../mock/fixture-catalog.mjs';
import {MockDevice} from '../../mock/device.mjs';
import {getParameter,validateParameter,getPreset} from '../../src/generated/fm-contract.mjs';
const handlerCC=[7,10,14,15,20,21,22,23,24,25,26,27,28,29,30,31,71,72,73,74,75,76,77,79,85,86,87,102,103,104,105,106,107,109,110,111,112,113];
const copy=value=>structuredClone(value);
export const turn=()=>new Promise(resolve=>setImmediate(resolve));
export async function settle(controller) {
  for(let i=0;i<400;i++){await turn();if(!controller.running&&!controller.connecting&&!controller.polling)return;}
  throw Error('controller did not settle');
}
// Independent HTTP handler model: seven decimal numbers are parsed here, never
// using the client's encoder to decide whether a server request is legal.
export class RuntimeDevice {
  constructor() {
    this.time=10;this.caps=fixture('v2-live-capabilities');this.state=fixture('v2-live-state');
    this.requests=[];this.log=[];this.sessions=new Map();this.nextToken=100;this.ownerDelay=25;
    this.events=[];this.cacheDelay=50;this.nativeDelay=25;this.nativeLog=[];this.nativeTicket=0;this.legacy=new MockDevice({now:()=>this.time});
    this.transport={request:(path,options)=>this.request(path,options),release:(body,path='/api/command')=>{this.requests.push({path:'release',target:path,body});return this.request(path,{body,headers:{'X-FPGA-Control':'1'}}).catch(()=>{});}};
  }
  async delay(ms){this.time+=ms;await turn();}
  error(status,code) {const error=Error(code||'HTTP '+status);error.status=status;error.code=code;return error;}
  flush() {
    const due=this.events.filter(e=>e.at<=this.time);this.events=this.events.filter(e=>e.at>this.time);
    for(const event of due)event.apply();
    for(const [token,session] of this.sessions)if(this.time-session.lease>1200&&this.state.clients[session.client].token===token)this.state.clients[session.client].keys=0;
  }
  snapshot(){this.flush();return copy(this.state);}
  moveContext(){this.state.context_epoch++;this.caps.context_epoch=this.state.context_epoch;this.state.clients.forEach(c=>c.keys=0);}
  parse(body) {
    if(typeof body!=='string'||body.length>95||!/^[0-9]+(?: [0-9]+){6}$/.test(body))throw this.error(400,'invalid_command');
    const numbers=body.split(' ').map(Number);
    if(!numbers.every(n=>Number.isInteger(n)&&n>=0&&n<=4294967295)||!numbers.slice(0,3).every(Boolean))throw this.error(400,'invalid_command');
    const [token,sequence,context,op,a,b,c]=numbers;
    const valid=op===2?handlerCC.includes(a)&&b<=127&&c===0:op===3?a<=33554431&&b>0&&b<=127&&c===0:op===4?a===0&&b===0&&c===0:op===5?a<=2&&c===0&&(a===2?b>0:b===0):op===6?a>=200&&a<=8000&&b===0&&c===0:op===7?a>0:op===8?b===0&&c===0:false;
    if(!valid)throw this.error(400,'invalid_command');return {token,sequence,context,op,a,b,c};
  }
  async request(path,options={}) {
    this.requests.push({path,options:copy({...options,signal:undefined})});this.flush();
    if(this.failPath===path&&this.failNetwork)throw Error('network timeout');
    if(this.failPath===path)throw this.error(this.failStatus||503,this.failCode||'owner_unavailable');
    if(this.missing&&path==='/api/v2/capabilities')throw this.error(this.missing);
    if(path.startsWith('/api/')&&!path.startsWith('/api/v2/')) {
      const result=path==='/api/state'?{status:200,body:this.legacy.snapshot()}:path==='/api/session'?this.legacy.session(options.body,options.headers?.['X-FPGA-Control']):this.legacy.command(options.body,options.headers?.['X-FPGA-Control']);
      if(result.status>=400)throw this.error(result.status);return result.body;
    }
    if(path==='/api/v2/capabilities'){if(this.onCaps)this.onCaps();return copy(this.caps);}
    if(path==='/api/v2/state'){if(this.holdState){this.holdState=false;const captured=this.snapshot();await new Promise(resolve=>this.stateGate=resolve);return captured;}return this.snapshot();}
    if(options.headers?.['X-FPGA-Control']!=='1')throw this.error(403,'header_required');
    if(path==='/api/v2/session') {
      if(options.body!=='')throw this.error(400,'empty_body_required');
      const client=0,token=this.nextToken++,context=this.state.context_epoch;
      this.sessions.set(token,{client,context,sequence:0,lease:this.time});
      return {api_major:2,api_minor:0,token,client,lease_ms:1200,context_epoch:context};
    }
    if(path!=='/api/v2/command')throw this.error(404);
    const command=this.parse(options.body),session=this.sessions.get(command.token);
    if(!session||this.time-session.lease>10000)throw this.error(410,'session_expired');
    if(session.context!==this.state.context_epoch||command.context!==this.state.context_epoch)throw this.error(409,'stale_context');
    if(command.sequence<=session.sequence)throw this.error(409,'stale_sequence');
    if(command.op===7){const parameter=getParameter(this.state.fm.profile_id,command.a);if(!parameter||!validateParameter(this.state.fm.profile_id,command.a,parameter.scope_id,command.b,command.c))throw this.error(400,'invalid_command');}
    if(command.op===8&&!getPreset(this.state.fm.profile_id,command.a))throw this.error(400,'invalid_command');
    session.sequence=command.sequence;session.lease=this.time;
    this.log.push({...command,stage:'received',at:this.time});
    this.state.clients[session.client].received=command.sequence;
    this.events.push({at:this.time+this.ownerDelay,apply:()=>this.adopt(command,session)});
    const receipt={api_major:2,accepted:command.sequence,context_epoch:command.context,stage:'received',fpga:'unverified'};
    if(this.holdWhen?.(command)){this.holdWhen=null;await new Promise(resolve=>this.gate=resolve);}
    return receipt;
  }
  adopt(command,session) {
    const slot=this.state.clients[session.client];const code=command.context!==this.state.context_epoch?5:(this.ownerCode||0);
    slot.token=command.token;slot.result_sequence=command.sequence;slot.result_code=code;slot.result_context_epoch=command.context;
    this.log.push({...command,stage:'owner',code});
    if(code)return;
    if(command.op===3){slot.processed=command.sequence;slot.keys=command.a;session.lease=this.time;}
    else if(command.op===4){slot.stop_processed=command.sequence;this.moveContext();this.beginNative(command);}
    else {
      slot.parameter_processed=command.sequence;
      if(command.op===5){this.state.mode={requested:command.a,applied:command.a,switching:false,recovering:false};if(command.a===2){this.state.fm.session_id++;this.state.fm.engine_epoch++;this.state.fm.flags=3;}this.state.legacy.ready=command.a!==2;this.moveContext();}
      if(command.op===8){this.moveContext();this.beginNative(command);}
      if(command.op===7&&!this.noCache){this.events.push({at:this.time+this.cacheDelay,apply:()=>{const values=this.state.fm.parameter_values;const item=values.find(v=>v.parameter_id===command.a&&v.target_word===command.b);if(item){item.value_u32=command.c;item.sequence_id++;}}});}
    }
  }
  beginNative(command) {
    const fm=this.state.fm;
    if(this.state.mode.applied!==2||!fm.available||!fm.valid||!fm.session_id||!(fm.flags&2))return;
    const ticket=++this.nativeTicket,session=fm.session_id,topology=fm.topology_generation;
    const identity={session_id:session,engine_epoch:fm.engine_epoch,sequence_id:(command.op===4?fm.panic.accepted?.sequence_id||0:fm.accepted?.sequence_id||0)+1};
    this.state.mode.switching=true;
    if(!this.nativeModeOnly){fm.flags=15;fm.phase=command.op===4?4:2;if(command.op===4)fm.engine_epoch++;}
    const epoch=fm.engine_epoch;
    if(command.op===4)fm.panic.accepted=copy(identity);else fm.accepted=copy(identity);
    this.nativeLog.push({stage:'accepted',op:command.op,at:this.time,context:this.state.context_epoch,identity:copy(identity)});
    this.events.push({at:this.time+this.nativeDelay,apply:()=>{
      if(ticket!==this.nativeTicket||fm.session_id!==session||fm.engine_epoch!==epoch||fm.topology_generation!==topology)return;
      this.state.mode.switching=false;
      if(!this.nativeModeOnly){fm.flags=3;fm.phase=2;}
      if(command.op===4){fm.panic.completed=copy(identity);fm.panic.result={...identity,code:0};}
      else{fm.completed=copy(identity);fm.result={...identity,code:0};}
      this.nativeLog.push({stage:'completed',op:command.op,at:this.time,context:this.state.context_epoch,identity:copy(identity)});
    }});
  }
  commands(){return this.log.filter(e=>e.stage==='received');}
}
