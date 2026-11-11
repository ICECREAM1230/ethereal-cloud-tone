import {RuntimeDevice} from './runtime-device.mjs';
// A server-side fixture, independent of the client byte encoder. It models HTTP
// queue receipt only and deliberately produces no FPGA/audio confirmation.
export class RawMidiDevice extends RuntimeDevice {
  constructor(){super();this.rawRequests=[];}
  async request(path,options={}){
    if(path!=='/api/v1/midi/raw')return super.request(path,options);
    this.rawRequests.push({path,options:structuredClone(options)});
    const body=JSON.parse(options.body),session=this.sessions.get(body.token);
    if(options.headers?.['X-FPGA-Control']!=='1')throw this.error(403,'header_required');
    if(body.source!=='web'||!Array.isArray(body.bytes)||body.bytes.length<1||body.bytes.length>256||!Array.from(body.bytes).every(b=>Number.isInteger(b)&&b>=0&&b<=255))throw this.error(400,'invalid_midi');
    if(!session)throw this.error(410,'unknown_session');
    if(body.context_epoch!==this.state.context_epoch||session.context!==body.context_epoch)throw this.error(409,'stale_context');
    if(body.sequence<=session.sequence)throw this.error(409,'stale_sequence');
    if(this.rawError)throw this.error(this.rawError,this.rawError===503?'midi_queue_full':'engine_not_ready');
    session.sequence=body.sequence;session.lease=this.time;
    this.log.push({stage:'midi-queued',sequence:body.sequence,bytes:structuredClone(body.bytes)});
    if(this.holdRaw){this.holdRaw=false;await new Promise(resolve=>this.rawGate=resolve);}
    return {accepted:true,queued_bytes:body.bytes.length};
  }
}
