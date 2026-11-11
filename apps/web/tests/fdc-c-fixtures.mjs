import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import {Script} from 'node:vm';

const value=flag=>process.argv[process.argv.indexOf(flag)+1];
if(!process.argv.includes('--fixtures')||!process.argv.includes('--output'))throw Error('--fixtures and --output required');
const fixtures=resolve(value('--fixtures')),output=resolve(value('--output'));
const html=await readFile(new URL('../dist/index.html',import.meta.url),'utf8');
// Execute the exact generated production module, without running the DOM app.
// This extraction changes no bytes within its factory and adds no shipped hook.
const marker='e["fdc.mjs"]=function(){',start=html.indexOf(marker),end=html.indexOf(';e["',start+marker.length);
assert.ok(start>0&&end>start,'frozen production module factory exists');
const context={structuredClone,TextEncoder,ArrayBuffer,DataView,Set};
new Script('const e={};'+html.slice(start,end)+';globalThis.exports=e["fdc.mjs"];').runInNewContext(context);
const {FdcController,validateFdcState}=context.exports;
const provenance={};const raw={};
for(const name of ['queued_receipt','sent_observed_off','observed_enabled','unavailable']){
  raw[name]=await readFile(resolve(fixtures,name+'.json'),'utf8');
  provenance[name]={sha256:createHash('sha256').update(raw[name]).digest('hex'),bytes:Buffer.byteLength(raw[name])};
}
for(const name of ['sent_observed_off','observed_enabled','unavailable'])validateFdcState(JSON.parse(raw[name]));
let observed='sent_observed_off';const posts=[];
const owner={apiMajor:2,status:'online',token:101,sequence:0,epoch:1,inputVersion:1,runtimeState:{context_epoch:9},
  suspended:false,rearming:false,modeWaiting:false,transition:null,lastAdopted:44,now:()=>0,notify(){},kick(){},fail(error){throw error;},
  cpuMode:{supported:true,valid:true,ready:true,busy:false,fault:false,pending:false,desired:0,applied:0},
  transport:{async request(path,options={}){if(options.method==='POST'){posts.push(options.body);return JSON.parse(raw.queued_receipt);}return JSON.parse(raw[observed]);}}};
const fdc=new FdcController(owner);await fdc.poll();assert.equal(fdc.queue(1),true);await fdc.send(1);
assert.equal(fdc.command.request_id,1);assert.equal(fdc.phase,'queued');assert.equal(owner.lastAdopted,44);
await fdc.poll();assert.equal(fdc.phase,'queued','C fixture result request9 cannot confirm queued request1');
observed='observed_enabled';await fdc.poll();assert.equal(fdc.phase,'queued','enabled plus user config flag is not request completion');
// Independent C sent fixture represents an already-sent retained request9.
fdc.command={token:101,sequence:1,context:9,request_id:9,op:1,started:0,version:1};
observed='sent_observed_off';await fdc.poll();assert.equal(fdc.phase,'transport_sent');assert.equal(fdc.state.driver_enabled,false);
assert.equal(owner.lastAdopted,44);assert.match(fdc.message,/不代表 CPU 已采用/);
observed='unavailable';await fdc.poll();assert.equal(fdc.available,false);assert.equal(fdc.can(1),false);
await mkdir(output,{recursive:true});
const summary={status:'PASS',html_sha256:createHash('sha256').update(html).digest('hex'),fixture_provenance:provenance,
  checks:7,posts,source:'Exact generated production fdc.mjs factory; unmodified actual C HTTP fixture bytes parsed at transport boundary.',hardware:'NOT_RUN'};
await writeFile(resolve(output,'summary.json'),JSON.stringify(summary,null,2)+'\n');console.log(JSON.stringify(summary));
