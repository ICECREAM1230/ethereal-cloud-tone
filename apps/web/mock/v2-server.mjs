import http from 'node:http';
import {readFile} from 'node:fs/promises';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {V2Device} from './v2-device.mjs';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
export async function startV2Server(options={}){
  const device=options.device||new V2Device(options);
  const server=http.createServer(async(req,res)=>{
    try{
      const path=new URL(req.url,'http://localhost').pathname;const parts=[];let bytes=0;
      for await(const part of req){bytes+=part.length;if(bytes>4096){res.writeHead(413);res.end();return;}parts.push(part);}const body=Buffer.concat(parts).toString('utf8');res.setHeader('Cache-Control','no-store');
      if(path.startsWith('/api/')){
        if(device.offline){req.socket.destroy();return;}
        const result=path==='/api/v2/state'&&req.method==='GET'?{status:200,body:device.snapshot()}
          :path==='/api/v2/capabilities'&&req.method==='GET'?{status:200,body:device.capabilities()}
          :path==='/api/v2/session'&&req.method==='POST'?device.session(body,req.headers['x-fpga-control'])
          :path==='/api/v2/command'&&req.method==='POST'?device.command(body,req.headers['x-fpga-control']):{status:404,body:{api_major:2,code:'not_found'}};
        const response=path==='/api/v1/midi/raw'&&req.method==='POST'?device.rawMidi(body,req.headers['x-fpga-control']):result;
        if(path==='/api/v2/command'&&device.receiptDelay)await new Promise(done=>setTimeout(done,device.receiptDelay));
        if(path==='/api/v1/midi/raw'&&device.rawReceiptDelay)await new Promise(done=>setTimeout(done,device.rawReceiptDelay));
        res.writeHead(response.status,{'Content-Type':'application/json'});res.end(JSON.stringify(response.body));return;
      }
      if(path==='/'||path==='/index.html'){
        res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Content-Security-Policy':"default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'"});res.end(await readFile(resolve(root,'dist/index.html')));return;
      }
      res.writeHead(404);res.end('Not found');
    }catch(error){res.writeHead(502,{'Content-Type':'application/json'});res.end(JSON.stringify({error:error.message}));}
  });
  await new Promise((done,reject)=>{server.once('error',reject);server.listen(options.port??0,options.host||'127.0.0.1',done);});
  const timer=setInterval(()=>device.advance(),20);timer.unref();server.once('close',()=>clearInterval(timer));
  return {device,url:`http://127.0.0.1:${server.address().port}`,close:()=>new Promise(done=>{server.close(done);server.closeAllConnections();})};
}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  const index=process.argv.indexOf('--port');const live=await startV2Server({port:index<0?4174:Number(process.argv[index+1])});console.log(`FM v2 模拟设备（无设备声音）: ${live.url}`);
  for(const signal of ['SIGINT','SIGTERM'])process.on(signal,async()=>{await live.close();process.exit(0);});
}
