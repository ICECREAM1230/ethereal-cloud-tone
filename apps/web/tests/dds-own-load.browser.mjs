import assert from 'node:assert/strict';
import http from 'node:http';
import {createHash} from 'node:crypto';
import {readFile,mkdir,writeFile,open as openDirectory} from 'node:fs/promises';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {chromium} from 'playwright';
import {ReverbDevice} from './helpers/reverb-device.mjs';
import {applyCC} from '../mock/device.mjs';

// Independent owner/HTTP seam: sessions start on KS; route selection is owner
// metadata only. A successful DDS LOAD all_sound_off changes context and panic,
// while only that exact asynchronous job reports its resulting context.
class OwnLoadDevice extends ReverbDevice {
  constructor(variant){
    super();this.variant=variant;this.http=[];this.ddsCalls=[];this.commandErrors=[];
    this.state.mode={requested:0,applied:0,switching:false,recovering:false};this.state.legacy.ready=true;
    this.caps.cpu_mode={version:1,route_only:true,channel_routes:true,modes:[0,1,2]};
    this.state.cpu_mode={version:1,route_only:true,channel_routes:true,valid:true,ready:true,busy:false,fault:false,pending:false,sequence:1,desired:0,applied:0};
    this.state.clients.forEach(c=>c.midi_channel=1);
    this.state.legacy.inputs={version:1,available:true,initialized:true,enabled:true,ready:true,capabilities_known:true,independent_usb:true,usb_configured:false,usb_error:false,stop_pending:false,fault:0,stop_requested:0,stop_completed:0,stop_fault:0};
    this.files=new Map([['Patches/Factory/Pads/Supersaw.toml','format = 1\nname = "Supersaw"\n'],['Patches/Factory/Pads/Galaxy.toml','format = 1\nname = "Galaxy"\n']]);
    this.jobId=0;this.loadCount=0;this.restorePending=null;
  }
  parse(body){
    if(typeof body==='string'&&/^[0-9]+(?: [0-9]+){6}$/.test(body)){
      const [token,sequence,context,op,a,b,c]=body.split(' ').map(Number);
      if(op===5&&[0,1,2].includes(a)&&b===0&&c===0)return {token,sequence,context,op,a,b,c};
    }
    return super.parse(body);
  }
  advanceLoad(){
    if(!this.job||this.jobOp!=='load'||this.jobApplied||this.variant==='failed_job'||this.variant==='timeout_job'||this.time<this.applyDue)return;
    this.jobApplied=true;this.moveContext();this.state.legacy.panic=(this.state.legacy.panic+1)>>>0;
    this.job.context_epoch=this.state.context_epoch;
    if(this.variant==='extra_context')this.moveContext();
    if(this.variant==='extra_panic')this.state.legacy.panic=(this.state.legacy.panic+1)>>>0;
  }
  adopt(command,session){
    if(command.op===5){
      if(this.jobApplied&&command.a===0&&this.variant==='import_load'&&!this.restoreReleased){this.restorePending={command,session};return;}
      const slot=this.state.clients[session.client];
      Object.assign(slot,{token:command.token,result_sequence:command.sequence,result_code:0,result_context_epoch:command.context,parameter_processed:command.sequence,midi_channel:command.a});
      this.log.push({...command,stage:'owner',code:0});return;
    }
    if(command.op===2&&this.variant==='unsupported_cc'){this.ownerCode=2;super.adopt(command,session);this.ownerCode=0;return;}
    super.adopt(command,session);
    if(command.op===2)applyCC(this.state.legacy,command.a,command.b);
    if(command.op===4)this.state.legacy.panic=(this.state.legacy.panic+1)>>>0;
  }
  releaseRestore(){this.restoreReleased=true;const pending=this.restorePending;this.restorePending=null;if(pending)this.adopt(pending.command,pending.session);}
  async request(path,options={}){
    this.advanceLoad();
    let result;
    if(!path.startsWith('/api/dds/')){
      result=await super.request(path,options);
      if(path==='/api/v2/session')this.state.clients[result.client].midi_channel=1;
    }else{
      const u=new URL(path,'http://127.0.0.1');
      this.ddsCalls.push({at:this.time,path,method:options.method||'GET',body:options.body||''});
      if(options.headers?.['X-FPGA-Control']!=='1')throw this.error(403,'header_required');
      if(u.pathname==='/api/dds/presets'){
        const op=u.searchParams.get('op')||'list',p=u.searchParams.get('path')||'';
        this.job={dds_api:1,job_id:++this.jobId,phase:'working',operation:{list:1,load:2,save:3,read:4,write:5,default:6,import:8}[op],mounted:true,context_epoch:this.state.context_epoch,warnings:0,next_offset:0,path:p,error:'',completion_scope:'owner_or_file_only'};
        this.jobOp=op;this.jobBody=options.body;this.jobApplied=false;this.jobDue=this.time+(op==='load'?2600:100);this.applyDue=this.time+70;
        if(op==='load'){this.loadCount++;this.loadStarted=this.time;}
        result={dds_api:1,job_id:this.jobId,phase:'wait_owner'};
      }else if(Number(u.searchParams.get('id'))!==this.job?.job_id||this.variant==='retired_job'&&this.jobOp==='load')throw this.error(404,'retired');
      else if(u.pathname==='/api/dds/status'){
        this.advanceLoad();
        if(this.time>=this.jobDue&&this.job.phase==='working'&&!(this.variant==='timeout_job'&&this.jobOp==='load')){
          this.job.phase=this.variant==='failed_job'&&this.jobOp==='load'?'failed':'done';this.job.error=this.job.phase==='failed'?'injected load failure':'';
          if(this.job.phase==='done'&&['import','write'].includes(this.jobOp))this.files.set(this.job.path,this.jobBody);
        }
        result={...this.job};
        if(this.jobOp==='load'){
          if(this.variant==='wrong_job')result.job_id++;
          if(this.variant==='wrong_path')result.path='Patches/Factory/Pads/Supersaw.toml';
          if(this.variant==='wrong_operation')result.operation=3;
        }
      }else if(u.pathname==='/api/dds/result')result=this.jobOp==='list'?[...this.files.keys()].join('\n')+'\n':this.files.get(this.job.path);
      else throw this.error(404,'not_found');
    }
    this.http.push({at:this.time,path,method:options.method||'GET',body:options.body||'',response:structuredClone(result)});return result;
  }
}
const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'artifacts/dds-own-load');
const scratch=resolve(process.env.WEB_UI_SCRATCH_DIR||fileURLToPath(new URL('../artifacts/dds-own-load-scratch',import.meta.url)));
await mkdir(output,{recursive:true});await mkdir(scratch,{recursive:true});
const htmlPath=resolve(process.env.TEST_HTML||fileURLToPath(new URL('../dist/index.html',import.meta.url))),html=await readFile(htmlPath);
// Short /proc FD alias keeps Chrome Unix socket paths below the Linux limit.
const directory=await openDirectory(scratch,'r');process.env.TMPDIR='/proc/'+process.pid+'/fd/'+directory.fd;
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const reports=[],errors=[];const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function until(fn,label,timeout=6500){const start=performance.now();while(!await fn()){if(performance.now()-start>timeout)throw Error('timeout: '+label);await wait(20);}}
async function capture(page){return page.evaluate(()=>({dds:document.querySelector('#dds').getAttribute('aria-pressed'),ks:document.querySelector('#ks').getAttribute('aria-pressed'),status:document.querySelector('#ddsPresetStatus').textContent,loaded:document.querySelector('#ddsLoadedPreset').textContent,feedback:document.querySelector('#feedback').textContent,connection:document.querySelector('#connectionText').textContent,raw:JSON.parse(document.querySelector('#raw').textContent)}));}
const variants=(process.env.TEST_VARIANT?process.env.TEST_VARIANT.split(','):['import_load','unsupported_cc','failed_job','suspend','panic','extra_context','extra_panic','wrong_job','wrong_path','wrong_operation','retired_job','timeout_job']);
try{
 for(const variant of variants){
  const device=new OwnLoadDevice(variant),server=http.createServer(async(req,res)=>{
    try{
      const u=new URL(req.url,'http://127.0.0.1');if(u.pathname==='/'){res.writeHead(200,{'Content-Type':'text/html; charset=utf-8'});res.end(html);return;}if(u.pathname==='/favicon.ico'){res.writeHead(204);res.end();return;}
      const chunks=[];for await(const chunk of req)chunks.push(chunk);device.time=performance.now();
      const result=await device.request(u.pathname+u.search,{method:req.method,body:Buffer.concat(chunks).toString(),headers:{'X-FPGA-Control':req.headers['x-fpga-control']}});
      const status=u.pathname==='/api/dds/presets'?202:req.method==='POST'?u.pathname.endsWith('/session')?201:202:200;
      res.writeHead(status,{'Content-Type':typeof result==='string'?'text/plain; charset=utf-8':'application/json'});res.end(typeof result==='string'?result:JSON.stringify(result));
    }catch(error){if(req.url==='/api/v2/command')device.commandErrors.push({at:device.time,error:error.code||error.message});res.writeHead(error.status||500,{'Content-Type':'application/json'});res.end(JSON.stringify({code:error.code||error.message,error:error.code||error.message}));}
  });
  await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const context=await browser.newContext({viewport:{width:1440,height:1000},serviceWorkers:'block'}),page=await context.newPage();
  await page.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());page.on('pageerror',e=>errors.push({variant,message:e.message}));
  let before,cancellation;
  try{
    await page.goto('http://127.0.0.1:'+server.address().port);await until(()=>page.locator('#dds').isEnabled(),'mode selector');await page.locator('#dds').click();
    await until(async()=>JSON.parse(await page.locator('#raw').textContent()).clients[0].midi_channel===0,'DDS route adoption');
    await until(()=>page.locator('#ddsLoad').isEnabled(),'library ready');
    before=await capture(page);const oldToken=before.raw.clients[0].token;
    if(variant==='unsupported_cc'){
      await page.locator('#cc73').evaluate(el=>{el.value=90;el.dispatchEvent(new Event('input',{bubbles:true}));});
      await until(()=>device.log.some(c=>c.op===2&&c.a===73&&c.stage==='owner'&&c.code===2),'unsupported owner result');
      await wait(500);const rejected=await capture(page);
      assert.match(rejected.feedback,/未采用|拒绝|不支持/,'the rejection is explicitly visible');
      assert.equal(rejected.raw.clients[0].token,oldToken,'parameter rejection preserves valid session');assert.equal(rejected.dds,'true');assert.equal(await page.locator('#ddsLoad').isEnabled(),true,'owner rejection clears only the rejected parameter pending state');
      assert.deepEqual(rejected.raw.legacy.amp,before.raw.legacy.amp,'rejected ADSR does not change patch');assert.equal(device.http.filter(r=>r.path==='/api/v2/session').length,1);assert.equal(device.commands().filter(c=>c.op===2&&c.a===73).length,1);
    }else{
      if(variant==='import_load'){
        await page.locator('#ddsImportFile').setInputFiles({name:'Transaction.toml',mimeType:'text/plain',buffer:Buffer.from('format = 1\nname = "Transaction"\n')});
        await until(()=>page.locator('#ddsImport').isEnabled(),'import ready');await page.locator('#ddsImport').click();await until(async()=>/已导入/.test(await page.locator('#ddsPresetStatus').textContent()),'import saved');
        assert.equal(device.loadCount,0,'import saves without implicitly loading');assert.match(await page.locator('#ddsLoadedPreset').textContent(),/尚未/);
        await page.locator('[data-key="0"]').dispatchEvent('pointerdown',{pointerId:91,pointerType:'touch',button:0,bubbles:true});await until(()=>device.commands().some(c=>c.op===3&&c.a===1),'held note before load');
      }else await page.locator('#ddsPresetList').selectOption('Patches/Factory/Pads/Galaxy.toml');
      await page.locator('#ddsLoad').click();await until(()=>device.loadCount===1,'LOAD posted');
      if(variant==='suspend')await page.evaluate(()=>window.dispatchEvent(new Event('blur')));
      if(variant==='panic')await page.locator('#panic').click();
      if(variant==='import_load'){
        await until(()=>!!device.restorePending,'fresh-session DDS route awaiting owner adoption');
        assert.match(await page.locator('#ddsLoadedPreset').textContent(),/尚未/,'loaded status waits for owner route adoption');
        assert.equal(await page.locator('#cc73').isDisabled(),true,'parameters stay fenced while route is pending');device.releaseRestore();
        await until(async()=>/本次已加载/.test(await page.locator('#ddsLoadedPreset').textContent()),'LOAD success visible after adoption');
        const after=await capture(page);assert.equal(after.dds,'true');assert.equal(after.ks,'false');assert.equal(after.raw.context_epoch,before.raw.context_epoch+1);assert.equal(after.raw.legacy.panic,(before.raw.legacy.panic+1)>>>0);
        const commands=device.commands(),fresh=commands.filter(c=>c.token!==oldToken);assert.deepEqual(fresh.map(c=>[c.op,c.a]),[[3,0],[5,0]],'fresh session only confirms empty and selects requested DDS once');
        assert.equal(commands.filter(c=>c.op===3&&c.a!==0).length,1,'old held key is not replayed');assert.equal(device.http.filter(r=>r.path==='/api/v2/session').length,2);assert.deepEqual(device.commandErrors,[],'no stale-context old heartbeat escapes during LOAD');
      }else{
        await until(async()=>await page.locator('#ddsPresetsPanel').getAttribute('data-busy')==='false','LOAD transaction finishes',variant==='timeout_job'?15500:7000);
        await wait(600);const after=await capture(page);assert.match(after.loaded,/尚未/,'cancelled or failed LOAD is not shown as loaded');
        const freshSelections=device.commands().filter(c=>c.op===5&&c.a===0&&c.at>=device.loadStarted);assert.equal(freshSelections.length,0,'failed/cancelled transaction cannot restore DDS automatically');
        if(variant==='suspend')assert.match(after.connection,/暂停|离线|已断开|失焦/);
        if(!['suspend','panic'].includes(variant)){
          cancellation=after;
          assert.equal(await page.locator('#reconnect').isEnabled(),true,'cancelled LOAD permits an explicit recovery');
          const count=device.commands().length;await page.locator('#reconnect').click();
          await until(async()=>JSON.parse(await page.locator('#raw').textContent()).clients[0].token!==oldToken,'explicit empty reconnect');
          await until(async()=>/已连接/.test(await page.locator('#connectionText').textContent()),'explicit reconnect adopted');
          const recovered=await capture(page);assert.equal(recovered.raw.clients[0].keys,0);assert.equal(recovered.ks,'true','explicit recovery follows the fresh session KS default');
          assert.ok(device.commands().slice(count).every(c=>c.op===3&&c.a===0),'explicit recovery only confirms empty and never repeats LOAD/route/old keys');
        }
        if(variant==='panic')assert.ok(device.commands().some(c=>c.op===4),'Panic preempts the load fence');
      }
      assert.equal(device.loadCount,1,'LOAD is submitted exactly once');
    }
    const after=await capture(page);await page.screenshot({path:resolve(output,variant+'.png')});reports.push({variant,result:'PASS',before,cancellation,after,commands:device.commands(),ddsCalls:device.ddsCalls,http:device.http,commandErrors:device.commandErrors});console.log('PASS '+variant);
  }catch(error){reports.push({variant,result:'FAIL',error:error.message,before,after:await capture(page),commands:device.commands(),ddsCalls:device.ddsCalls,http:device.http,commandErrors:device.commandErrors});await page.screenshot({path:resolve(output,variant+'-failure.png')});console.error('FAIL '+variant+': '+error.message);}
  finally{await context.close();await new Promise(r=>{server.close(r);server.closeAllConnections();});}
 }
 assert.deepEqual(errors,[]);if(reports.some(r=>r.result==='FAIL'))process.exitCode=1;
}finally{
 await writeFile(resolve(output,'report.json'),JSON.stringify({browser:browser.version(),htmlPath,htmlBytes:html.length,htmlSHA256:createHash('sha256').update(html).digest('hex'),physicalDeviceVerified:false,model:'Independent local HTTP/owner model: successful LOAD changes opaque context and panic; newly allocated sessions start at KS; route selection is an adopted metadata update.',reports,errors},null,2)+'\n');await browser.close();await directory.close();
}
