import {decodeCommand} from '../src/protocol.mjs';
export function initialState() {
  return {ready:true,revision:1,panic:0,rendering:false,test_tone:false,fpga:'unverified',simulation:true,
    tokens:[0,0,0,0],processed:[0,0,0,0],parameter_processed:[0,0,0,0],stop_processed:[0,0,0,0],keys:[0,0,0,0],
    engine:{available:true,valid:true,requested:0,applied:0,switching:false,busy:false,recovering:false,fault:0,recoveries:0,timeouts:0,active:0,init:0,release:0,accepted:0,mode_sequence:0,rejects:0,frames:0,deadlines:0,started:[0,0,0,0]},ks_decay_ms:3000,
    osc:[[0,0,0,0],[3,-12,0,0]],voice:1,mix:0,detune:6,spread:127,volume:207,pan:0,filter:[0,512,0,1,64],cutoff_offset:0,resonance_offset:0,
    amp:[152,32,240,40],mod:[152,32,240,40],mod_depth:2048,lfo:[[2,350,16,0],[2,350,0,0]],velocity:[64,64]};
}
export function applyCC(state,cc,value) {
  if(state.engine.requested===1 && cc!==7 && cc!==10)return;
  const env=new Map([[73,['amp',0]],[75,['amp',1]],[79,['amp',2]],[72,['amp',3]],[102,['mod',0]],[103,['mod',1]],[104,['mod',2]],[105,['mod',3]]]);
  if(env.has(cc)){const [field,index]=env.get(cc);state[field][index]=(index===2?value:127-value)*2;return;}
  const rate=value=>Math.min(65535,Math.max(1,Math.round(.03*Math.pow(1000,value/127)*16777216/48000)));
  const duty=value=>Math.trunc((.5-.5*Math.pow(10,-value/127))*16777216);
  switch(cc){
    case 7:state.volume=value*2;break;
    case 10:state.pan=value-64;break;
    case 14:case 22:state.osc[cc===14?0:1][1]=Math.floor(value*25/128)-12;break;
    case 15:case 23:state.osc[cc===15?0:1][2]=Math.trunc((value-64)*2/3);break;
    case 20:case 21:state.osc[cc-20][0]=value>>5;break;
    case 24:state.mix=value-64;break;
    case 25:case 85:state.osc[cc===25?0:1][3]=duty(value);break;
    case 26:state.voice=Math.min(2,value*3>>7);break;
    case 27:state.detune=value>>2;break;
    case 28:state.spread=value;break;
    case 29:state.filter[2]=value*3>>7;break;
    case 30:state.filter[3]=Number(value>=64);break;
    case 31:state.filter[4]=value;break;
    case 71:state.resonance_offset=Math.trunc(value*5632/127)-state.filter[1];break;
    case 74:state.cutoff_offset=(value<<7)|((state.cutoff_offset+8192)&127);state.cutoff_offset-=8192;break;
    case 106:state.cutoff_offset=((state.cutoff_offset+8192)&16256)+value-8192;break;
    case 76:case 109:state.lfo[cc===76?0:1][1]=rate(value);break;
    case 77:state.lfo[0][2]=value*4;break;
    case 86:case 87:state.velocity[cc-86]=value;break;
    case 107:state.mod_depth=(value-64)*Math.abs(value-64)*4;break;
    case 110:state.lfo[1][2]=value*[16,32,4,32][state.lfo[1][3]];break;
    case 111:case 113:state.lfo[cc===113?0:1][0]=value>>5;break;
    case 112:state.lfo[1][3]=value>>5;break;
  }
}
export class MockDevice {
  constructor(options={}) {
    this.now=options.now||(()=>performance.now());this.ownerDelay=options.ownerDelay??20;this.modeDelay=options.modeDelay??120;
    this.state=initialState();this.sessions=Array.from({length:4},()=>({token:0,seen:-Infinity,sequence:0}));
    this.armed=[true,true,true,true];this.expires=[0,0,0,0];this.events=[];this.log=[];this.nextToken=100000;
    this.externalNotes=[];this.modeDue=null;this.offline=false;this.receiptDelay=0;
  }
  response(status,body){return {status,body};}
  session(body='',header=null) {
    this.advance();if(header!=='1')return this.response(403,{error:'header required'});
    if(body!=='')return this.response(400,{error:'empty body required'});
    const client=this.sessions.findIndex(session=>!session.token || this.now()-session.seen>=10000);
    if(client<0)return this.response(503,{error:'four active pages; retry later'});
    const token=++this.nextToken;this.sessions[client]={token,sequence:0,seen:this.now()};
    this.events.push({start:true,client,token,due:this.now()+this.ownerDelay});
    return this.response(201,{token,client,lease_ms:1200});
  }
  command(body,header=null) {
    this.advance();if(header!=='1')return this.response(403,{error:'header required'});
    let command;try{command=decodeCommand(body);}catch{return this.response(400,{error:'invalid command'});}
    const client=this.sessions.findIndex(session=>session.token===command.token);
    if(client<0 || this.now()-this.sessions[client].seen>=10000)return this.response(410,{error:'session expired or unknown'});
    const session=this.sessions[client];if(command.sequence<=session.sequence)return this.response(409,{error:'stale sequence'});
    session.sequence=command.sequence;session.seen=this.now();
    const event={...command,client,expires:this.now()+1200,due:this.now()+this.ownerDelay,received:this.now()};
    this.events.push(event);this.log.push({...event,stage:'received'});
    return this.response(202,{accepted:command.sequence,stage:'received',fpga:'unverified'});
  }
  panic() {
    this.state.keys.fill(0);this.armed.fill(false);this.state.panic++;this.state.test_tone=false;this.externalNotes=[];
  }
  advance() {
    const now=this.now();
    for(let index=0;index<4;index++)if(this.state.keys[index]&&now>=this.expires[index]){this.state.keys[index]=0;this.armed[index]=false;this.state.revision++;}
    const ready=this.events.filter(event=>event.due<=now);this.events=this.events.filter(event=>event.due>now);
    for(const event of ready){
      const i=event.client;const s=this.state;
      if(event.start){s.tokens[i]=event.token;s.keys[i]=0;s.processed[i]=s.parameter_processed[i]=s.stop_processed[i]=0;this.armed[i]=true;s.revision++;continue;}
      if(s.tokens[i]!==event.token || event.sequence<=s.processed[i])continue;
      s.processed[i]=event.sequence;
      if(event.op===2){applyCC(s,event.a,event.b);s.parameter_processed[i]=event.sequence;}
      if(event.op===3){
        if(now>=event.expires){s.keys[i]=0;this.armed[i]=false;}
        else {this.expires[i]=event.expires;if(event.a===0)this.armed[i]=true;if(this.armed[i]&&!s.engine.switching&&!s.engine.recovering)s.keys[i]=event.a;}
      }
      if(event.op===4){this.panic();s.stop_processed[i]=event.sequence;}
      if(event.op===5 && (event.a===0 || s.engine.available)){
        this.panic();s.engine.requested=event.a;s.engine.switching=true;s.engine.valid=false;s.parameter_processed[i]=event.sequence;
        this.modeDue=now+this.modeDelay;s.engine.mode_sequence++;
      }
      if(event.op===6){s.ks_decay_ms=event.a;s.parameter_processed[i]=event.sequence;}
      s.revision++;this.log.push({...event,stage:'owner',keys:[...s.keys],at:now});
    }
    if(this.modeDue!==null&&now>=this.modeDue&&!this.state.engine.recovering){const e=this.state.engine;e.applied=e.requested;e.valid=e.available;e.switching=false;this.modeDue=null;this.state.revision++;}
    const engine=this.state.engine;
    if(engine.requested===1){const count=this.state.keys.reduce((sum,mask)=>sum+mask.toString(2).replaceAll('0','').length,0);engine.active=(1<<Math.min(4,count))-1;}
    else engine.active=0;
  }
  snapshot(){this.advance();return structuredClone(this.state);}
  configure(values) {
    for(const key of ['ownerDelay','modeDelay','receiptDelay'])if(Number.isFinite(values[key])&&values[key]>=0&&values[key]<=10000)this[key]=values[key];
    if(typeof values.offline==='boolean')this.offline=values.offline;
    if(typeof values.available==='boolean'){this.state.engine.available=values.available;this.state.engine.valid=values.available;this.state.revision++;}
    if(typeof values.recovering==='boolean'){
      this.state.engine.recovering=values.recovering;this.state.engine.switching=values.recovering;this.state.engine.valid=!values.recovering&&this.state.engine.available;
      if(values.recovering){this.panic();this.state.engine.fault=2;this.state.engine.recoveries++;}
      this.state.revision++;
    }
    if(values.panic){this.panic();this.state.revision++;}
    if(values.clearLog)this.log.length=0;
  }
}
