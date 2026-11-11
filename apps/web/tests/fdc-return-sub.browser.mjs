import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {startUiPreview} from '../scripts/preview-ui.mjs';
import {FdcUiDevice} from './helpers/fdc-ui-device.mjs';
const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'artifacts/fdc-return-sub');await mkdir(output,{recursive:true});
const factory=new URL('../../../fpga/firmware/esp32/main/web/index.html',import.meta.url);
class Device extends FdcUiDevice{
 constructor(tag=0xfdc1){super();this.tag=tag;this.stale=false;}
 fdcSnapshot(){const s=super.fdcSnapshot();s.status_word=(this.tag<<16|(this.fdcRevision<<8)|this.fdcFlags)>>>0;
  s.audio_engine=this.tag===0xfdc1?'subtractive':'fm';s.timbre=this.tag===0xfdc1?'current_subtractive_patch':'electric_piano_tine_sustained';s.continuous=true;
  s.fresh=!this.stale;s.raw={version:1,supported:true,valid:true,fresh:!this.stale,sample_valid:true,sample_fresh:!this.stale,ch0:13334567,ch1:13199000,gain_q16:65536,gain_percent:100,sequence:41,revision:1,quality_ch0:0,quality_ch1:0,muted:false,pitch_stale:false,volume_stale:false};return s;}
 async request(path,options={}){if(path==='/api/audio/routes')return {version:1,available:true,valid:true,ready:true,pending:false,busy:false,error:0,pt8211:0,pcm_a:1};return super.request(path,options);}
}
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const checks=[],errors=[];let live;
async function until(fn,label){const t=performance.now();while(!await fn()){if(performance.now()-t>7000)throw Error('timeout '+label);await new Promise(r=>setTimeout(r,20));}}
async function open(tag=0xfdc1){
 if(live){await live.context.close();await live.close();}
 const device=new Device(tag),server=await startUiPreview({device,htmlPath:factory});
 const context=await browser.newContext({viewport:{width:390,height:844},serviceWorkers:'block'}),page=await context.newPage();
 page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>assert.equal(new URL(r.url()).hostname,'127.0.0.1'));
 live={...server,device,context,page};await page.goto(server.url+'/#theremin');
 await until(()=>page.locator('#fdcEnable').isEnabled(),'eligible FDC');return live;
}
async function check(name,fn){try{await fn();checks.push({name,status:'PASS'});console.log('PASS '+name);}catch(e){checks.push({name,status:'FAIL',error:e.stack});console.error('FAIL '+name,e);}}
try{
 await check('factory direct theremin page displays subtractive current patch and six untouched calibration fields',async()=>{
  const {page,device}=await open();assert.equal(await page.locator('#theremin').isVisible(),true);assert.equal(await page.locator('#regularPages').isVisible(),false);
  assert.match(await page.locator('#thereminVoice').textContent(),/减法合成器.*当前减法音色.*持续发声/);
  assert.match(await page.locator('#fdcHealth').textContent(),/减法合成器通路可用/);
  assert.deepEqual(await page.locator('#fdcCalibration input').evaluateAll(xs=>xs.map(x=>x.value)),['','','','','','']);
  assert.match(await page.locator('#fdcRaw').textContent(),/13334567.*13199000.*65536/);assert.equal(device.fdcPosts.length,0);
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
  await page.screenshot({path:resolve(output,'subtractive-390.png'),fullPage:true});
 });
 await check('navigation to and from standalone theremin page never arms or stops FDC',async()=>{
  const {page,device}=await open();await page.locator('.tabs a[href="#perform"]').click();await page.locator('.tabs a[href="#theremin"]').click();
  assert.equal(device.fdcPosts.length,0);assert.equal(await page.locator('#theremin').isVisible(),true);
 });
 await check('FDC1 enable sends one request and retains transport-only wording',async()=>{
  const {page,device}=await open();await page.locator('#fdcEnable').click();await until(()=>device.fdcPosts.length===1,'enable request');
  await until(()=>page.locator('#fdcStatus').textContent().then(v=>v.includes('传输已发送')),'transport receipt');
  assert.equal(device.fdcPosts[0].identity.op,1);assert.match(await page.locator('#fdcStatus').textContent(),/不代表 CPU 已采用/);
  assert.match(await page.locator('#fdcObserved').textContent(),/控制已关闭/);
 });
 await check('FDC1 complete calibration preserves six-word protocol and does not re-enable',async()=>{
  const {page,device}=await open();await page.locator('#fdcPanel summary').click();
  for(const [name,value]of Object.entries({pitch_far:'1000',pitch_near:'2000',volume_far:'3000',volume_near:'1000',pitch_sigma:'1',volume_sigma:'2'}))await page.locator('[name="'+name+'"]').fill(value);
  await page.locator('#fdcApply').click();await until(()=>device.fdcPosts.length===1,'calibrate request');
  assert.equal(device.fdcPosts[0].identity.op,3);assert.deepEqual(device.fdcPosts[0].words,[1000,2000,3000,1000,1065353216,1073741824]);
  await until(()=>page.locator('#fdcStatus').textContent().then(v=>v.includes('传输已发送')),'calibrate transport');assert.equal(device.fdcPosts.length,1);
 });
 await check('previous FDC2 image still shows FM and permits existing enable protocol',async()=>{
  const {page,device}=await open(0xfdc2);assert.match(await page.locator('#thereminVoice').textContent(),/FM 电钢琴.*持续发声/);
  await page.locator('#fdcEnable').click();await until(()=>device.fdcPosts.length===1,'FM compatibility enable');assert.equal(device.fdcPosts[0].identity.op,1);
 });
 await check('stale observation cannot arm or calibrate either route',async()=>{
  const {page,device}=await open();device.stale=true;await page.locator('#fdcRefresh').click();
  await until(()=>page.locator('#fdcObserved').textContent().then(v=>v.includes('尚无新鲜')),'stale observed');
  assert.equal(await page.locator('#fdcEnable').isDisabled(),true);assert.equal(await page.locator('#fdcApply').isDisabled(),true);assert.equal(device.fdcPosts.length,0);
 });
 assert.deepEqual(errors,[]);
}finally{
 if(live){await live.context.close();await live.close();}await browser.close();
 await writeFile(resolve(output,'browser-summary.json'),JSON.stringify({status:checks.every(c=>c.status==='PASS')&&!errors.length?'PASS':'FAIL',checks,errors,factory_html_sha256:createHash('sha256').update(await readFile(factory)).digest('hex'),scope:'Actual factory HTML in isolated real Chrome plus loopback mocks only; no board or external network'},null,2)+'\n');
}
if(checks.some(c=>c.status!=='PASS')||errors.length)process.exitCode=1;
