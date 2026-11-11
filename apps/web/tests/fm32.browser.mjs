import assert from 'node:assert/strict';
import {mkdir,writeFile,readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {startUiPreview} from '../scripts/preview-ui.mjs';
import {FdcUiDevice} from './helpers/fdc-ui-device.mjs';
import {getPreset,packTarget,FM_TARGET_NONE} from '../src/generated/fm-contract.mjs';

const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'artifacts/fm32');await mkdir(output,{recursive:true});
class PairedDevice extends FdcUiDevice {
 constructor(){super({mode:0});this.legacy=false;this.stale=false;this.audio=[0,1];this.routePosts=[];
  this.caps.cpu_mode.channel_routes=this.state.cpu_mode.channel_routes=true;
  this.state.clients[0].midi_channel=2;
  this.state.fm.parameter_values=this.presetValues(3,0);this.state.fm.features=816;
  this.state.fm.accepted=this.state.fm.completed=this.state.fm.result=null;
  this.nativeSequence=0;

 }
 presetValues(id,sequence){return getPreset(1,id).values.map(v=>({parameter_id:v.parameter_id,target_word:packTarget(FM_TARGET_NONE,v.operator_index),value_u32:v.value_u32,sequence_id:sequence}));}
 gate(cmd,slot){if([7,8].includes(cmd.op))return this.state.fm.valid&&this.state.fm.flags===3?null:'engine_not_ready';return super.gate(cmd,slot);}
 adopt(cmd,session){
  if(![7,8].includes(cmd.op))return super.adopt(cmd,session);
  const slot=this.state.clients[cmd.client];
  Object.assign(slot,{token:cmd.token,result_sequence:cmd.sequence,result_code:0,result_context_epoch:cmd.context,parameter_processed:cmd.sequence});
  this.log.push({...cmd,stage:'owner',code:0});
  const identity={session_id:this.state.fm.session_id,engine_epoch:this.state.fm.engine_epoch,sequence_id:++this.nativeSequence};
  this.state.fm.flags|=4;this.state.fm.accepted={...identity};
  this.schedule(100,()=>{
   this.state.fm.flags&=~4;this.state.fm.completed={...identity};this.state.fm.result={...identity,code:0};
   if(cmd.op===8)this.state.fm.parameter_values=this.presetValues(cmd.a,identity.sequence_id);
   else this.cache(cmd.a,cmd.b,cmd.c,identity.sequence_id);
  });
 }
 fdcSnapshot(){const s=super.fdcSnapshot();s.status_word=((this.legacy?0xfdc10000:0xfdc20000)|(this.fdcRevision<<8)|this.fdcFlags)>>>0;
  s.audio_engine=this.legacy?'subtractive':'fm';s.timbre=this.legacy?'unknown':'electric_piano_tine_sustained';s.continuous=!this.legacy;s.fresh=!this.stale;
  s.raw={version:1,supported:true,valid:true,fresh:!this.stale,sample_valid:true,sample_fresh:!this.stale,ch0:13334567,ch1:13199000,gain_q16:65536,gain_percent:100,sequence:41,revision:1,quality_ch0:0,quality_ch1:0,muted:false,pitch_stale:false,volume_stale:false};return s;}
 async request(path,options={}){
  if(path==='/api/audio/routes'){
   if(options.method==='POST'){this.audio=options.body.split(' ').map(Number);this.routePosts.push(options.body);}
   return {version:1,available:true,valid:true,ready:true,pending:false,busy:false,error:0,pt8211:this.audio[0],pcm_a:this.audio[1]};
  }
  return super.request(path,options);
 }
}
const presetNames=['正弦测试音','双算子键盘','四算子钟声','电钢琴','金属钟声'];
const errors=[],checks=[];const device=new PairedDevice();
const server=await startUiPreview({device});
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const context=await browser.newContext({viewport:{width:390,height:844},serviceWorkers:'block'});const page=await context.newPage();
page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>assert.equal(new URL(r.url()).hostname,'127.0.0.1'));
const until=async(fn,label)=>{const start=performance.now();while(!await fn()){if(performance.now()-start>10000)throw Error('timeout '+label);await new Promise(r=>setTimeout(r,30));}};
async function check(name,fn){await fn();checks.push({name,status:'PASS'});console.log('PASS '+name);}
try{
 await page.goto(server.url);
 await until(()=>page.locator('#connectionText').textContent().then(x=>x.includes('已连接')),'connected');
 await check('actual product FM32 profile and panel show32 voices and4 operators',async()=>{
  await page.locator('.tabs a[href="#sound"]').click();
  await until(()=>page.locator('#fmPanel option[value="voice:31"]').count().then(x=>x>0),'32voice panel');
  assert.match(await page.locator('#fmAvailability').textContent(),/32复音.*4算子/);
  const voices=await page.locator('#fmPanel option').evaluateAll(xs=>xs.filter(x=>x.value.startsWith('voice:')).map(x=>x.value));
  assert.equal(voices.length,32);assert.equal(voices.at(-1),'voice:31');
  assert.deepEqual(await page.locator('#fmPreset option').evaluateAll(xs=>xs.filter(x=>x.value!=='').map(x=>({id:x.value,name:x.textContent}))),presetNames.map((name,id)=>({id:String(id),name})));
  assert.doesNotMatch(await page.locator('#fmPreset').textContent(),/diagnostic_sine|two_op_keys|four_op_bell|electric_piano_tine|metal_bell/);
 });
 await check('confirmed boot electric piano is selected without an automatic apply',async()=>{
  await until(()=>page.locator('#fmPreset').inputValue().then(x=>x==='3'),'default electric piano');
  assert.match(await page.locator('#fmPresetDescription').textContent(),/参数匹配预置：电钢琴。/);
  assert.equal(device.log.filter(x=>x.op===8).length,0);
  assert.doesNotMatch(await page.locator('#fmPresetDescription').textContent(),/electric_piano_tine|Original|Yamaha|DX7/);
  await page.screenshot({path:resolve(output,'default-epiano-390.png'),fullPage:true});
 });
 await check('polling preserves a manually chosen different preset without applying it',async()=>{
  await page.locator('#fmPreset').selectOption('4');await page.waitForTimeout(650);
  assert.equal(await page.locator('#fmPreset').inputValue(),'4');assert.equal(device.log.filter(x=>x.op===8).length,0);
  assert.match(await page.locator('#fmPresetDescription').textContent(),/参数匹配预置：电钢琴/);
 });
 await check('all five explicit preset choices complete with one FM session and update the confirmed label',async()=>{
  const sessions=device.log.filter(x=>x.stage==='session').length;
  for(const id of [0,1,2,3,4]){
   await until(()=>page.locator('#applyPreset').isEnabled(),'preset editing ready');
   const before=device.log.filter(x=>x.stage==='owner'&&x.op===8).length;
   await page.locator('#fmPreset').selectOption(String(id));await page.locator('#applyPreset').click();
   await until(()=>device.log.filter(x=>x.stage==='owner'&&x.op===8).length===before+1,'one explicit preset');
   await until(()=>page.locator('#fmPresetDescription').textContent().then(x=>x.includes('参数匹配预置：')&&x.includes(presetNames[id])),'confirmed preset label');
   await until(()=>page.locator('#applyPreset').isEnabled(),'native completion');
  }
  assert.equal(device.log.filter(x=>x.stage==='session').length,sessions);
 });
 await check('an explicit next-note parameter edit stops claiming a catalog match',async()=>{
  const input=page.locator('[data-fm-parameter="17"][data-fm-operator="patch-operator:0"] input');
  assert.equal(await input.isEnabled(),true);
  await input.evaluate(el=>{el.value='42';el.dispatchEvent(new Event('input',{bubbles:true}));});
  await until(()=>device.state.fm.parameter_values.some(x=>x.parameter_id===17&&x.target_word===packTarget(FM_TARGET_NONE,0)&&x.value_u32===42),'confirmed custom parameter');
  await until(()=>page.locator('#fmPresetDescription').textContent().then(x=>x.includes('当前参数未完整匹配目录预置')),'custom parameter label');
 });
 await check('390px FM32 layout has no horizontal overflow',async()=>{
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
  await page.screenshot({path:resolve(output,'fm32-390.png'),fullPage:true});
 });
 await check('independent theremin shows continuous FM electric piano and full-scale raw gain',async()=>{
  await page.locator('.tabs a[href="#theremin"]').click();
  await until(()=>page.locator('#fdcRaw').textContent().then(x=>x.includes('65536/65536')),'raw gain');
  await until(()=>page.locator('#regularPages').isVisible().then(x=>!x),'independent page');
  await until(()=>page.locator('#thereminVoice').textContent().then(x=>/FM.*电钢琴.*持续/.test(x)),'continuous FM identity');
  await until(()=>page.locator('#fdcEnable').isEnabled(),'enabled controls');
  assert.equal(await page.locator('#fdcEnable').isEnabled(),true);
  assert.equal(await page.locator('#fdcCalibration input').count(),6);
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
  await page.screenshot({path:resolve(output,'theremin-390.png'),fullPage:true});
 });
 await check('theremin observation and navigation issue no enable/calibration request',async()=>{
  assert.equal(device.fdcPosts.length,0);assert.equal(device.routePosts.length,0);
 });
 await check('stale FDC state closes enabling without hiding diagnostics',async()=>{
  device.stale=true;await page.locator('#fdcRefresh').click();
  await until(()=>page.locator('#fdcEnable').isEnabled().then(x=>!x),'stale arm gate');
  assert.equal(await page.locator('#fdcDisable').isEnabled(),false);
  assert.match(await page.locator('#fdcRaw').textContent(),/失鲜/);
 });
 await check('legacy FDC1 stays disable-only and never claims continuous FM',async()=>{
  device.stale=false;device.legacy=true;device.fdcRevision++;await page.locator('#fdcRefresh').click();
  await until(()=>page.locator('#thereminVoice').textContent().then(x=>!x.includes('持续')),'legacy identity');
  await until(()=>page.locator('#fdcDisable').isEnabled(),'fresh legacy controls');
  assert.equal(await page.locator('#fdcEnable').isEnabled(),false);assert.equal(await page.locator('#fdcApply').isEnabled(),false);
  assert.equal(await page.locator('#fdcDisable').isEnabled(),true);
 });
 await check('1280px independent theremin and navigation retain no overflow',async()=>{
  device.legacy=false;device.fdcRevision++;await page.locator('#fdcRefresh').click();await page.setViewportSize({width:1280,height:900});
  await until(()=>page.locator('#fdcEnable').isEnabled(),'new identity');
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
  await page.screenshot({path:resolve(output,'theremin-1280.png'),fullPage:true});
  await page.locator('.tabs a[href="#perform"]').click();await until(()=>page.locator('#regularPages').isVisible(),'regular page');
 });
 await check('reconnecting without a confirmed CPU cache never selects or reapplies boot EP',async()=>{
  const applied=device.log.filter(x=>x.stage==='owner'&&x.op===8).length;
  device.state.fm.parameter_values=[];device.sessions=Array(4).fill(null);device.state.clients[0].midi_channel=2;
  await page.reload();await until(()=>page.locator('#connectionText').textContent().then(x=>x.includes('已连接')),'reconnected unknown cache');
  await page.locator('.tabs a[href="#sound"]').click();
  await until(()=>page.locator('#fmPanel option[value="voice:31"]').count().then(x=>x>0),'unknown-cache descriptor');
  assert.equal(await page.locator('#fmPreset').inputValue(),'');
  assert.match(await page.locator('#fmPresetDescription').textContent(),/当前参数未完整匹配目录预置/);
  assert.equal(device.log.filter(x=>x.stage==='owner'&&x.op===8).length,applied);
 });
 assert.deepEqual(errors,[]);
 await writeFile(resolve(output,'result.json'),JSON.stringify({status:'PASS',browser:await browser.version(),checks,errors,html_sha256:createHash('sha256').update(await readFile(new URL('../dist/index.html',import.meta.url))).digest('hex'),scope:'Real Google Chrome; actual product HTML and loopback model only; no devices'},null,2)+'\n');
} catch(error){await page.screenshot({path:resolve(output,'failure.png'),fullPage:true});await writeFile(resolve(output,'dom.txt'),await page.locator('body').innerText());await writeFile(resolve(output,'failure.json'),JSON.stringify({checks,errors,error:error.stack},null,2)+'\n');throw error;}
finally{await context.close();await browser.close();await server.close();}
