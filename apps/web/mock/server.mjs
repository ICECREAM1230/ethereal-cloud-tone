import http from 'node:http';
import {readFile} from 'node:fs/promises';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {MockDevice} from './device.mjs';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
export async function startServer(options={}){
  const device=options.device||new MockDevice();const proxy=options.proxy||null;
  if(proxy && (!/^http:\/\/[^/]+\/?$/.test(proxy)||new URL(proxy).username||new URL(proxy).password))throw Error('proxy must be an http:// device origin without credentials/path');
  const server=http.createServer(async(req,res)=>{
    try{
      const path=new URL(req.url,'http://localhost').pathname;
      const parts=[];let bytes=0;for await(const part of req){bytes+=part.length;if(bytes>4096){res.writeHead(413);res.end();return;}parts.push(part);}const body=Buffer.concat(parts).toString('utf8');
      res.setHeader('Cache-Control','no-store');
      if(path.startsWith('/api/')){
        if(proxy){
          const abort=new AbortController();const timer=setTimeout(()=>abort.abort(),1200);
          try{
            const response=await fetch(new URL(path,proxy),{method:req.method,headers:{'X-FPGA-Control':req.headers['x-fpga-control']||'','Content-Type':'text/plain'},body:req.method==='GET'?undefined:body,signal:abort.signal});
            res.writeHead(response.status,{'Content-Type':'application/json'});res.end(await response.text());
          }finally{clearTimeout(timer);}return;
        }
        if(device.offline){req.socket.destroy();return;}
        const result=path==='/api/state'&&req.method==='GET'?{status:200,body:device.snapshot()}
          :path==='/api/session'&&req.method==='POST'?device.session(body,req.headers['x-fpga-control'])
          :path==='/api/command'&&req.method==='POST'?device.command(body,req.headers['x-fpga-control']):{status:404,body:{error:'not found'}};
        if(path==='/api/command'&&device.receiptDelay)await new Promise(done=>setTimeout(done,device.receiptDelay));
        res.writeHead(result.status,{'Content-Type':'application/json'});res.end(JSON.stringify(result.body));return;
      }
      if(!proxy&&path==='/__mock/control'&&req.method==='POST'){device.configure(JSON.parse(body));res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify({simulation:true}));return;}
      if(!proxy&&path==='/__mock/log'&&req.method==='GET'){res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify(device.log));return;}
      if(path==='/'||path==='/index.html'){
        res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Content-Security-Policy':"default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'"});
        res.end(await readFile(resolve(root,'dist/index.html')));return;
      }
      res.writeHead(404);res.end('Not found');
    }catch(error){res.writeHead(502,{'Content-Type':'application/json'});res.end(JSON.stringify({error:error.message}));}
  });
  await new Promise((done,reject)=>{server.once('error',reject);server.listen(options.port??0,options.host||'127.0.0.1',done);});
  const timer=setInterval(()=>device.advance(),20);timer.unref();server.once('close',()=>clearInterval(timer));
  return {server,device,url:`http://127.0.0.1:${server.address().port}`,close:()=>new Promise(done=>server.close(done))};
}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  const args=process.argv.slice(2);const value=(name,fallback)=>args.includes(name)?args[args.indexOf(name)+1]:fallback;
  const port=Number(value('--port','4173'));const host=value('--host','127.0.0.1');const proxy=value('--proxy',null);
  const live=await startServer({port,host,proxy});console.log(`${proxy?'Device proxy → '+proxy:'模拟设备（无声音）'}: ${live.url}`);
  for(const signal of ['SIGINT','SIGTERM'])process.on(signal,()=>{live.server.close(()=>process.exit(0));});
}
