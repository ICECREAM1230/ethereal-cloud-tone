import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import {startServer} from '../mock/server.mjs';
import {postOptions} from '../src/protocol.mjs';

test('standalone HTTP mock applies ABI, serves offline HTML and enforces header/body',async()=>{
  const live=await startServer();
  try{
    const html=await fetch(live.url);assert.equal(html.status,200);assert.match(html.headers.get('content-security-policy'),/connect-src 'self'/);assert.match(await html.text(),/声场/);
    assert.equal((await fetch(live.url+'/api/session',{method:'POST'})).status,403);
    assert.equal((await fetch(live.url+'/api/session',postOptions('x'))).status,400);
    const response=await fetch(live.url+'/api/session',postOptions(''));assert.equal(response.status,201);const {token}=await response.json();
    const cmd=await fetch(live.url+'/api/command',postOptions(`${token} 1 3 1 100`));assert.equal(cmd.status,202);assert.deepEqual(await cmd.json(),{accepted:1,stage:'received',fpga:'unverified'});
    await new Promise(done=>setTimeout(done,60));const state=await(await fetch(live.url+'/api/state')).json();assert.equal(state.keys[0],1);assert.equal(state.processed[0],1);assert.equal(state.simulation,true);
    assert.equal((await fetch(live.url+'/api/command',postOptions(`${token} 1 3 0 100`))).status,409);
    assert.equal((await fetch(live.url+'/api/command',postOptions(`${token} 2 2 64 100`))).status,400);
    const cors=await fetch(live.url+'/api/state',{headers:{Origin:'https://example.com'}});assert.equal(cors.headers.get('access-control-allow-origin'),null);
  }finally{await live.close();}
});
test('same-origin proxy preserves requests without modifying device CORS',async()=>{
  const seen=[];
  const target=http.createServer(async(req,res)=>{const parts=[];for await(const part of req)parts.push(part);seen.push({url:req.url,header:req.headers['x-fpga-control'],body:Buffer.concat(parts).toString()});res.writeHead(202,{'Content-Type':'application/json'});res.end('{"accepted":7,"stage":"received","fpga":"unverified"}');});
  await new Promise(done=>target.listen(0,'127.0.0.1',done));
  const live=await startServer({proxy:`http://127.0.0.1:${target.address().port}`});
  try{
    const result=await fetch(live.url+'/api/command',postOptions('100 7 3 0 100'));assert.equal(result.status,202);assert.deepEqual(seen,[{url:'/api/command',header:'1',body:'100 7 3 0 100'}]);
    assert.equal((await fetch(live.url+'/__mock/log')).status,404);assert.equal(result.headers.get('access-control-allow-origin'),null);
  }finally{await live.close();await new Promise(done=>target.close(done));}
});
