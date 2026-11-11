import assert from 'node:assert/strict';
import http from 'node:http';
import {readFile,mkdir,writeFile,open as openDirectory} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {gzipSync} from 'node:zlib';
import {chromium} from 'playwright';
import {ReverbDevice} from './helpers/reverb-device.mjs';

// Independent model of the C DDS job slot: accepting a request does not adopt it.
class DdsDevice extends ReverbDevice {
  constructor(){
    super();this.state.mode={requested:0,applied:0,switching:false,recovering:false};this.state.legacy.ready=true;
    this.ddsCalls=[];this.commandErrors=[];this.jobId=0;this.ddsDelay=100;this.mounted=true;this.files=new Map([
      ['Patches/Factory/Pads/Supersaw.toml','format = 1\nname = "Supersaw"\n'],
      ['Patches/Factory/Pads/Galaxy.toml','format = 1\nname = "Galaxy"\n']]);
  }
  async request(path,options={}){
    if(!path.startsWith('/api/dds/'))return super.request(path,options);
    this.flush();
    if(this.ddsMissing)throw this.error(404,'not_found');
    const u=new URL(path,'http://localhost');this.ddsCalls.push({path,method:options.method||'GET',body:options.body||'',headers:options.headers});
    if(options.headers?.['X-FPGA-Control']!=='1')throw this.error(403,'header_required');
    if(u.pathname==='/api/dds/presets'){
      if(this.forceBusy)throw this.error(409,'busy');
      const op=u.searchParams.get('op')||'list',p=u.searchParams.get('path')||'';
      this.job={dds_api:1,job_id:++this.jobId,phase:'working',operation:{list:1,load:2,save:3,read:4,write:5,default:6,import:8}[op],mounted:this.mounted,context_epoch:this.state.context_epoch,warnings:0,next_offset:0,path:p,error:'',completion_scope:'owner_or_file_only'};
      this.jobDue=performance.now()+this.ddsDelay;this.jobOp=op;this.jobBody=options.body;
      if(op==='load'&&!this.failDds){
        const job=this.job,context=this.state.context_epoch;
        this.events.push({at:this.time+Math.min(120,this.ddsDelay),apply:()=>{
          if(this.job!==job)return;
          if(this.state.context_epoch!==context){job.phase='failed';job.error='owner context changed';return;}
          this.moveContext();this.state.legacy.panic=(this.state.legacy.panic+1)>>>0;
          job.context_epoch=this.state.context_epoch;
        }});
      }
      return {dds_api:1,job_id:this.jobId,phase:'wait_owner'};
    }
    if(Number(u.searchParams.get('id'))!==this.job?.job_id)throw this.error(404,'retired');
    if(u.pathname==='/api/dds/status'){
      if(this.holdDdsStatus){this.holdDdsStatus=false;await new Promise(resolve=>this.ddsStatusGate=resolve);}
      if(performance.now()>=this.jobDue&&this.job.phase==='working'){
        const exists=this.jobOp==='import'&&this.files.has(this.job.path);
        this.job.phase=this.failDds||exists?'failed':'done';this.job.error=exists?'target exists (or recoverable .new/.bak); explicit replacement required':this.failDds?(this.failDdsError||'injected IO failure'):'';
        if(this.job.phase==='done'){
          if(this.jobOp==='save')this.files.set(this.job.path,'format = 1\nname = "saved"\n');
          if(this.jobOp==='write'||this.jobOp==='import')this.files.set(this.job.path,this.jobBody);
          if(this.jobOp==='default')this.defaultPath=this.job.path;
          if(this.jobOp==='load')this.loadedPath=this.job.path;
        }
      }
      return {...this.job};
    }
    if(u.pathname==='/api/dds/result')return this.jobOp==='list'?[...this.files.keys()].join('\n')+'\n':this.files.get(this.job.path);
    throw this.error(404,'not_found');
  }
}
const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'output/playwright/dds-presets');await mkdir(output,{recursive:true});
const html=await readFile(new URL('../dist/index.html',import.meta.url)),gzip=gzipSync(html);
const tempDirectory=await openDirectory('/media/ice/D;/FPGA_simulation/esp32_dds_fix_20261104/ui/browser','r');process.env.TMPDIR='/proc/'+process.pid+'/fd/'+tempDirectory.fd;
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const checks=[],errors=[];let live;
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function until(fn,label,timeout=6000){const start=performance.now();while(!await fn()){if(performance.now()-start>timeout)throw Error('timeout: '+label);await wait(20);}}
async function open(configure=()=>{},js=true,width=390){
  if(live){await live.context.close();await live.close();}
  const device=new DdsDevice();configure(device);
  const server=http.createServer(async(req,res)=>{
    try{
      const url=new URL(req.url,'http://localhost');
      if(url.pathname==='/'){res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Content-Encoding':'gzip'});res.end(gzip);return;}
      if(url.pathname==='/favicon.ico'){res.writeHead(204);res.end();return;}
      const parts=[];for await(const part of req)parts.push(part);device.time=performance.now();
      const result=await device.request(url.pathname+url.search,{method:req.method,body:Buffer.concat(parts).toString(),headers:{'X-FPGA-Control':req.headers['x-fpga-control']}});
      if(device.dropImportAck&&url.searchParams.get('op')==='import'){res.writeHead(202,{'Content-Type':'application/json'});res.write('{\"dds_api\":1,');setTimeout(()=>res.destroy(),20);return;}
      const status=url.pathname==='/api/dds/presets'?202:req.method==='POST'?url.pathname.endsWith('/session')?201:202:200;
      res.writeHead(status,{'Content-Type':typeof result==='string'?'text/plain; charset=utf-8':'application/json'});res.end(typeof result==='string'?result:JSON.stringify(result));
    }catch(e){if(req.url==='/api/v2/command')device.commandErrors.push(e.code||e.message);res.writeHead(e.status||500,{'Content-Type':'application/json'});res.end(JSON.stringify({code:e.code||e.message,error:e.code||e.message}));}
  });
  await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const context=await browser.newContext({viewport:{width,height:844},isMobile:width<700,hasTouch:width<700,javaScriptEnabled:js});
  const page=await context.newPage();page.setDefaultTimeout(3000);await page.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());page.on('pageerror',e=>errors.push(e.message));
  live={device,page,context,close:()=>new Promise(r=>{server.close(r);server.closeAllConnections();})};
  await page.goto('http://127.0.0.1:'+server.address().port);
  assert.equal(await page.locator('#ddsPresetsPanel').count(),1,'built page contains the DDS preset library');
  return live;
}
async function listed(page){try{await until(()=>page.locator('#ddsLoad').isEnabled(),'preset list ready');}catch(error){console.log(await page.locator('#ddsPresetStatus').textContent(),await page.locator('#connectionText').textContent(),await page.locator('#engineStatus').textContent(),errors);console.log(live.device.ddsCalls);throw error;}}
async function done(page,text){await until(async()=>new RegExp(text).test(await page.locator('#ddsPresetStatus').textContent()),text);}
async function check(name,run){if(process.env.DDS_BROWSER_FILTER&&!name.includes(process.env.DDS_BROWSER_FILTER))return;await run();checks.push({name,result:'PASS'});console.log('PASS '+name);}
try{
  await check('startup disables preset writes without running script',async()=>{
    const {page,device}=await open(()=>{},false);assert.equal(await page.locator('#ddsLoad').isDisabled(),true);assert.equal(device.ddsCalls.length,0);
  });
  await check('phone lists Factory presets without automatically loading and has no horizontal overflow',async()=>{
    const supplied=[
      ['Patches/Factory/Pads/Supersaw.toml','铺底 / 超级锯齿'],
      ['Patches/Factory/Pads/Galaxy.toml','铺底 / 银河'],
      ['Patches/User/Bass/Synth Bass.toml','贝斯 / 合成贝斯'],
      ['Patches/User/Leads/Synth Lead.toml','主奏 / 合成主奏'],
      ['Patches/User/Pads/Warm Pad.toml','铺底 / 温暖铺底'],
      ['Patches/User/Custom/My Pad.toml','Custom / My Pad']
    ];
    const {page,device}=await open(d=>{for(const [path] of supplied)d.files.set(path,'format = 1\n');});await listed(page);
    assert.deepEqual(await page.locator('#ddsPresetList option').evaluateAll(xs=>xs.map(x=>[x.value,x.textContent])),supplied);
    assert.deepEqual(await page.locator('#ddsPresetList optgroup').evaluateAll(xs=>xs.map(x=>x.label)),['内置音色','我的音色']);
    assert.equal(device.loadedPath,undefined);assert.equal(device.ddsCalls.some(x=>/[?&]op=(load|save|write|default)/.test(x.path)),false);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.locator('#ddsPresetsPanel').screenshot({path:resolve(output,'phone-library.png')});
  });
  await check('load waits for completion, clears old held notes, and keeps global STOP available',async()=>{
    const {page,device}=await open();await listed(page);device.ddsDelay=650;
    await page.locator('[data-key="0"]').dispatchEvent('pointerdown',{pointerId:21,pointerType:'touch',button:0,bubbles:true});
    await until(()=>device.commands().some(x=>x.op===3&&x.a===1),'held On');
    await page.locator('#ddsLoad').click();await until(()=>device.ddsCalls.some(x=>x.path.includes('op=load')),'load submitted');
    assert.equal(await page.locator('#panic').isEnabled(),true);assert.equal(await page.locator('.piano-key:enabled').count(),0);
    assert.doesNotMatch(await page.locator('#ddsPresetStatus').textContent(),/已加载/);
    await done(page,'已加载');await until(()=>page.locator('.piano-key:enabled').count().then(n=>n===25),'new keys enabled');
    assert.equal(device.commands().filter(x=>x.op===3&&x.a).length,1);assert.equal(device.loadedPath,'Patches/Factory/Pads/Supersaw.toml');
    assert.equal(await page.locator('#ddsLoadedPreset').textContent(),'本次已加载：铺底 / 超级锯齿');
  });
  await check('save uses a Chinese User path and changes neither current timbre nor Factory',async()=>{
    const {page,device}=await open();await listed(page);await page.getByText('保存当前减法音色',{exact:true}).first().click();await page.locator('#ddsCategory').fill('我的音色');await page.locator('#ddsName').fill('柔和');
    await page.locator('#ddsSave').click();await done(page,'已保存');assert.ok(device.files.has('Patches/User/我的音色/柔和.toml'));assert.equal(device.loadedPath,undefined);
  });
  await check('normal owner context advance during loading rebinds without losing completion',async()=>{
    const {page,device}=await open();await listed(page);device.ddsDelay=750;
    await page.locator('#ddsLoad').click();await done(page,'已加载');
    await until(()=>page.locator('.piano-key:enabled').count().then(n=>n===25),'rebound after load');
    assert.equal(device.ddsCalls.filter(x=>x.path.includes('op=load')).length,1);assert.match(await page.locator('#connectionText').textContent(),/已连接/);
  });
  await check('fast load completion rebinds before sending the next empty bitmap',async()=>{
    const {page,device}=await open();await listed(page);device.ddsDelay=150;
    await page.locator('#ddsLoad').click();await done(page,'已加载');
    await until(()=>page.locator('.piano-key:enabled').count().then(n=>n===25),'keys after fast load');
    assert.equal(device.commandErrors.includes('stale_context'),false,'completed load must not send old-context Off and disconnect');
    const states=device.requests.filter(r=>r.path==='/api/v2/state').length;await wait(650);
    assert.ok(device.requests.filter(r=>r.path==='/api/v2/state').length>states,'background state polling resumes after fast load');
  });
  await check('STOP invalidates a pending load result and is sent before that job finishes',async()=>{
    const {page,device}=await open();await listed(page);device.ddsDelay=1000;await page.locator('#ddsLoad').click();
    await until(()=>device.ddsCalls.some(x=>x.path.includes('op=load')),'load accepted');await page.locator('#panic').click();
    await until(()=>device.commands().some(x=>x.op===4),'STOP passed');await done(page,'结果未知');
    assert.doesNotMatch(await page.locator('#ddsPresetStatus').textContent(),/已加载/);assert.equal(device.ddsCalls.filter(x=>x.path.includes('op=load')).length,1);
  });
  await check('Panic cancels an exclusive hung LOAD before explicit KS selection; late cleanup preserves new notes',async()=>{
    const {page,device}=await open();await listed(page);const context=device.state.context_epoch,panic=device.state.legacy.panic;device.holdDdsStatus=true;await page.locator('#ddsLoad').click();
    await until(()=>!!device.ddsStatusGate,'hung status');
    // The owner LOAD completes first; Panic retires only the page intent, not the server job.
    await until(()=>{device.time=performance.now();device.flush();return device.job.context_epoch!==context;},'owner LOAD completed before explicit Panic');
    assert.equal(device.job.context_epoch,device.state.context_epoch);assert.equal(device.state.legacy.panic,(panic+1)>>>0);
    assert.equal(await page.locator('#ks').isDisabled(),true,'exclusive LOAD keeps other mode choices fenced');
    assert.equal(await page.locator('#panic').isEnabled(),true,'explicit global STOP stays available');
    const before=device.commands().length;await page.locator('#panic').click();
    await until(()=>device.log.some(x=>x.stage==='owner'&&x.op===4),'STOP adopted');
    await until(()=>page.locator('#ks').isEnabled(),'empty post-STOP session ready');
    await page.locator('#ks').click();
    await until(()=>page.locator('.string-cell:enabled').count().then(n=>n>0),'explicit new KS intent active',1800);
    assert.equal(device.commands().slice(before).some(x=>x.op===5&&x.a===0),false,'cancelled LOAD must not automatically select DDS');
    await page.locator('#stringsLayout [data-layout="chromatic"]').click();
    await until(()=>page.locator('.string-cell:enabled').count().then(n=>n===25),'chromatic ready');
    await page.locator('[data-string-key="1"]').dispatchEvent('pointerdown',{pointerId:40,pointerType:'touch',button:0,bubbles:true});
    await until(()=>device.commands().some(x=>x.op===3&&x.a===2),'new KS note');device.ddsStatusGate();await done(page,'结果未知');
    await wait(350);assert.equal(device.commands().at(-1).a,2,'old completion must not release new KS input');assert.doesNotMatch(await page.locator('#ddsLoadedPreset').textContent(),/本次已加载/);
  });
  await check('import is explicit, export downloads TOML, and startup default is separate from loading',async()=>{
    const {page,device}=await open();await listed(page);await page.locator('#ddsImportCategory').fill('User');
    await page.locator('#ddsImportFile').setInputFiles({name:'Import.toml',mimeType:'text/plain',buffer:Buffer.from('format = 1\nname = "Imported"\n')});
    assert.equal(device.files.has('Patches/User/User/Import.toml'),false);await page.locator('#ddsImport').click();await done(page,'已导入');
    await page.locator('#ddsRefresh').click();await listed(page);await page.locator('#ddsPresetList').selectOption('Patches/User/User/Import.toml');
    const download=page.waitForEvent('download');await page.locator('#ddsExport').click();assert.match((await download).suggestedFilename(),/Import\.toml/);await done(page,'已导出');
    await page.getByText('开机音色设置',{exact:true}).click();await page.locator('#ddsDefault').click();await done(page,'开机音色');assert.equal(device.defaultPath,'Patches/User/User/Import.toml');assert.equal(device.loadedPath,undefined);
    await page.locator('#ddsDefaultReset').click();await done(page,'恢复');assert.equal(device.defaultPath,'');
  });
  await check('external import is visible, explicit, complete only after done, and ready for manual load',async()=>{
    const {page,device}=await open();await listed(page);device.ddsDelay=700;
    const text='format = 1\n# 外部完整参数\n[filter]\ncutoff = 1234.5\n';
    await page.locator('#ddsImportFile').setInputFiles({name:'温暖.TOML',mimeType:'text/plain',buffer:Buffer.from(text)});
    await until(()=>page.locator('#ddsImport').isEnabled(),'file ready');
    assert.equal(await page.locator('#ddsImportFile').isVisible(),true);assert.match(await page.locator('#ddsImportSelected').textContent(),/温暖.TOML/);
    assert.match(await page.locator('#ddsImportTarget').textContent(),/Patches\/User\/我的音色\/温暖.toml/);
    assert.equal(device.ddsCalls.some(x=>/[?&]op=(import|write|load)/.test(x.path)),false);
    await page.locator('#ddsImport').click();await until(()=>device.ddsCalls.some(x=>x.path.includes('op=import')),'accepted import');
    await page.locator('#ddsImport').dispatchEvent('click');assert.equal(await page.locator('#ddsImport').isDisabled(),true);
    assert.doesNotMatch(await page.locator('#ddsPresetStatus').textContent(),/已导入/);
    await done(page,'已导入');assert.equal(device.files.get('Patches/User/我的音色/温暖.toml'),text);
    assert.equal(device.loadedPath,undefined);assert.equal(device.ddsCalls.filter(x=>x.path.includes('op=import')).length,1);
    assert.equal(await page.locator('#ddsPresetList').inputValue(),'Patches/User/我的音色/温暖.toml');assert.equal(await page.locator('#ddsImportFile').inputValue(),'');
    assert.equal(await page.locator('#ddsImport').isDisabled(),true);
    await page.locator('#ddsPresetsPanel').screenshot({path:resolve(output,'phone-import-success.png')});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.locator('#ddsLoad').click();await done(page,'已加载');assert.equal(device.loadedPath,'Patches/User/我的音色/温暖.toml');
  });
  await check('same-name import preserves old file, cancels safely, and replaces only on explicit confirmation',async()=>{
    const path='Patches/User/我的音色/Existing.toml',original='format=1\n# original\n',replacement='format=1\n# replacement\n';
    const {page,device}=await open(d=>d.files.set(path,original));await listed(page);
    await page.locator('#ddsImportFile').setInputFiles({name:'Existing.toml',mimeType:'text/plain',buffer:Buffer.from(replacement)});
    await until(()=>page.locator('#ddsImport').isEnabled(),'ready');await page.locator('#ddsImport').click();await done(page,'同名');
    assert.equal(device.files.get(path),original);assert.equal(device.ddsCalls.some(x=>x.path.includes('op=write')),false);
    await page.locator('#ddsImportConflict').screenshot({path:resolve(output,'phone-replace-confirmation.png')});
    await page.locator('#ddsImportCancel').click();assert.equal(await page.locator('#ddsImportConflict').isHidden(),true);assert.equal(device.files.get(path),original);
    await page.locator('#ddsImport').click();await done(page,'同名');await page.locator('#ddsImportName').fill('Different');assert.equal(await page.locator('#ddsImportConflict').isHidden(),true);
    await page.locator('#ddsImportName').fill('Existing');await page.locator('#ddsImport').click();await done(page,'同名');
    await page.locator('#ddsImportReplace').click();await done(page,'已导入');assert.equal(device.files.get(path),replacement);
    assert.equal(device.ddsCalls.filter(x=>x.path.includes('op=write')).length,1);assert.equal(device.loadedPath,undefined);
  });
  await check('bad local files and invalid paths never upload; 16 KiB boundary imports',async()=>{
    const {page,device}=await open();await listed(page);
    for(const [name,buffer] of [['Bad.sf2',Buffer.from('format=1')],['Empty.toml',Buffer.alloc(0)],['Large.toml',Buffer.alloc(16385,120)],['Utf8.toml',Buffer.from([0xc3,0x28])],['Nul.toml',Buffer.from('format=1\0')],['NoLF.toml',Buffer.alloc(16384,120)]]){
      await page.locator('#ddsImportFile').setInputFiles({name,mimeType:'text/plain',buffer});await until(async()=>!(/正在读取/.test(await page.locator('#ddsImportSelected').textContent())),'invalid file checked');assert.equal(await page.locator('#ddsImport').isDisabled(),true);
    }
    assert.equal(device.ddsCalls.some(x=>/[?&]op=(import|write)/.test(x.path)),false);
    const buffer=Buffer.from('format = 1\n#'+'x'.repeat(16371)+'\n');await page.locator('#ddsImportFile').setInputFiles({name:'Full.toml',mimeType:'text/plain',buffer});await until(()=>page.locator('#ddsImport').isEnabled(),'boundary ready');
    await page.locator('#ddsImportCategory').fill('../Factory');assert.equal(await page.locator('#ddsImport').isDisabled(),true);
    await page.locator('#ddsImportCategory').fill('中'.repeat(22));assert.equal(await page.locator('#ddsImport').isDisabled(),true);
    await page.locator('#ddsImportCategory').fill('我的音色');await page.locator('#ddsImport').click();await done(page,'已导入');assert.equal(Buffer.byteLength(device.files.get('Patches/User/我的音色/Full.toml')),16384);
  });
  await check('a newer file selection wins when an older read finishes late; clearing removes stale content',async()=>{
    const {page,device}=await open();await listed(page);
    await page.evaluate(()=>{const original=File.prototype.arrayBuffer;File.prototype.arrayBuffer=async function(){if(this.name==='Slow.toml')await new Promise(r=>setTimeout(r,500));return original.call(this);};});
    await page.locator('#ddsImportFile').setInputFiles({name:'Slow.toml',mimeType:'text/plain',buffer:Buffer.from('format=1\n# old\n')});
    await page.locator('#ddsImportFile').setInputFiles({name:'Latest.toml',mimeType:'text/plain',buffer:Buffer.from('format=1\n# latest\n')});await until(()=>page.locator('#ddsImport').isEnabled(),'latest ready');await wait(600);
    assert.equal(await page.locator('#ddsImportName').inputValue(),'Latest');await page.locator('#ddsImport').click();await done(page,'已导入');assert.equal(device.files.get('Patches/User/我的音色/Latest.toml'),'format=1\n# latest\n');
    await page.locator('#ddsImportFile').setInputFiles({name:'Slow.toml',mimeType:'text/plain',buffer:Buffer.from('format=1\n')});await page.locator('#ddsImportFile').setInputFiles([]);await wait(600);assert.equal(await page.locator('#ddsImport').isDisabled(),true);assert.equal(await page.locator('#ddsImportSelected').textContent(),'尚未选择文件。');
  });
  await check('busy and invalid TOML failures retain file for deliberate correction without automatic retry',async()=>{
    const {page,device}=await open();await listed(page);await page.locator('#ddsImportFile').setInputFiles({name:'Retry.toml',mimeType:'text/plain',buffer:Buffer.from('format=1\n')});await until(()=>page.locator('#ddsImport').isEnabled(),'ready');
    device.forceBusy=true;await page.locator('#ddsImport').click();await done(page,'另一项');assert.equal(await page.locator('#ddsImport').isEnabled(),true);await wait(350);assert.equal(device.ddsCalls.filter(x=>x.path.includes('op=import')).length,1);
    device.forceBusy=false;device.failDds=true;device.failDdsError='TOML: invalid table';await page.locator('#ddsImport').click();await done(page,'TOML');assert.equal(device.loadedPath,undefined);assert.equal(device.files.has('Patches/User/我的音色/Retry.toml'),false);
    device.failDds=false;await page.locator('#ddsImport').click();await done(page,'已导入');
  });
  await check('lost import acknowledgement is unknown, never retried, and never offered as successful',async()=>{
    const {page,device}=await open();await listed(page);await page.locator('#ddsImportFile').setInputFiles({name:'Unknown.toml',mimeType:'text/plain',buffer:Buffer.from('format=1\n')});await until(()=>page.locator('#ddsImport').isEnabled(),'ready');device.dropImportAck=true;
    await page.locator('#ddsImport').click();await done(page,'尚未确认');assert.match(await page.locator('#ddsPresetStatus').textContent(),/不会自动重试/);await wait(600);
    assert.equal(device.ddsCalls.filter(x=>x.path.includes('op=import')).length,1);assert.equal(device.loadedPath,undefined);assert.doesNotMatch(await page.locator('#ddsPresetStatus').textContent(),/已导入/);
  });
  await check('slow import keeps 250 ms heartbeats, Off and STOP active; engine change invalidates the pending result',async()=>{
    const {page,device}=await open();await listed(page);await page.locator('#ddsImportFile').setInputFiles({name:'SlowImport.toml',mimeType:'text/plain',buffer:Buffer.from('format=1\n')});await until(()=>page.locator('#ddsImport').isEnabled(),'ready');device.ddsDelay=1500;
    await page.locator('#ddsImport').click();await until(()=>device.ddsCalls.some(x=>x.path.includes('op=import')),'import accepted');
    await page.locator('[data-key="0"]').dispatchEvent('pointerdown',{pointerId:71,pointerType:'touch',button:0,bubbles:true});await until(()=>device.commands().some(x=>x.op===3&&x.a===1),'On');
    const count=device.commands().filter(x=>x.op===3&&x.a===1).length;await until(()=>device.commands().filter(x=>x.op===3&&x.a===1).length>count,'heartbeat',1000);
    await page.locator('[data-key="0"]').dispatchEvent('pointerup',{pointerId:71,pointerType:'touch',button:0,bubbles:true});await until(()=>device.commands().at(-1).a===0,'Off');await page.locator('#panic').click();await until(()=>device.commands().some(x=>x.op===4),'STOP');
    await done(page,'结果未知');assert.doesNotMatch(await page.locator('#ddsPresetStatus').textContent(),/已导入/);assert.equal(device.ddsCalls.filter(x=>x.path.includes('op=import')).length,1);
    await page.locator('#ks').click();await until(async()=>/KS/.test(await page.locator('#voiceTag').textContent()),'KS switch');assert.match(await page.locator('#voiceTag').textContent(),/16/);
    await page.locator('#fm').click();await until(async()=>/FM/.test(await page.locator('#voiceTag').textContent()),'FM switch');
  });
  await check('replacement confirmation is invalidated by STOP before an overwriting request',async()=>{
    const path='Patches/User/我的音色/Protected.toml',original='format=1\n# old\n';
    const {page,device}=await open(d=>d.files.set(path,original),true,1440);await listed(page);
    await page.locator('#ddsImportFile').setInputFiles({name:'Protected.toml',mimeType:'text/plain',buffer:Buffer.from('format=1\n# new\n')});await until(()=>page.locator('#ddsImport').isEnabled(),'ready');
    await page.locator('#ddsImport').click();await done(page,'同名');await page.locator('#ddsPresetsPanel').screenshot({path:resolve(output,'desktop-import-confirmation.png')});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.locator('#panic').click();await until(()=>page.locator('#ddsImportConflict').isHidden(),'confirmation invalidated');
    await page.locator('#ddsImportReplace').dispatchEvent('click');assert.equal(device.ddsCalls.some(x=>x.path.includes('op=write')),false);assert.equal(device.files.get(path),original);
  });
  await check('mode switch invalidates an import while its response is pending without blocking the new engine',async()=>{
    const {page,device}=await open();await listed(page);
    await page.locator('#ddsImportFile').setInputFiles({name:'Pending.toml',mimeType:'text/plain',buffer:Buffer.from('format=1\n')});await until(()=>page.locator('#ddsImport').isEnabled(),'ready');
    device.holdDdsStatus=true;await page.locator('#ddsImport').click();await until(()=>!!device.ddsStatusGate,'import pending');
    await page.locator('#ks').click();await until(()=>page.locator('.string-cell:enabled').count().then(n=>n>0),'KS active',1800);
    await page.locator('#stringsLayout [data-layout="chromatic"]').click();
    await until(()=>page.locator('.string-cell:enabled').count().then(n=>n===25),'chromatic ready');
    await page.locator('[data-string-key="1"]').dispatchEvent('pointerdown',{pointerId:91,pointerType:'touch',button:0,bubbles:true});await until(()=>device.commands().some(x=>x.op===3&&x.a===2),'KS note');device.ddsStatusGate();await done(page,'结果未知');
    assert.doesNotMatch(await page.locator('#ddsPresetStatus').textContent(),/已导入/);assert.equal(device.loadedPath,undefined);assert.equal(device.ddsCalls.filter(x=>x.path.includes('op=import')).length,1);
  });
  await check('a slow list never blocks key heartbeat, Off, or STOP',async()=>{
    const {page,device}=await open();await listed(page);device.ddsDelay=900;await page.locator('#ddsRefresh').click();
    await page.locator('[data-key="0"]').dispatchEvent('pointerdown',{pointerId:23,pointerType:'touch',button:0,bubbles:true});
    await until(()=>device.commands().some(x=>x.op===3&&x.a===1),'On during query');const count=device.commands().filter(x=>x.op===3&&x.a===1).length;
    await until(()=>device.commands().filter(x=>x.op===3&&x.a===1).length>count,'heartbeat during query');
    await page.locator('[data-key="0"]').dispatchEvent('pointerup',{pointerId:23,pointerType:'touch',button:0,bubbles:true});await until(()=>device.commands().at(-1).a===0,'Off during query');
    await page.locator('#panic').click();await until(()=>device.commands().some(x=>x.op===4),'STOP during query');assert.match(await page.locator('#connectionText').textContent(),/已连接/);
  });
  await check('missing endpoint affects only the library; no storage still permits Factory loading',async()=>{
    let view=await open(d=>d.ddsMissing=true);await done(view.page,'尚未提供');assert.equal(await view.page.locator('.piano-key:enabled').count(),25);
    view=await open(d=>d.mounted=false);await listed(view.page);assert.equal(await view.page.locator('#ddsSave').isDisabled(),true);assert.equal(await view.page.locator('#ddsLoad').isEnabled(),true);
  });
  await check('job failure fences input without success and permits deliberate empty reconnection',async()=>{
    const {page,device}=await open();await listed(page);device.failDds=true;await page.locator('#ddsLoad').click();await done(page,'失败');
    assert.doesNotMatch(await page.locator('#ddsPresetStatus').textContent(),/已加载/);assert.equal(device.loadedPath,undefined);
    assert.equal(await page.locator('.piano-key:enabled').count(),0,'failed LOAD keeps performance fenced');
    assert.equal(await page.locator('#reconnect').isEnabled(),true,'explicit recovery is available');
    const before=device.commands().length;await page.locator('#reconnect').click();await until(()=>page.locator('.piano-key:enabled').count().then(n=>n===25),'deliberate empty reconnect');
    assert.ok(device.commands().slice(before).every(x=>x.op===3&&x.a===0),'explicit recovery cannot replay LOAD or held keys');
  });
  assert.deepEqual(errors,[]);
}finally{
  if(live){await live.context.close();await live.close();}
  await writeFile(resolve(output,'report.json'),JSON.stringify({browser:browser.version(),htmlBytes:html.length,htmlSHA256:createHash('sha256').update(html).digest('hex'),physical_device_verified:false,checks,errors},null,2)+'\n');await browser.close();await tempDirectory.close();
}
