import http from 'node:http';
import {readFile} from 'node:fs/promises';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {gzipSync} from 'node:zlib';
import {UiDevice} from '../tests/helpers/ui-device.mjs';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
export async function startUiPreview({device=new UiDevice(),port=0,htmlPath=resolve(root,'dist/index.html')}={}) {
  const gzip=gzipSync(await readFile(htmlPath));
  const server=http.createServer(async(req,res)=>{
    try{
      const url=new URL(req.url,'http://localhost');
      res.setHeader('Cache-Control','no-store');
      if(url.pathname==='/'||url.pathname==='/index.html'){
        res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Content-Encoding':'gzip','Content-Security-Policy':"default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'"});res.end(gzip);return;
      }
      if(url.pathname==='/favicon.ico'){res.writeHead(204);res.end();return;}
      if(!url.pathname.startsWith('/api/')){res.writeHead(404);res.end();return;}
      if(device.offline){req.socket.destroy();return;}
      const chunks=[];let bytes=0;
      for await(const chunk of req){bytes+=chunk.length;if(bytes>16384){res.writeHead(413);res.end();return;}chunks.push(chunk);}
      const result=await device.request(url.pathname+url.search,{method:req.method,body:Buffer.concat(chunks).toString(),headers:{'X-FPGA-Control':req.headers['x-fpga-control']}});
      const status=url.pathname==='/api/dds/presets'?202:req.method==='POST'?url.pathname.endsWith('/session')?201:202:200;
      res.writeHead(status,{'Content-Type':typeof result==='string'?'text/plain; charset=utf-8':'application/json'});res.end(typeof result==='string'?result:JSON.stringify(result));
    }catch(error){if(req.url==='/api/v2/command')device.commandErrors.push(error.code||error.message);res.writeHead(error.status||500,{'Content-Type':'application/json'});res.end(JSON.stringify({code:error.code||error.message,error:error.code||error.message}));}
  });
  await new Promise((done,reject)=>{server.once('error',reject);server.listen(port,'127.0.0.1',done);});
  const timer=setInterval(()=>device.advance(),20);timer.unref();
  const close=()=>new Promise(done=>{clearInterval(timer);server.close(done);server.closeAllConnections();});
  return {device,url:'http://127.0.0.1:'+server.address().port,close};
}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  const index=process.argv.indexOf('--port'),modeIndex=process.argv.indexOf('--mode');
  const mode=modeIndex<0?1:Number(process.argv[modeIndex+1]);
  const preview=await startUiPreview({port:index<0?0:Number(process.argv[index+1]),device:new UiDevice({mode})});
  console.log('本地模拟 · 不连接设备、不产生声音：'+preview.url);
  for(const signal of ['SIGINT','SIGTERM'])process.on(signal,async()=>{await preview.close();process.exit(0);});
}
