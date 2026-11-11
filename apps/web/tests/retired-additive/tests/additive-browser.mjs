import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {startV2Server} from '../mock/v2-server.mjs';
import {AdditiveDevice} from '../mock/additive-device.mjs';
import {V2Device} from '../mock/v2-device.mjs';
import {getAdditivePreset} from '../src/additive.mjs';
const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'artifacts/additive-ui-20261019/browser');await mkdir(output,{recursive:true});
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const results=[],errors=[],requests=[];let live;
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function until(fn,label,timeout=7000){const end=Date.now()+timeout;while(!await fn()){if(Date.now()>end)throw Error('timeout: '+label);await wait(25);}}
async function open(device=new AdditiveDevice(),width=390){if(live){await live.context.close();await live.close();}
  const server=await startV2Server({device}),context=await browser.newContext({viewport:{width,height:900},isMobile:width<768,hasTouch:true}),page=await context.newPage();live={...server,context,page};
  page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(r.url().includes('/api/'))requests.push({url:r.url(),method:r.method(),body:r.postData()});});
  await page.goto(server.url);assert.equal(await page.locator('#additive').count(),1,'additive entry must exist');return live;
}
async function playable(page){await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);}
async function selectAdditive(){await playable(live.page);await live.page.locator('#additive').click();await until(()=>live.device.state.mode.applied===3&&!live.device.state.mode.switching,'additive selected');await playable(live.page);}
async function input(page,selector,value){await page.locator(selector).evaluate((el,v)=>{el.value=String(v);el.dispatchEvent(new Event('input',{bubbles:true}));},value);}
async function check(name,run){try{await run();results.push({name,result:'PASS'});console.log('PASS '+name);}catch(error){results.push({name,result:'FAIL',error:error.message});console.error('FAIL '+name+': '+error.message);}}
try{
  await check('older firmware keeps existing engines and disables additive without false support',async()=>{
    const {page}=await open(new V2Device());await playable(page);assert.equal(await page.locator('#additive').isDisabled(),true);assert.equal(await page.locator('#additiveSound').isVisible(),false);assert.equal(await page.locator('#fm').isDisabled(),false);
  });
  await check('new capability exposes 8 voices, 32 partials and no automatic patch writes',async()=>{
    const {page,device}=await open();await playable(page);assert.equal(device.log.filter(e=>e.stage==='patch-received').length,0);assert.equal(device.log.filter(e=>e.stage==='received'&&e.op===5).length,0);
    await selectAdditive();assert.equal(await page.locator('#additiveSound').isVisible(),true);assert.equal(await page.locator('[data-additive-weight]').count(),32);assert.equal(await page.locator('#fmSound').isVisible(),false);assert.equal(await page.locator('#ddsPanels').isVisible(),false);assert.match(await page.locator('#voiceTag').textContent(),/8.*32/);assert.equal(device.log.filter(e=>e.stage==='patch-received').length,0);
  });
  await check('partial edits remain a local draft until one explicit atomic apply',async()=>{
    const {page,device}=live;const token=device.log.filter(e=>e.stage==='session').at(-1).token;
    await input(page,'[data-additive-weight="0"]',30000);await wait(120);assert.equal(device.state.additive.patch.partials[0].weight,65535);assert.equal(device.log.filter(e=>e.stage==='patch-received').length,0);
    await page.locator('#release').click();await playable(page);assert.equal(await page.locator('[data-additive-weight="0"]').inputValue(),'30000');
    await page.locator('#additiveApply').click();await until(()=>device.log.some(e=>e.stage==='patch-committed'),'commit');await playable(page);
    assert.equal(device.log.filter(e=>e.stage==='patch-received').length,1);assert.equal(device.state.additive.patch.partials[0].weight,30000);assert.notEqual(device.log.filter(e=>e.stage==='session').at(-1).token,token);assert.equal(device.state.clients.reduce((v,s)=>v|s.keys,0),0);
  });
  await check('example selection explicitly loads draft and preserves exact nonharmonic ratios',async()=>{
    const {page,device}=live;await page.locator('#additivePreset').selectOption('bell');await page.locator('#additiveLoadPreset').click();const before=device.log.filter(e=>e.stage==='patch-received').length;await wait(100);assert.equal(device.log.filter(e=>e.stage==='patch-received').length,before);
    await page.locator('#additiveApply').click();await until(()=>device.state.additive.patch.partials[1].ratio_q16===getAdditivePreset('bell').partials[1].ratio_q16,'bell committed');await playable(page);assert.deepEqual(device.state.additive.patch,getAdditivePreset('bell'));
  });
  await check('Panic interrupts a pending additive commit without reviving the cancelled patch',async()=>{
    const {page,device}=await open();await selectAdditive();device.nativeDelay=500;await input(page,'[data-additive-weight="0"]',12345);await page.locator('#additiveApply').click();await until(()=>device.log.some(e=>e.stage==='patch-owner'),'owner pending');assert.equal(await page.locator('[data-additive-weight="0"]').isDisabled(),true);assert.equal(await page.locator('#additiveLoadPreset').isDisabled(),true);await page.locator('#panic').click();await playable(page);await wait(600);assert.equal(device.log.filter(e=>e.stage==='patch-committed').length,0);assert.equal(device.state.additive.patch.partials[0].weight,65535);assert.equal(device.state.clients.reduce((v,s)=>v|s.keys,0),0);
  });
  await check('HTTP rejection stays explicit and reconnect never resubmits the old patch',async()=>{
    const {page,device}=await open();await selectAdditive();device.patchStatus=503;await input(page,'[data-additive-weight="0"]',12000);await page.locator('#additiveApply').click();await until(async()=>await page.locator('#connectionText').textContent()==='未连接','rejection');device.patchStatus=0;await page.locator('#reconnect').click();await playable(page);assert.equal(device.state.additive.patch.partials[0].weight,65535);assert.equal(device.log.filter(e=>e.stage==='patch-received').length,0);
  });
  await check('owner receipt without hardware commit times out and never claims the patch applied',async()=>{
    const {page,device}=await open();await selectAdditive();device.noCommit=true;await input(page,'[data-additive-weight="0"]',10000);await page.locator('#additiveApply').click();
    await until(()=>device.log.some(e=>e.stage==='patch-owner'),'missing commit owner');await page.locator('#release').click();
    await until(async()=>await page.locator('#connectionText').textContent()==='未连接','commit timeout');assert.match(await page.locator('#feedback').textContent(),/加法音色提交确认超时/);assert.equal(device.state.additive.patch.partials[0].weight,65535);assert.equal(await page.locator('[data-key="0"]').isDisabled(),true);assert.equal(device.log.filter(e=>e.stage==='patch-received').length,1);
    device.noCommit=false;device.state.additive.busy=false;await page.locator('#reconnect').click();await playable(page);assert.equal(device.log.filter(e=>e.stage==='patch-received').length,1);
  });
  for(const width of [320,390,768,1440])await check('additive layout '+width+'px keeps document within viewport',async()=>{
    const {page}=await open(new AdditiveDevice(),width);await selectAdditive();assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    if(width===390||width===1440)await page.locator('#additiveSound').screenshot({path:resolve(output,'additive-'+width+'.png')});
  });
  await check('no uncaught scripts and all control requests remain local to the model',async()=>{assert.deepEqual(errors,[]);assert(requests.every(r=>r.url.startsWith('http://127.0.0.1:')));});
}finally{
  if(live){await live.context.close();await live.close();}await writeFile(resolve(output,'additive-browser-report.json'),JSON.stringify({browser:browser.version(),physical_device_verified:false,results,errors},null,2)+'\n');await browser.close();
}
if(results.some(r=>r.result!=='PASS'))process.exitCode=1;
