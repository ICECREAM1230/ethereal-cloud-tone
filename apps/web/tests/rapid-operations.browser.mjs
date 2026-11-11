import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {chromium} from 'playwright';
import {bundleModules} from '../scripts/bundle.mjs';
import {startV2Server} from '../mock/v2-server.mjs';
import {V2Device} from '../mock/v2-device.mjs';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'artifacts/rapid-operations');
await mkdir(output,{recursive:true});
// Read an explicit frozen artifact, or bundle the working source in memory.
// Neither path rewrites dist or firmware; all HTTP commands go to loopback.
const artifactPath=process.env.WEB_UI_HTML?resolve(process.env.WEB_UI_HTML):null;
let html;
if(artifactPath)html=await readFile(artifactPath,'utf8');
else{
  const script=await bundleModules(resolve(root,'src'));
  const readStyle=await readFile(resolve(root,'src/style.css'),'utf8');
  html=(await readFile(resolve(root,'src/index.html'),'utf8'))
    .replace('/* STYLE_SLOT */',()=>readStyle).replace('/* SCRIPT_SLOT */',()=>script.replace(/<\/script/gi,match=>'<\\/'+match.slice(2)));
}
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const results=[],errors=[];let live;
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const received=d=>d.log.filter(event=>event.stage==='received');
async function until(fn,label,timeout=6000){const start=Date.now();while(!await fn()){if(Date.now()-start>timeout)throw Error('timeout: '+label);await wait(15);}}
const pointer=(page,key,type,id=77)=>page.locator(`[data-key="${key}"]`).dispatchEvent(type,{pointerId:id,pointerType:'touch',button:0,bubbles:true});
async function open(){
  if(live){await live.context.close();await live.close();}
  const d=new V2Device({ownerDelay:10}),server=await startV2Server({device:d});
  const context=await browser.newContext({viewport:{width:390,height:844},hasTouch:true,isMobile:true,serviceWorkers:'block'});
  const page=await context.newPage();live={...server,context,page};
  await page.route(server.url+'/',route=>route.fulfill({status:200,contentType:'text/html; charset=utf-8',body:html}));
  page.on('pageerror',error=>errors.push(error.message));
  await page.goto(server.url);await until(async()=>!await page.locator('[data-key="0"]').isDisabled(),'initial owner empty');
  return {page,d};
}
async function check(name,run){
  try{await run();results.push({name,result:'PASS',deviceLog:live.device.log});console.log('PASS '+name);}
  catch(error){results.push({name,result:'FAIL',error:error.stack,deviceLog:live?.device.log});console.error('FAIL '+name+': '+error.message);}
}
try{
  await check('ordinary musical BUSY never greys the keyboard during 24 rapid taps',async()=>{
    const {page,d}=await open();const token=d.state.clients[0].token;
    await pointer(page,0,'pointerdown',90);await until(()=>d.snapshot().clients[0].keys===1,'first On');
    d.configure({busy:true});
    await until(async()=>JSON.parse(await page.locator('#raw').textContent()).fm.flags===11,'BUSY observed by page');
    const disabled=await page.locator('.piano-key:disabled').count();
    results.push({name:'observed BUSY presentation',result:'EVIDENCE',disabled_keys:disabled,feedback:await page.locator('#feedback').textContent(),connection:await page.locator('#connectionText').textContent()});
    await page.screenshot({path:resolve(output,'music-busy.png')});
    assert.equal(disabled,0,'ordinary mailbox BUSY must not disable all 25 keys');
    assert.equal(await page.locator('#fm').isDisabled(),true);
    assert.equal(await page.locator('#fmPanel input').isDisabled(),true);
    assert.equal(await page.locator('#fmPreset').isDisabled(),true);
    assert.equal(await page.locator('#cc7').isDisabled(),false);
    assert.match(await page.locator('#fmAvailability').textContent(),/演奏.*等待空闲/);
    await pointer(page,0,'pointerup',90);await until(()=>d.snapshot().clients[0].keys===0,'first Off');
    const before=received(d).filter(event=>event.op===3&&event.a).length;
    for(let index=0;index<24;index++){
      const key=[0,2,4][index%3],id=100+index;
      await pointer(page,key,'pointerdown',id);await wait(15);
      await pointer(page,key,'pointerup',id);await wait(15);
    }
    await until(()=>d.snapshot().clients[0].keys===0&&received(d).filter(event=>event.op===3&&event.a).length===before+24,'all rapid On Off adopted');
    assert.equal(d.state.clients[0].token,token);
    assert.equal(d.log.filter(event=>event.stage==='session').length,1);
    assert.equal(await page.locator('.piano-key:disabled').count(),0);
    assert.equal(await page.locator('.piano-key[aria-pressed="true"]').count(),0);
    assert.match(await page.locator('#connectionText').textContent(),/已连接/);
    assert.doesNotMatch(await page.locator('#receiptInfo').textContent(),/timeout|not_ready|owner result/);
    const edges=received(d).filter(event=>event.op===3).slice(1);
    assert(edges.every((event,index)=>index===0||event.sequence>edges[index-1].sequence));
    assert.equal(edges.filter(event=>event.a===0).length,25);
    d.configure({busy:false});await until(async()=>!await page.locator('#fmPanel input').isDisabled(),'idle edits restored');
    await page.screenshot({path:resolve(output,'rapid-taps-complete.png')});
  });
  for(const [name,change] of [
    ['MUTED',d=>d.state.fm.flags=15],['FAULT',d=>d.state.fm.flags=27],
    ['recovering mode',d=>d.state.mode.recovering=true]
  ])await check(`true ${name} still disables performance and clears old contacts`,async()=>{
    const {page,d}=await open();await pointer(page,0,'pointerdown',190);await until(()=>d.snapshot().clients[0].keys===1,'held before fault');
    change(d);await until(async()=>await page.locator('.piano-key:disabled').count()===25,'fault fence');
    const before=received(d).filter(event=>event.op===3&&event.a).length;
    await pointer(page,2,'pointerdown',191);await wait(150);
    assert.equal(received(d).filter(event=>event.op===3&&event.a).length,before);
    await until(()=>d.snapshot().clients.reduce((mask,slot)=>mask|slot.keys,0)===0,'old contacts released');
    assert.equal(await page.locator('.piano-key[aria-pressed="true"]').count(),0);
  });
  await check('no uncaught browser errors',async()=>assert.deepEqual(errors,[]));
}finally{
  if(live){await live.context.close();await live.close();}
  await writeFile(resolve(output,'rapid-operations-report.json'),JSON.stringify({browser:browser.version(),physical_device_verified:false,
    html_path:artifactPath,html_bytes:Buffer.byteLength(html),html_sha256:createHash('sha256').update(html).digest('hex'),results,errors},null,2)+'\n');
  await browser.close();
}
if(results.some(result=>result.result==='FAIL'))process.exitCode=1;
