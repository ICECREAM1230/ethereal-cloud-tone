import assert from 'node:assert/strict';
import {mkdir,writeFile,readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {startUiPreview} from '../scripts/preview-ui.mjs';
import {FdcUiDevice} from './helpers/fdc-ui-device.mjs';

const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'artifacts/fdc');await mkdir(output,{recursive:true});
const checks=[],errors=[],posts=[];
const html_sha256=createHash('sha256').update(await readFile(new URL('../dist/index.html',import.meta.url))).digest('hex');
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
let live;
async function until(fn,label,timeout=7000){const start=performance.now();while(!await fn()){if(performance.now()-start>timeout)throw Error('timeout '+label);await new Promise(r=>setTimeout(r,20));}}
async function open(options={}){
  if(live){await live.context.close();await live.close();}
  const device=new FdcUiDevice(options),server=await startUiPreview({device});
  const context=await browser.newContext({viewport:{width:options.width||390,height:844},serviceWorkers:'block'}),page=await context.newPage();
  page.on('pageerror',error=>errors.push(error.message));page.on('request',request=>assert.equal(new URL(request.url()).hostname,'127.0.0.1'));
  await page.goto(server.url+'/#theremin');live={...server,context,page};
  await until(()=>page.locator('#connectionText').textContent().then(v=>v.includes('已连接')),'connected');
  await until(()=>page.locator('#fdcStatus').textContent().then(v=>!v.includes('连接设备后')),'fdc observation');return live;
}
async function check(name,run){try{await run();checks.push({name,result:'PASS'});console.log('PASS '+name);}catch(error){checks.push({name,result:'FAIL',error:error.stack});console.error('FAIL '+name,error);}}
try{
  await check('390px visible FDC panel has six empty manual fields and no overflow',async()=>{
    const {page}=await open();assert.equal(await page.locator('#fdcEnable').isEnabled(),true);
    assert.equal(await page.locator('#fdcCalibration input').count(),6);assert.deepEqual(await page.locator('#fdcCalibration input').evaluateAll(inputs=>inputs.map(input=>input.value)),['','','','','','']);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.locator('#fdcPanel').scrollIntoViewIfNeeded();await page.screenshot({path:resolve(output,'fdc-390.png'),fullPage:true});
  });
  await check('enable receipt and revision/observed enabled never imply application',async()=>{
    const {page,device}=await open();device.fdcDelay=2500;
    await page.locator('#fdcEnable').click();await until(()=>device.fdcPosts.length===1,'enable queued');
    device.fdcFlags=15;device.fdcRevision++;
    await page.locator('#fdcRefresh').click();await until(()=>page.locator('#fdcObserved').textContent().then(v=>v.includes('控制已启用')),'observed enabled');
    assert.match(await page.locator('#fdcStatus').textContent(),/排队/);
    await until(()=>page.locator('#fdcStatus').textContent().then(v=>v.includes('传输已发送')),'transport terminal');
    assert.match(await page.locator('#fdcStatus').textContent(),/不代表 CPU 已采用/);assert.equal(device.fdcPosts.length,1);posts.push(...device.fdcPosts);
  });
  await check('manual complete calibration emits canonical six words and no automatic re-enable',async()=>{
    const {page,device}=await open();await page.locator('#fdcPanel summary').click();
    await page.locator('#fdcApply').click();assert.match(await page.locator('#fdcStatus').textContent(),/完整的六个/);assert.equal(device.fdcPosts.length,0);
    const values={pitch_far:'1000',pitch_near:'2000',volume_far:'3000',volume_near:'1000',pitch_sigma:'1',volume_sigma:'2'};
    for(const [name,value] of Object.entries(values))await page.locator('[name="'+name+'"]').fill(value);
    await page.locator('#fdcApply').click();await until(()=>device.fdcPosts.length===1,'calibration queued');
    assert.equal(device.fdcPosts[0].identity.op,3);assert.deepEqual(device.fdcPosts[0].words,[1000,2000,3000,1000,1065353216,1073741824]);
    await until(()=>page.locator('#fdcStatus').textContent().then(v=>v.includes('传输已发送')),'calibration transport');
    assert.match(await page.locator('#fdcObserved').textContent(),/控制已关闭/);assert.equal(device.fdcPosts.length,1);posts.push(...device.fdcPosts);
  });
  await check('KS mode keeps ordinary strings playable while only disable/restart remain available',async()=>{
    const {page}=await open({mode:1});assert.equal(await page.locator('#fdcEnable').isDisabled(),true);assert.equal(await page.locator('#fdcApply').isDisabled(),true);
    assert.equal(await page.locator('#fdcDisable').isEnabled(),true);assert.ok(await page.locator('.string-cell:enabled').count()>0);
  });
  await check('CPU eligible permits FDC without mode metadata and fault still permits restart',async()=>{
    const {page,device}=await open({modeMetadata:false});assert.equal(await page.locator('#fdcEnable').isEnabled(),true);
    device.fdcFlags=35;device.fdcRevision++;await page.locator('#fdcRefresh').click();
    await until(()=>page.locator('#fdcHealth').textContent().then(v=>v.includes('故障')),'fault observed');
    assert.equal(await page.locator('#fdcEnable').isDisabled(),true);assert.equal(await page.locator('#fdcRestart').isEnabled(),true);
    await page.locator('#fdcRestart').click();await until(()=>device.fdcPosts.length===1,'restart queued');
    assert.equal(device.fdcPosts[0].identity.op,2);await until(()=>page.locator('#fdcStatus').textContent().then(v=>v.includes('传输已发送')),'restart transport only');
    assert.match(await page.locator('#fdcHealth').textContent(),/故障/);posts.push(...device.fdcPosts);
  });
  await check('missing endpoint disables only FDC and preserves existing piano/STOP',async()=>{
    const {page,device}=await open({available:false});assert.match(await page.locator('#fdcStatus').textContent(),/尚未提供 FDC/);
    assert.equal(await page.locator('#fdcEnable').isDisabled(),true);assert.ok(await page.locator('.piano-key:enabled').count()>0);
    await page.locator('#panic').click();await until(()=>device.log.some(e=>e.stage==='received'&&e.op===4),'existing STOP');assert.equal(device.fdcPosts.length,0);
  });
  await check('STOP cancels pending UI identity and never retries arming',async()=>{
    const {page,device}=await open();device.holdFdc=true;
    await page.locator('#fdcEnable').click();await until(()=>!!device.releaseFdc,'held FDC post');
    await page.locator('#panic').click();await until(()=>device.log.some(e=>e.stage==='received'&&e.op===4),'STOP while post pending');
    device.releaseFdc();await new Promise(r=>setTimeout(r,1200));assert.equal(device.fdcPosts.length,1);assert.doesNotMatch(await page.locator('#fdcStatus').textContent(),/实际.*采用成功/);
  });
  assert.deepEqual(errors,[]);
}finally{
  if(live){await live.context.close();await live.close();}await browser.close();
  await writeFile(resolve(output,'summary.json'),JSON.stringify({status:checks.every(c=>c.result==='PASS')&&errors.length===0?'PASS':'FAIL',html_sha256,checks,errors,posts,scope:'Loopback mock plus real Chrome; no board/network/device evidence.'},null,2)+'\n');
}
if(checks.some(c=>c.result!=='PASS')||errors.length)process.exitCode=1;
