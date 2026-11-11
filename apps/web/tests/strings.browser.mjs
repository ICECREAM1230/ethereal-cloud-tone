import assert from 'node:assert/strict';
import {mkdir,writeFile,readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {UiDevice} from './helpers/ui-device.mjs';
import {startUiPreview} from '../scripts/preview-ui.mjs';
const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'artifacts/after');await mkdir(output,{recursive:true});
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const checks=[],errors=[],screenshots=[],observations=[];let live;
const htmlHash=async()=>createHash('sha256').update(await readFile(new URL('../dist/index.html',import.meta.url))).digest('hex');
const html_sha256=await htmlHash();
const wait=ms=>new Promise(r=>setTimeout(r,ms));
const received=d=>d.log.filter(e=>e.stage==='received');
const keyFrames=d=>received(d).filter(e=>e.op===3);
const mask=d=>d.snapshot().clients.reduce((m,c)=>m|c.keys,0);
async function until(fn,label,timeout=7000){const start=performance.now();while(!await fn()){if(performance.now()-start>timeout)throw Error('timeout: '+label);await wait(20);}}
async function open({width=390,height=844,mode=1}={}){
 if(live){await live.context.close();await live.close();}
 const device=new UiDevice({mode}),server=await startUiPreview({device});
 const context=await browser.newContext({viewport:{width,height},hasTouch:true,isMobile:width<700,serviceWorkers:'block'});
 const page=await context.newPage();page.on('pageerror',error=>errors.push(error.message));
 page.on('request',request=>{const url=new URL(request.url());assert.equal(url.hostname,'127.0.0.1','all requests stay on loopback');});
 await page.goto(server.url);live={...server,context,page};
 await playable(page,mode);
 return live;
}
async function playable(page,mode=1){await until(()=>page.locator(mode===1?'.string-cell:enabled':'.piano-key:enabled').count().then(n=>n>0),'playable mode '+mode);}
const cell=(page,row=5,column=0)=>page.locator(`.string-cell[data-string-row="${row}"][data-string-column="${column}"]`);
async function point(locator,id=1){const box=await locator.boundingBox();assert(box,'cell has bounds');return {id,x:box.x+box.width/2,y:box.y+box.height/2};}
async function touch(cdp,type,points){await cdp.send('Input.dispatchTouchEvent',{type,touchPoints:points});}
async function synthetic(locator,type,id=81,extra={}){await locator.dispatchEvent(type,{pointerId:id,pointerType:'touch',button:0,buttons:type==='pointerup'?0:1,bubbles:true,...extra});}
async function assertCleared(page,device){await until(()=>mask(device)===0,'device empty');await until(()=>page.locator('.string-cell[aria-pressed="true"]').count().then(n=>n===0),'local contacts clear after observed context');}
async function check(name,eventKind,run){if(process.env.STRINGS_BROWSER_FILTER&&!name.includes(process.env.STRINGS_BROWSER_FILTER))return;try{await run();checks.push({name,event_kind:eventKind,result:'PASS'});console.log('PASS '+name);}catch(error){checks.push({name,event_kind:eventKind,result:'FAIL',error:error.stack});console.error('FAIL '+name+': '+error.stack);}}
try{
 await check('KS uses strings; piano is hidden and disabled','DOM',async()=>{const {page}=await open();assert.equal(await page.locator('#stringsPanel').isVisible(),true);assert.equal(await page.locator('#pianoWindow').isVisible(),false);assert.equal(await page.locator('.piano-key:enabled').count(),0);assert.equal(await page.locator('.string-cell').count(),36);});
 await check('real two-finger different-note hold, independent release and no stuck notes','trusted CDP touch',async()=>{
  const {page,device,context}=await open();await page.locator('#strings').scrollIntoViewIfNeeded();const cdp=await context.newCDPSession(page),a=cell(page,5,0),b=cell(page,4,0),p1=await point(a,1),p2=await point(b,2);
  const first=1<<Number(await a.getAttribute('data-string-key')),second=1<<Number(await b.getAttribute('data-string-key'));
  await touch(cdp,'touchStart',[p1,p2]);await until(()=>mask(device)===(first|second),'two held');
  await touch(cdp,'touchEnd',[p1]);await until(()=>mask(device)===second,'only second remains');
  await touch(cdp,'touchEnd',[]);await assertCleared(page,device);await cdp.detach();
 });
 await check('real same-note fingers preserve held bit until final release','trusted CDP touch',async()=>{
  const {page,device,context}=await open();await page.locator('#strings').scrollIntoViewIfNeeded();const cdp=await context.newCDPSession(page),a=cell(page,5,0),p1=await point(a,1),p2={...p1,id:2,x:p1.x+5};const expected=1<<Number(await a.getAttribute('data-string-key'));
  await touch(cdp,'touchStart',[p1,p2]);await until(()=>mask(device)===expected,'same held');await touch(cdp,'touchEnd',[p1]);await wait(80);assert.equal(mask(device),expected);await touch(cdp,'touchEnd',[]);await assertCleared(page,device);await cdp.detach();
 });
 await check('real same-note repluck preserves Off then On boundary','trusted CDP touch',async()=>{
  const {page,device,context}=await open();await page.locator('#strings').scrollIntoViewIfNeeded();const cdp=await context.newCDPSession(page),p=await point(cell(page),1),before=keyFrames(device).length;
  for(let i=0;i<2;i++){await touch(cdp,'touchStart',[p]);await until(()=>mask(device)===1,'On');await touch(cdp,'touchEnd',[]);await until(()=>mask(device)===0,'Off');}
  assert.deepEqual(keyFrames(device).slice(before).map(e=>e.a),[1,0,1,0]);await cdp.detach();
 });
 await check('real cross-string drag preserves the other finger','trusted CDP touch',async()=>{
  const {page,device,context}=await open();await page.locator('#strings').scrollIntoViewIfNeeded();const cdp=await context.newCDPSession(page),p1=await point(cell(page,5,0),1),p2=await point(cell(page,4,1),2),next=await point(cell(page,4,0),1),other=1<<Number(await cell(page,4,1).getAttribute('data-string-key'));
  await touch(cdp,'touchStart',[p1,p2]);await until(()=>mask(device)===(1|other),'two held');await touch(cdp,'touchMove',[next,p2]);await until(()=>mask(device)===((1<<4)|other),'drag preserves other');await touch(cdp,'touchEnd',[]);await assertCleared(page,device);await cdp.detach();
 });
 await check('fast real sweep visits all crossed strings in order','trusted CDP touch',async()=>{
  const {page,device,context}=await open();await page.locator('#strings').scrollIntoViewIfNeeded();const cdp=await context.newCDPSession(page),start=await point(cell(page,5,0),1),end=await point(cell(page,0,0),1),before=keyFrames(device).length;
  await touch(cdp,'touchStart',[start]);await until(()=>mask(device)===1,'start low');await touch(cdp,'touchMove',[end]);await until(()=>mask(device)===(1<<19),'end high');await touch(cdp,'touchEnd',[]);await assertCleared(page,device);
  const nonzero=keyFrames(device).slice(before).filter(e=>e.a).map(e=>e.a);assert.deepEqual(nonzero,[0,4,7,12,16,19].map(k=>1<<k));observations.push({name:'fast sweep',frames:nonzero});await cdp.detach();
 });
 await check('real diagonal crossing only plucks the entered cells','trusted CDP touch',async()=>{
  const {page,device,context}=await open();await page.locator('#strings').scrollIntoViewIfNeeded();const cdp=await context.newCDPSession(page),source=await cell(page,5,0).boundingBox(),target=await cell(page,4,1).boundingBox(),corner={x:target.x,y:source.y},start={id:1,x:corner.x-20,y:corner.y+20},end={id:1,x:corner.x+20,y:corner.y-20},expected=1<<Number(await cell(page,4,1).getAttribute('data-string-key')),before=keyFrames(device).length;
  await touch(cdp,'touchStart',[start]);await until(()=>mask(device)===1,'start low');await touch(cdp,'touchMove',[end]);await until(()=>mask(device)===expected,'diagonal destination');await touch(cdp,'touchEnd',[]);await assertCleared(page,device);const frames=keyFrames(device).slice(before).map(e=>e.a);assert.deepEqual(frames,[1,expected,0]);observations.push({name:'diagonal',frames});await cdp.detach();
 });
 await check('trusted touch cancellation clears held contact','trusted CDP touchCancel',async()=>{
  const {page,device,context}=await open();await page.locator('#strings').scrollIntoViewIfNeeded();const cdp=await context.newCDPSession(page),p=await point(cell(page),1);await touch(cdp,'touchStart',[p]);await until(()=>mask(device)===1,'On');await touch(cdp,'touchCancel',[]);await assertCleared(page,device);await cdp.detach();
 });
 await check('trusted drag outside strings releases and return plucks again','trusted CDP touch',async()=>{
  const {page,device,context}=await open();await page.locator('#strings').scrollIntoViewIfNeeded();const cdp=await context.newCDPSession(page),p=await point(cell(page),1),box=await page.locator('#strings').boundingBox(),outside={...p,x:box.x-4};await touch(cdp,'touchStart',[p]);await until(()=>mask(device)===1,'On');await touch(cdp,'touchMove',[outside]);await until(()=>mask(device)===0,'leave Off');await touch(cdp,'touchMove',[p]);await until(()=>mask(device)===1,'return On');await touch(cdp,'touchEnd',[]);await assertCleared(page,device);await cdp.detach();
 });
 await check('pointercancel and lost capture release one contact once','synthetic PointerEvent',async()=>{
  for(const event of ['pointercancel','lostpointercapture']){const {page,device}=await open(),a=cell(page);await synthetic(a,'pointerdown',71);await until(()=>mask(device)===1,'On');await synthetic(a,event,71);await assertCleared(page,device);const count=keyFrames(device).length;await synthetic(a,'pointerup',71);await wait(80);assert.equal(keyFrames(device).length,count);}
 });
 await check('Space/Enter and focus loss release keyboard contact','trusted keyboard / DOM focus',async()=>{
  const {page,device}=await open();for(const key of ['Space','Enter']){const a=cell(page);await a.focus();await page.keyboard.down(key);await until(()=>mask(device)===1,key+' On');await page.keyboard.up(key);await assertCleared(page,device);}
  await cell(page).focus();await page.keyboard.down('Space');await until(()=>mask(device)===1,'focused On');await page.locator('#stringsLayout button[data-layout="chords"]').focus();await assertCleared(page,device);await page.keyboard.up('Space');
 });
 await check('whole chromatic range preserves C3 and C5 bits and keyboard navigation','trusted keyboard / DOM',async()=>{
  const {page,device}=await open();await page.locator('#stringsLayout button[data-layout="chromatic"]').click();assert.equal(await page.locator('#stringsLayout button.selected').getAttribute('data-layout'),'chromatic');assert.equal(await page.locator('#stringsLayout button[aria-pressed="true"]').getAttribute('data-layout'),'chromatic');assert.equal(await page.locator('.string-cell').count(),25);assert.deepEqual(await page.locator('.string-cell').evaluateAll(nodes=>nodes.map(n=>Number(n.dataset.stringKey)).sort((a,b)=>a-b)),Array.from({length:25},(_,i)=>i));
  for(const k of [0,24]){const a=page.locator(`.string-cell[data-string-key="${k}"]`);await a.focus();await page.keyboard.down('Space');await until(()=>mask(device)===(1<<k),'extreme '+k);await page.keyboard.up('Space');await assertCleared(page,device);}
  const a=page.locator('.string-cell').first();await a.focus();await page.keyboard.press('ArrowRight');assert.notEqual(await page.evaluate(()=>document.activeElement.dataset.stringKey),await a.getAttribute('data-string-key'));
 });
 await check('held strings renew lease with existing heartbeat after 1.2 seconds','trusted CDP touch',async()=>{
  const {page,device,context}=await open();await page.locator('#strings').scrollIntoViewIfNeeded();const cdp=await context.newCDPSession(page),p=await point(cell(page),1);await touch(cdp,'touchStart',[p]);await until(()=>mask(device)===1,'On');await wait(1550);assert.equal(mask(device),1);const ons=keyFrames(device).filter(e=>e.a===1);assert(ons.length>=5);const gaps=ons.slice(1).map((e,i)=>e.at-ons[i].at);assert(gaps.every(n=>n<500),'heartbeat gaps '+gaps);observations.push({name:'held heartbeat',gaps_ms:gaps,ons:ons.length});await touch(cdp,'touchEnd',[]);await assertCleared(page,device);await cdp.detach();
 });
 await check('slow reverb job leaves KS heartbeat, release and STOP responsive','trusted CDP touch / synthetic range input / trusted STOP click',async()=>{
  const {page,device,context}=await open();device.reverbDelay=2500;await page.locator('#strings').scrollIntoViewIfNeeded();const cdp=await context.newCDPSession(page),p=await point(cell(page),1);await touch(cdp,'touchStart',[p]);await until(()=>mask(device)===1,'On');
  await until(()=>page.locator('#reverbWet').isEnabled(),'reverb writable');await page.locator('#reverbWet').evaluate(el=>{el.value=16000;el.dispatchEvent(new Event('input',{bubbles:true}));});await until(()=>device.effect.pending,'effect pending');await wait(1500);assert.equal(mask(device),1);assert(keyFrames(device).filter(e=>e.a===1).length>=5);
  const releaseAt=performance.now();await touch(cdp,'touchEnd',[]);await assertCleared(page,device);const release_ms=performance.now()-releaseAt;assert(release_ms<700,'Off stays responsive');assert.equal(device.effect.pending,true);
  await touch(cdp,'touchStart',[p]);await until(()=>mask(device)===1,'fresh On');const stopAt=performance.now();await page.locator('#panic').click();await assertCleared(page,device);const stop_ms=performance.now()-stopAt;assert(stop_ms<700,'STOP stays responsive');observations.push({name:'slow reverb',delay_ms:device.reverbDelay,release_ms,stop_ms});await touch(cdp,'touchEnd',[]);await cdp.detach();
 });
 for(const action of ['STOP','engine switch','offline','HTTP disconnect','window blur','page hidden','context change','layout switch'])await check(action+' clears held contacts and old moves never replay',action==='HTTP disconnect'?'real loopback socket disconnect / synthetic PointerEvent':'synthetic boundary + PointerEvent; real button click for STOP/mode/layout',async()=>{
  const {page,device}=await open(),a=cell(page),bounds=await a.boundingBox();await synthetic(a,'pointerdown',91);await until(()=>mask(device)===1,'held On');
  if(action==='STOP')await page.locator('#panic').click();
  if(action==='engine switch')await page.locator('#dds').click();
  if(action==='offline')await page.evaluate(()=>window.dispatchEvent(new Event('offline')));
  if(action==='HTTP disconnect')device.offline=true;
  if(action==='window blur')await page.evaluate(()=>window.dispatchEvent(new Event('blur')));
  if(action==='page hidden')await page.evaluate(()=>{Object.defineProperty(document,'hidden',{configurable:true,get:()=>true});document.dispatchEvent(new Event('visibilitychange'));});
  if(action==='context change')device.moveContext();
  if(action==='layout switch')await page.locator('#stringsLayout button[data-layout="chromatic"]').click();
  await assertCleared(page,device);
  if(action==='engine switch'){await playable(page,0);await page.locator('#ks').click();}
  if(action==='window blur')await page.evaluate(()=>window.dispatchEvent(new Event('focus')));
  if(action==='page hidden')await page.evaluate(()=>{Object.defineProperty(document,'hidden',{configurable:true,get:()=>false});document.dispatchEvent(new Event('visibilitychange'));});
  if(action==='offline')await page.evaluate(()=>window.dispatchEvent(new Event('online')));
  if(action==='HTTP disconnect'){device.offline=false;await page.evaluate(()=>window.dispatchEvent(new Event('online')));}
  await playable(page);const before=keyFrames(device).filter(e=>e.a).length;
  await page.locator('#strings').dispatchEvent('pointermove',{pointerId:91,pointerType:'touch',button:0,buttons:1,bubbles:true,clientX:bounds.x+bounds.width/2,clientY:bounds.y+bounds.height/2});await page.locator('#strings').dispatchEvent('pointerup',{pointerId:91,pointerType:'touch',bubbles:true});await wait(500);
  assert.equal(mask(device),0);assert.equal(keyFrames(device).filter(e=>e.a).length,before);assert.equal(await page.locator('.string-cell[aria-pressed="true"]').count(),0);
 });
 for(const [label,width,height] of [['phone',390,844],['tablet',1024,768],['desktop',1440,900],['narrow',320,740],['portrait',768,1024]])await check(label+' responsive strings and screenshot','layout / real Chromium rendering',async()=>{
  const {page}=await open({width,height});await page.evaluate(()=>window.scrollTo(0,0));
  const metrics=await page.evaluate(()=>({scrollWidth:document.documentElement.scrollWidth,innerWidth,strings:[...document.querySelectorAll('.string-cell')].map(n=>{const b=n.getBoundingClientRect();return {width:b.width,height:b.height};})}));assert(metrics.scrollWidth<=metrics.innerWidth,'horizontal overflow');assert(metrics.strings.every(b=>b.width>=44&&b.height>=44),'string targets >=44px: '+JSON.stringify(metrics.strings[0]));
  for(const [kind,fullPage] of [['viewport',false],['full',true]]){const path=resolve(output,`${label}-${kind}.png`);await page.screenshot({path,fullPage});screenshots.push({label,kind,path,width,height,metrics});}const path=resolve(output,`${label}-instrument.png`);await page.locator('#perform').screenshot({path});screenshots.push({label,kind:'instrument',path,width,height});
 });
 for(const [label,mode] of [['desktop-dds',0],['desktop-fm',2]])await check(label+' remains fully available','DOM / real Chromium rendering',async()=>{
  const {page}=await open({width:1440,height:900,mode:mode===2?0:mode});if(mode===2){await page.locator('#fm').click();await playable(page,2);assert.equal(await page.locator('#fmSound').isVisible(),true);}else{await until(()=>page.locator('#ddsLoad').isEnabled(),'library ready');assert.equal(await page.locator('#ddsPanels').isVisible(),true);}assert.equal(await page.locator('#stringsPanel').isVisible(),false);assert.equal(await page.locator('.piano-key:enabled').count(),25);assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await page.screenshot({path:resolve(output,label+'-full.png'),fullPage:true});await page.screenshot({path:resolve(output,label+'-viewport.png')});
 });
 await check('local preview supports independent reverb and DDS import replacement flow','trusted browser controls / mock jobs',async()=>{
  const {page,device}=await open({mode:0});await until(()=>page.locator('#ddsLoad').isEnabled(),'library');
  await until(()=>page.locator('#reverbWet').isEnabled(),'reverb writable');await page.locator('#reverbWet').evaluate(el=>{el.value=12000;el.dispatchEvent(new Event('input',{bubbles:true}));});await until(()=>device.effect.wet_q15===12000,'reverb adopted');
  await page.locator('#ddsImportFile').setInputFiles({name:'测试.toml',mimeType:'text/plain',buffer:Buffer.from('format = 1\nname = "demo"\n')});await page.locator('#ddsImport').click();await until(()=>device.files.has('Patches/User/我的音色/测试.toml'),'import');assert.equal(device.loadedPath,undefined);
  await page.locator('#ddsImportFile').setInputFiles({name:'测试.toml',mimeType:'text/plain',buffer:Buffer.from('format = 1\nname = "changed"\n')});await page.locator('#ddsImport').click();await until(()=>page.locator('#ddsImportConflict').isVisible(),'replace visible');await page.locator('#ddsImportReplace').click();await until(()=>device.files.get('Patches/User/我的音色/测试.toml').includes('changed'),'replace');assert.equal(device.loadedPath,undefined);
 });
 await check('no uncaught browser errors','browser pageerror',async()=>assert.deepEqual(errors,[]));
}finally{
 if(live){await live.context.close();await live.close();}
 const report={date:'2026-10-30 Asia/Tokyo',html_sha256,html_sha256_at_end:await htmlHash(),browser:browser.version(),physical_device_verified:false,loopback_only:true,checks,errors,observations,screenshots};
 await writeFile(resolve(output,'strings-browser-report.json'),JSON.stringify(report,null,2)+'\n');await browser.close();
}
console.log(JSON.stringify({passed:checks.filter(x=>x.result==='PASS').length,failed:checks.filter(x=>x.result==='FAIL').length,errors}));if(checks.some(x=>x.result!=='PASS'))process.exitCode=1;
