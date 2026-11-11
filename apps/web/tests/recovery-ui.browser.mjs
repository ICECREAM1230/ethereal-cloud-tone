import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {startV2Server} from '../mock/v2-server.mjs';
import {V2Device} from '../mock/v2-device.mjs';

// All control requests terminate in an independent loopback model.
const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'artifacts/recovery-ui-20261012');
await mkdir(output,{recursive:true});
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const results=[],errors=[];let live;
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const received=d=>d.log.filter(e=>e.stage==='received');
async function until(fn,label,timeout=7000){const start=Date.now();while(!await fn()){if(Date.now()-start>timeout)throw Error('timeout: '+label);await wait(25);}}
function device(mode=0,recovering=false,switching=false){
  const d=new V2Device();d.state.mode={requested:mode,applied:mode,recovering,switching};
  d.state.legacy.ready=!recovering;
  Object.assign(d.state.legacy.engine,{requested:mode,applied:mode,recovering,switching});
  return d;
}
async function open(d,width=390){
  if(live){await live.context.close();await live.close();}
  const server=await startV2Server({device:d});
  const context=await browser.newContext({viewport:{width,height:844},hasTouch:true,isMobile:true,serviceWorkers:'block'});
  const page=await context.newPage();live={...server,context,page};
  page.on('pageerror',error=>errors.push(error.message));
  await page.goto(server.url);await until(async()=>/已连接/.test(await page.locator('#connectionText').textContent()),'session adopted');
  return page;
}
async function check(name,run){
  try{await run();results.push({name,result:'PASS'});console.log('PASS '+name);}
  catch(error){results.push({name,result:'FAIL',error:error.message});console.error('FAIL '+name+': '+error.message);}
}
try {
  await check('removed wire mode stays blocked but explicit DDS selection can exit it',async()=>{
    const d=device();d.state.mode={requested:3,applied:3,switching:true,recovering:false};d.state.legacy.ready=false;
    const page=await open(d);
    assert.equal(await page.locator('.piano-key:disabled').count(),25);
    assert.equal(await page.locator('.engine-switch button').count(),3);
    assert.equal(await page.locator('#additive').count(),0);
    assert.equal(await page.locator('#bend').isDisabled(),true);
    assert.equal(await page.locator('#dds').isEnabled(),true);
    assert.equal(received(d).filter(e=>e.op===3&&e.a).length,0);
    await page.locator('#dds').click();await until(async()=>!await page.locator('[data-key="0"]').isDisabled(),'exit removed mode');
    assert.equal(d.snapshot().mode.applied,0);assert.equal(received(d).filter(e=>e.op===5&&e.a===3).length,0);
    assert.equal(received(d).filter(e=>e.op===5&&e.a===0).length,1);
  });
  for(const mode of [0,1])for(const recovering of [false,true])await check(`${mode===0?'DDS':'KS'} ${recovering?'recovering':'switching'} offers explicit reselect without new notes`,async()=>{
    const d=device(mode,recovering,true),page=await open(d),id=mode===0?'dds':'ks';
    assert.equal(await page.locator('.piano-key:disabled').count(),25);
    const status=await page.locator('#engineStatus').textContent();
    assert.match(status,recovering?/正在恢复/:/切换确认尚未完成/);assert.match(status,/重新选择/);
    const before=received(d).length;await page.locator('#'+id).click();
    await until(()=>received(d).slice(before).some(e=>e.op===5&&e.a===mode),'explicit reselect');
    await until(async()=>await page.locator('.piano-key:enabled,.string-cell:enabled').count()>0,'settled model');
    assert.equal(received(d).slice(before).filter(e=>e.op===5).length,1);
    assert.equal(received(d).filter(e=>e.op===3&&e.a).length,0);
  });
  for(const mode of [0,1])await check(`${mode===0?'DDS':'KS'} hides FM backend and settled current selection sends nothing`,async()=>{
    const d=device(mode),page=await open(d),id=mode===0?'dds':'ks';
    assert.equal(await page.locator('.fm-choice').isVisible(),false);
    const before=received(d).length;await page.locator('#'+id).click();await wait(100);
    assert.equal(received(d).slice(before).filter(e=>e.op===5).length,0);
    if(mode===0)await page.screenshot({path:resolve(output,'dds-390.png'),fullPage:true});
  });
  await check('FM controls appear only in FM sound panel after explicit selection',async()=>{
    const d=device(),page=await open(d);await page.locator('#fm').click();
    await until(async()=>!await page.locator('[data-key="0"]').isDisabled(),'FM selected');
    assert.equal(await page.locator('#fmSound .fm-choice').isVisible(),true);
    assert.equal(await page.locator('#perform .fm-choice').count(),0);
    await page.screenshot({path:resolve(output,'fm-390.png'),fullPage:true});
    await page.locator('#ks').click();await until(async()=>await page.locator('.string-cell:enabled').count()>0,'KS selected');
    assert.equal(await page.locator('.fm-choice').isVisible(),false);
  });
  await check('DDS engine_not_ready rejection is visible without an FM diagnosis or old receipt success',async()=>{
    const d=device(),page=await open(d),command=d.command.bind(d);
    d.command=(body,header)=>{const c=d.parse(body);return c?.op===3&&c.a?d.error(503,'engine_not_ready'):command(body,header);};
    await page.locator('[data-key="0"]').dispatchEvent('pointerdown',{pointerId:88,pointerType:'touch',button:0,bubbles:true});
    await until(async()=>/控制连接已释放/.test(await page.locator('#feedback').textContent()),'HTTP rejection');
    const text=await page.locator('#feedback').textContent();assert.match(text,/引擎尚未就绪/);assert.doesNotMatch(text,/FM/);
    assert.match(await page.locator('#receiptInfo').textContent(),/engine_not_ready/);
    assert.doesNotMatch(await page.locator('#receiptInfo').textContent(),/\d+ \/ \d+/);
    assert.equal(received(d).filter(e=>e.op===3&&e.a).length,0);
  });
  await check('failed mode confirmation ends waiting presentation and retains input fence',async()=>{
    const d=device(),page=await open(d);d.nativeDelay=10000;await page.locator('#ks').click();
    await until(async()=>/控制连接已释放/.test(await page.locator('#feedback').textContent()),'selection timeout');
    assert.match(await page.locator('#feedback').textContent(),/切换确认超时/);
    assert.doesNotMatch(await page.locator('#engineStatus').textContent(),/正在切换|正在确认按键释放/);
    assert.equal(await page.locator('.piano-key:disabled').count(),25);
  });
  await check('desktop DDS layout has no FM backend and no horizontal overflow',async()=>{
    const page=await open(device(),1440);assert.equal(await page.locator('.fm-choice').isVisible(),false);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.screenshot({path:resolve(output,'dds-1440.png'),fullPage:true});
  });
  await check('application has no uncaught browser errors',async()=>assert.deepEqual(errors,[]));
} finally {
  if(live){await live.context.close();await live.close();}
  await writeFile(resolve(output,'recovery-ui-report.json'),JSON.stringify({browser:browser.version(),physical_device_verified:false,results,errors},null,2)+'\n');
  await browser.close();
}
if(results.some(result=>result.result!=='PASS'))process.exitCode=1;
