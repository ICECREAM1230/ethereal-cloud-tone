import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {startV2Server} from '../mock/v2-server.mjs';
import {V2Device} from '../mock/v2-device.mjs';
const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'artifacts/raw-midi-browser');await mkdir(output,{recursive:true});
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const results=[],errors=[];let live;
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(fn,label,timeout=7000){const start=Date.now();while(!await fn()){if(Date.now()-start>timeout)throw Error('timeout: '+label);await wait(25);}}
const raw=d=>d.log.filter(e=>e.stage==='midi-queued');
async function open(recovering=false,width=390){
  if(live){await live.context.close();await live.close();}
  const d=new V2Device();d.state.mode={requested:0,applied:0,switching:recovering,recovering};d.state.legacy.ready=!recovering;
  Object.assign(d.state.legacy.engine,{requested:0,applied:0,switching:recovering,recovering});
  const server=await startV2Server({device:d});const context=await browser.newContext({viewport:{width,height:844},hasTouch:true,isMobile:true,serviceWorkers:'block'});
  const page=await context.newPage(),requests=[];live={...server,context,page,requests};
  page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(r.url().endsWith('/api/v1/midi/raw'))requests.push({body:JSON.parse(r.postData()),headers:r.headers()});});
  await page.goto(server.url);await until(async()=>/已连接/.test(await page.locator('#connectionText').textContent()),'session');
  assert.equal(await page.locator('#bend').count(),1,'page must bind a real raw MIDI performance control');
  return live;
}
async function input(page,value,event='input'){await page.locator('#bend').evaluate((el,{value,event})=>{el.value=String(value);el.dispatchEvent(new Event(event,{bubbles:true}));},{value,event});}
async function check(name,run){try{await run();results.push({name,result:'PASS'});console.log('PASS '+name);}catch(e){results.push({name,result:'FAIL',error:e.message});console.error('FAIL '+name+': '+e.message);}}
try {
  await check('page sends no raw defaults and keyboard retains its leased snapshot route',async()=>{
    const {page,device}=await open();assert.equal(raw(device).length,0);assert.equal(await page.locator('#bend').isDisabled(),false);
    await page.locator('[data-key="0"]').dispatchEvent('pointerdown',{pointerId:7,button:0,pointerType:'touch',bubbles:true});
    await until(()=>device.log.some(e=>e.stage==='received'&&e.op===3&&e.a===1),'snapshot');
    assert.equal(raw(device).length,0);await page.locator('[data-key="0"]').dispatchEvent('pointerup',{pointerId:7,button:0,pointerType:'touch',bubbles:true});
    await page.locator('#perform').screenshot({path:resolve(output,'dds-performance-390.png')});
  });
  await check('bend sends current-bound raw bytes and recent receipt only means queued',async()=>{
    const {page,device,requests}=await open();await input(page,16383);await until(()=>raw(device).length===1,'bend');
    assert.deepEqual(raw(device)[0].bytes,[224,127,127]);
    const b=requests[0].body;assert.equal(b.source,'web');assert.ok(b.token>0&&b.sequence>0&&b.context_epoch>0);assert.equal(requests[0].headers['content-type'],'application/json');
    await until(async()=>/queued/.test(await page.locator('#midiInfo').textContent()),'queued receipt');
    assert.doesNotMatch(await page.locator('#midiInfo').textContent(),/completed|已采用|已发声/);
    await input(page,16383,'change');await until(()=>raw(device).length===2,'center');assert.deepEqual(raw(device)[1].bytes,[224,0,64]);
  });
  await check('recovering page rejects both visible and dispatched raw control intent',async()=>{
    const {page,device}=await open(true);assert.equal(await page.locator('#bend').isDisabled(),true);await input(page,16383);await wait(100);assert.equal(raw(device).length,0);
  });
  await check('raw 503 is explicit and reconnect never repeats the old bend',async()=>{
    const {page,device,requests}=await open();device.rawStatus=503;await input(page,16383);
    await until(async()=>/控制连接已释放/.test(await page.locator('#feedback').textContent()),'queue full');assert.match(await page.locator('#feedback').textContent(),/队列已满/);
    device.rawStatus=0;await page.locator('#reconnect').click();await until(async()=>!await page.locator('[data-key="0"]').isDisabled(),'reconnect');assert.equal(requests.length,1);assert.equal(await page.locator('#bend').inputValue(),'8192');
  });
  await check('Panic fences a delayed raw receipt and discards queued raw intent',async()=>{
    const {page,device,requests}=await open();device.rawReceiptDelay=350;await input(page,9000);await until(()=>raw(device).length===1,'inflight');await input(page,10000);
    await page.locator('#panic').click();await until(async()=>!await page.locator('[data-key="0"]').isDisabled(),'Panic rebind');await wait(400);
    assert.equal(requests.length,1);assert.equal(await page.locator('#midiInfo').textContent(),'尚无 MIDI 排队回执');assert.equal(await page.locator('#bend').inputValue(),'8192');
  });
  await check('desktop layout keeps existing controls and introduces no document overflow',async()=>{
    const {page}=await open(false,1440);assert.equal(await page.locator('#cc7').count(),1);assert.equal(await page.locator('#cc10').count(),1);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await page.locator('#perform').screenshot({path:resolve(output,'dds-performance-1440.png')});
  });
  await check('application has no uncaught browser errors',async()=>assert.deepEqual(errors,[]));
} finally {
  if(live){await live.context.close();await live.close();}
  await writeFile(resolve(output,'raw-midi-browser-report.json'),JSON.stringify({browser:browser.version(),physical_device_verified:false,results,errors},null,2)+'\n');await browser.close();
}
if(results.some(r=>r.result!=='PASS'))process.exitCode=1;
