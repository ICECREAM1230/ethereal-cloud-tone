import test from 'node:test';
import assert from 'node:assert/strict';
import {DdsPresetClient,DdsPresetError,validatePresetPath} from '../src/dds-presets.mjs';

const factory='Patches/Factory/Pads/Galaxy.toml';
const user='Patches/User/我的音色/温暖 铜管.toml';
const codes={list:1,load:2,save:3,read:4,write:5,default:6,import:8};
const accepted=(id=7)=>({dds_api:1,job_id:id,phase:'wait_owner'});
const state=(operation,phase='done',extra={})=>({dds_api:1,job_id:7,phase,operation:codes[operation],mounted:true,context_epoch:3,warnings:0,next_offset:0,path:operation==='list'?'':factory,error:'',completion_scope:'owner_or_file_only',...extra});
const response=(status,value)=>({status,ok:status>=200&&status<300,json:async()=>value,text:async()=>value});
function fixture(replies,{timeoutMs=1000,pollMs=5,...options}={}){
  const requests=[];let time=0;
  const client=new DdsPresetClient({timeoutMs,pollMs,now:()=>time,delay:async(ms)=>{time+=ms;},fetch:async(url,request)=>{
    requests.push({url,request});const reply=replies.shift();
    assert.ok(reply,'unexpected request '+url);
    return typeof reply==='function'?reply(url,request):reply;
  },...options});
  return {client,requests};
}
const failure=code=>error=>error instanceof DdsPresetError&&error.code===code;

test('preset path matches C UTF-8 limits, case-insensitive roots and extension, preserves original text',()=>{
  for(const path of [factory,user,'patches/user/Pads/Soft Lead.TOML','Patches/User/'+('中'.repeat(21))+'a/'+('弦'.repeat(21))+'b.toml','Patches/User/.Hidden/A.B.toml'])assert.equal(validatePresetPath(path),path);
  for(const path of ['',null,'Factory/Pads/Galaxy.toml','system.toml','banks.toml','Patches/User/Pads/a/b.toml','Patches/User/../a.toml','Patches/User/Pads/ a.toml','Patches/User/Pads/a .toml','Patches/User/Pads/a..toml','Patches/User/Pads/a\u0000.toml','Patches/User/Pads/a\\b.toml','Patches/User/Pads/a?.toml','Patches/User/Pads/\ud800.toml',factory+'\n',factory+'\r','Patches/User/'+('中'.repeat(22))+'/a.toml','Patches/User/Pads/'+('a'.repeat(65))+'.toml'])assert.throws(()=>validatePresetPath(path),failure('invalid_path'),String(path));
  assert.equal(validatePresetPath(user,{userOnly:true}),user);
  assert.throws(()=>validatePresetPath(factory,{userOnly:true}),failure('invalid_path'));
});

test('list polls to completion before retrieving text and exposes parsed preset paths',async()=>{
  const {client,requests}=fixture([response(202,accepted()),response(200,state('list','wait_owner')),response(200,state('list','working')),response(200,state('list')),response(200,factory+'\n'+user+'\n')]);
  const result=await client.execute('list');
  assert.equal(result.status.phase,'done');assert.equal(result.text,factory+'\n'+user+'\n');assert.deepEqual(result.paths,[factory,user]);
  assert.deepEqual(requests.map(r=>r.url),['/api/dds/presets','/api/dds/status?id=7','/api/dds/status?id=7','/api/dds/status?id=7','/api/dds/result?id=7']);
  for(const {request} of requests){assert.equal(request.headers['X-FPGA-Control'],'1');assert.equal(request.cache,'no-store');}
  assert.equal(requests[0].request.method,'GET');
});

test('subsequent list page is an empty POST with op=list and unsigned offset',async()=>{
  const {client,requests}=fixture([response(202,accepted()),response(200,state('list','done',{next_offset:48})),response(200,user+'\n')]);
  const result=await client.execute('list',{offset:24});
  assert.equal(requests[0].url,'/api/dds/presets?op=list&offset=24');assert.equal(requests[0].request.method,'POST');assert.equal(requests[0].request.body,'');assert.equal(result.status.next_offset,48);
});

test('load URL-encodes Chinese paths and does not mistake ready/closing for done',async()=>{
  const {client,requests}=fixture([response(202,accepted()),response(200,state('load','ready',{path:user})),response(200,state('load','closing',{path:user})),response(200,state('load','done',{path:user,context_epoch:4}))]);
  const result=await client.execute('load',{path:user});
  const url=new URL(requests[0].url,'http://device');assert.equal(url.searchParams.get('op'),'load');assert.equal(url.searchParams.get('path'),user);assert.match(requests[0].url,/%E6/);assert.equal(requests[0].request.body,'');assert.equal(result.status.context_epoch,4);assert.equal(result.text,undefined);assert.equal(result.paths,undefined);assert.equal(requests.length,4);
});

test('save and write only accept User paths; settings import and read are excluded',async()=>{
  const {client,requests}=fixture([]);
  for(const operation of ['save','write'])for(const path of [factory,'system.toml','banks.toml'])await assert.rejects(client.execute(operation,{path,text:operation==='write'?'format = 1\n':''}),failure('invalid_path'));
  for(const path of ['system.toml','banks.toml'])await assert.rejects(client.execute('read',{path}),failure('invalid_path'));
  assert.equal(requests.length,0);
});

test('save/default finish from status only; empty default clears startup selection',async()=>{
  for(const [operation,path] of [['save',user],['default',factory],['default','']]){
    const {client,requests}=fixture([response(202,accepted()),response(200,state(operation,'done',{path}))]);
    const result=await client.execute(operation,{path});assert.equal(result.status.operation,codes[operation]);assert.equal(requests.length,2);assert.equal(new URL(requests[0].url,'http://device').searchParams.get('path'),path);
  }
});

test('write submits UTF-8 TOML verbatim once, accepting exactly 16384 bytes',async()=>{
  const text='中'.repeat(5461)+'a';assert.equal(new TextEncoder().encode(text).length,16384);
  const {client,requests}=fixture([response(202,accepted()),response(200,state('write','done',{path:user}))]);
  await client.execute('write',{path:user,text});assert.equal(requests[0].request.body,text);assert.match(requests[0].request.headers['Content-Type'],/^text\/plain/);assert.equal(requests.filter(r=>r.request.method==='POST').length,1);
  for(const invalid of [text+'a','','format=1\u0000','\ud800'])await assert.rejects(client.execute('write',{path:user,text:invalid}),failure('invalid_request'));
  assert.equal(requests.length,2);
});

test('read obtains completed text but does not parse it as a path list',async()=>{
  const text='format = 1\n[filter]\ncutoff = 700\n';
  const {client}=fixture([response(202,accepted()),response(200,state('read')),response(200,text)]);
  const result=await client.execute('read',{path:factory});assert.equal(result.text,text);assert.equal(result.paths,undefined);
});

test('unsupported operations, extra bodies, offsets and invalid callback never send requests',async()=>{
  const {client,requests}=fixture([]);
  for(const [operation,options] of [['program',{}],['list',{offset:-1}],['list',{offset:4294967296}],['list',{offset:1.5}],['list',{path:factory}],['load',{path:factory,offset:1}],['read',{path:factory,text:'unexpected'}],['list',{valid:null}]])await assert.rejects(client.execute(operation,options),failure('invalid_request'));
  assert.equal(requests.length,0);
});

test('single client rejects concurrent jobs instead of queueing or replaying them',async()=>{
  let release;const pending=new Promise(resolve=>{release=resolve;});
  const {client,requests}=fixture([()=>pending,response(200,state('save','done',{path:user}))]);
  const first=client.execute('save',{path:user});
  await assert.rejects(client.execute('list'),failure('busy'));assert.equal(requests.length,1);
  release(response(202,accepted()));await first;assert.equal(requests.length,2);
});

test('a failed backend job rejects with its error and never retrieves result',async()=>{
  const {client,requests}=fixture([response(202,accepted()),response(200,state('load','failed',{error:'DDS unavailable or request context expired'}))]);
  await assert.rejects(client.execute('load',{path:factory}),error=>failure('failed')(error)&&/context expired/.test(error.message));assert.equal(requests.length,2);
});

test('job ID or operation mismatch cannot complete the submitted job',async()=>{
  for(const extra of [{job_id:8},{operation:3}]){
    const {client,requests}=fixture([response(202,accepted()),response(200,state('load','done',extra))]);
    await assert.rejects(client.execute('load',{path:factory}),failure('protocol'));assert.equal(requests.length,2);
  }
});

test('strict response validation rejects missing/wrong fields and unexpected HTTP success',async()=>{
  for(const reply of [response(200,accepted()),response(202,{...accepted(),dds_api:2}),response(202,{...accepted(),job_id:0}),response(202,{...accepted(),phase:'done'})]){
    const {client}=fixture([reply]);await assert.rejects(client.execute('list'),failure('protocol'));
  }
  for(const extra of [{mounted:'true'},{context_epoch:-1},{warnings:1.5},{next_offset:NaN},{path:5},{path:user},{error:null},{completion_scope:'fpga_confirmed'},{phase:'unknown'}]){
    const {client}=fixture([response(202,accepted()),response(200,state('list','done',extra))]);await assert.rejects(client.execute('list'),failure('protocol'));
  }
  const missing=state('list');delete missing.operation;
  const {client}=fixture([response(202,accepted()),response(200,missing)]);await assert.rejects(client.execute('list'),failure('protocol'));
});

test('404/405/501 report unsupported API, and 409 reports busy without retries',async()=>{
  for(const status of [404,405,501,409]){
    const {client,requests}=fixture([response(status,{error:'no route'})]);await assert.rejects(client.execute('save',{path:user}),failure(status===409?'busy':'unsupported'));assert.equal(requests.length,1);
  }
});

test('retired job/result rejection never falls back to the latest device result',async()=>{
  for(const stage of ['status','result']){
    const replies=[response(202,accepted())];if(stage==='result')replies.push(response(200,state('list')));replies.push(response(404,{error:'job retired or unknown'}));
    const {client,requests}=fixture(replies);await assert.rejects(client.execute('list'),error=>failure('result_unavailable')(error)&&error.resultUnknown===true);assert.equal(requests.length,stage==='result'?3:2);
  }
});

test('list rejects invalid result entries and oversized read results',async()=>{
  for(const text of ['system.toml\n',factory+'\n\n'+user+'\n',factory+'\r\n']){
    const {client}=fixture([response(202,accepted()),response(200,state('list')),response(200,text)]);await assert.rejects(client.execute('list'),failure('protocol'));
  }
  const {client}=fixture([response(202,accepted()),response(200,state('read')),response(200,'a'.repeat(16385))]);await assert.rejects(client.execute('read',{path:factory}),failure('protocol'));
});

test('initial network loss leaves write outcome unknown and never resubmits the body',async()=>{
  const {client,requests}=fixture([()=>{throw new TypeError('connection lost');}]);
  await assert.rejects(client.execute('write',{path:user,text:'format = 1\n'}),error=>failure('network')(error)&&error.resultUnknown===true);assert.equal(requests.length,1);
});

test('network loss while receiving status or result remains unknown; malformed JSON is a protocol error',async()=>{
  for(const stage of ['status','result']){
    const lost={status:200,ok:true,json:async()=>{throw new TypeError('body connection lost');},text:async()=>{throw new TypeError('body connection lost');}};
    const replies=[response(202,accepted())];if(stage==='result')replies.push(response(200,state('list')));replies.push(lost);
    const {client,requests}=fixture(replies);await assert.rejects(client.execute('list'),error=>failure('network')(error)&&error.resultUnknown===true);assert.equal(requests.length,stage==='result'?3:2);
  }
  const {client}=fixture([{status:202,json:async()=>{throw new SyntaxError('invalid JSON');}}]);await assert.rejects(client.execute('list'),failure('protocol'));
});

test('timeout bounds a hung request including body read and releases single-flight ownership',async()=>{
  for(const stage of ['fetch','body']){
    const hung=()=>new Promise(()=>{});
    const {client,requests}=fixture([stage==='fetch'?hung:{status:202,ok:true,json:hung}],{timeoutMs:20});
    await assert.rejects(client.execute('save',{path:user}),error=>failure('timeout')(error)&&error.resultUnknown===true);assert.equal(requests.length,1);
    await assert.rejects(client.execute('program'),failure('invalid_request'));
  }
});

test('overall job deadline bounds continuous working status',async()=>{
  const replies=[response(202,accepted()),...Array.from({length:5},()=>response(200,state('load','working')))];
  const {client,requests}=fixture(replies,{timeoutMs:10,pollMs:5});await assert.rejects(client.execute('load',{path:factory}),failure('timeout'));assert.equal(requests.filter(r=>r.request.method==='POST').length,1);assert.ok(requests.length<=4);
});

test('invalid context before submission sends nothing; after submission stops further requests and reports unknown',async()=>{
  const {client,requests}=fixture([]);await assert.rejects(client.execute('list',{valid:()=>false}),error=>failure('context_changed')(error)&&error.resultUnknown===false);assert.equal(requests.length,0);
  let valid=true;
  const after=fixture([()=>{valid=false;return response(202,accepted());}]);await assert.rejects(after.client.execute('load',{path:factory,valid:()=>valid}),error=>failure('context_changed')(error)&&error.resultUnknown===true);assert.equal(after.requests.length,1);
});

test('context change while polling or receiving a result cannot report a completed job',async()=>{
  for(const stage of ['poll','result']){
    let valid=true;const replies=[response(202,accepted())];
    if(stage==='result')replies.push(response(200,state('list')));
    replies.push(()=>{valid=false;return stage==='poll'?response(200,state('list')):response(200,factory+'\n');});
    const {client,requests}=fixture(replies);await assert.rejects(client.execute('list',{valid:()=>valid}),error=>failure('context_changed')(error)&&error.resultUnknown===true);assert.equal(requests.length,stage==='result'?3:2);
  }
});

test('a removed view invalidates the operation during the poll delay and sends no further requests',async()=>{
  const view={isConnected:true};
  const {client,requests}=fixture([response(202,accepted()),response(200,state('list','working'))],{delay:async()=>{view.isConnected=false;}});
  await assert.rejects(client.execute('list',{valid:()=>view.isConnected}),error=>failure('context_changed')(error)&&error.resultUnknown===true);assert.equal(requests.length,2);
});


test('create-only import has its own operation and never falls back to an overwriting write',async()=>{
  const text='format = 1\n';
  const {client,requests}=fixture([response(202,accepted()),response(200,state('import','done',{path:user}))]);
  await client.execute('import',{path:user,text});
  assert.equal(new URL(requests[0].url,'http://mock').searchParams.get('op'),'import');
  assert.equal(requests[0].request.body,text);assert.equal(requests.length,2);
  for(const path of [factory,'system.toml','banks.toml'])await assert.rejects(client.execute('import',{path,text}),failure('invalid_path'));
  const old=fixture([response(400,{error:'unknown DDS operation'})]);
  await assert.rejects(old.client.execute('import',{path:user,text}),error=>failure('http')(error)&&!error.resultUnknown);
  assert.equal(old.requests.length,1);assert.doesNotMatch(old.requests[0].url,/op=write/);
});
