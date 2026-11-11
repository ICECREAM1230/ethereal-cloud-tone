import assert from 'node:assert/strict';
import http from 'node:http';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {gzipSync} from 'node:zlib';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {ReverbDevice} from './helpers/reverb-device.mjs';

const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'output/playwright/reverb');await mkdir(output,{recursive:true});
const html=await readFile(new URL('../dist/index.html',import.meta.url)),gzip=gzipSync(html);
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const checks=[],errors=[];let live;
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(fn,label,timeout=4500){const start=performance.now();while(!await fn()){if(performance.now()-start>timeout)throw Error('timeout: '+label);await wait(20);}}
async function open(configure=()=>{},width=390,javaScriptEnabled=true){
  if(live){await live.context.close();await live.close();}
  const device=new ReverbDevice();configure(device);
  const server=http.createServer(async(req,res)=>{
    try{
      const path=new URL(req.url,'http://localhost').pathname;
      if(path==='/'||path==='/index.html'){res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Content-Encoding':'gzip','Cache-Control':'no-store'});res.end(gzip);return;}
      if(path==='/favicon.ico'){res.writeHead(204);res.end();return;}
      const parts=[];for await(const part of req)parts.push(part);
      device.time=performance.now();
      const body=await device.request(path,{method:req.method,body:Buffer.concat(parts).toString(),headers:{'X-FPGA-Control':req.headers['x-fpga-control']}});
      res.writeHead(req.method==='POST'?path.endsWith('/session')?201:202:200,{'Content-Type':'application/json'});res.end(JSON.stringify(body));
    }catch(error){res.writeHead(error.status||500,{'Content-Type':'application/json'});res.end(JSON.stringify({api_major:2,code:error.code||error.message}));}
  });
  await new Promise(done=>server.listen(0,'127.0.0.1',done));
  const context=await browser.newContext({viewport:{width,height:844},javaScriptEnabled,hasTouch:width<700,isMobile:width<700});
  const page=await context.newPage();page.on('pageerror',error=>errors.push(error.message));
  live={device,page,context,close:()=>new Promise(done=>{server.close(done);server.closeAllConnections();})};
  await page.goto('http://127.0.0.1:'+server.address().port);
  assert.equal(await page.locator('#reverbPanel').count(),1,'built firmware page must contain the reverb panel');
  if(javaScriptEnabled)await until(async()=>/已连接/.test(await page.locator('#connectionText').textContent()),'bound session');
  return live;
}
async function input(page,id,value){await page.locator('#'+id).evaluate((node,value)=>{node.value=String(value);node.dispatchEvent(new Event('input',{bubbles:true}));},value);}
async function check(name,run){await run();checks.push({name,result:'PASS'});console.log('PASS '+name);}
try{
  await check('startup exposes labelled but disabled reverb controls before any script runs',async()=>{
    const {page,device}=await open(()=>{},390,false);
    assert.equal(await page.locator('#reverbWet').isDisabled(),true);assert.equal(await page.locator('#reverbDamp').isDisabled(),true);
    assert.equal(await page.locator('#reverbOff').isDisabled(),true);assert.equal(device.reverbReads,0);assert.equal(device.reverbPosts.length,0);
  });
  await check('the actual gzip page reads device values without writing defaults and fits a phone',async()=>{
    const {page,device}=await open(d=>{d.effect.wet_q15=0;d.effect.damp_q15=28672;});
    await until(()=>page.locator('#reverbWet').isEnabled(),'reverb available');
    assert.equal(await page.locator('#reverbWet').inputValue(),'0');assert.equal(await page.locator('#reverbDamp').inputValue(),'28672');
    assert.match(await page.locator('#reverbWetValue').textContent(),/0\.0%/);assert.match(await page.locator('#reverbDampValue').textContent(),/87\.5%/);
    assert.equal(device.reverbPosts.length,0);assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.locator('#reverbPanel').screenshot({path:resolve(output,'phone-reverb.png')});
  });
  await check('dragging sends the final pair, waits for native acknowledgement, and permits notes and Off meanwhile',async()=>{
    const {page,device}=await open(d=>{d.reverbDelay=850;});await until(()=>page.locator('#reverbWet').isEnabled(),'reverb available');
    await input(page,'reverbWet',12000);await input(page,'reverbWet',16000);await input(page,'reverbDamp',9000);
    await until(()=>device.reverbPosts.length===1,'coalesced POST');assert.match(device.reverbPosts[0].body,/ 16000 9000$/);
    assert.doesNotMatch(await page.locator('#reverbStatus').textContent(),/已获设备确认/);
    await page.locator('[data-key="0"]').dispatchEvent('pointerdown',{pointerId:11,pointerType:'touch',button:0,bubbles:true});
    await until(()=>device.commands().some(e=>e.op===3&&e.a===1),'On during reverb confirmation');
    await wait(300);assert.ok(device.commands().filter(e=>e.op===3&&e.a===1).length>=2,'held note heartbeat survives');
    await page.locator('[data-key="0"]').dispatchEvent('pointerup',{pointerId:11,pointerType:'touch',button:0,bubbles:true});
    await until(()=>device.commands().at(-1).a===0,'Off during confirmation');
    await until(async()=>/已获设备确认/.test(await page.locator('#reverbStatus').textContent()),'native reverb ACK');
    assert.equal(device.reverbPosts.length,1);
  });
  await check('zero damping keeps Wet unchanged; the explicit Off button sends Wet zero and preserves damping',async()=>{
    const {page,device}=await open();await until(()=>page.locator('#reverbWet').isEnabled(),'reverb available');
    await input(page,'reverbDamp',0);await until(()=>device.reverbPosts.length===1,'damping request');assert.match(device.reverbPosts[0].body,/ 8192 0$/);
    await until(async()=>/已获设备确认/.test(await page.locator('#reverbStatus').textContent()),'damping ACK');
    await page.locator('#reverbOff').click();await until(()=>device.reverbPosts.length===2,'Off request');assert.match(device.reverbPosts[1].body,/ 0 0$/);
    await until(async()=>/已获设备确认/.test(await page.locator('#reverbStatus').textContent()),'Off ACK');
    assert.match(await page.locator('#reverbReadback').textContent(),/仅直达声/);
  });
  await check('a slow effect HTTP response keeps both the musical heartbeat and Off responsive',async()=>{
    const {page,device}=await open(d=>{d.holdReverbPost=true;d.reverbDelay=850;});
    await until(()=>page.locator('#reverbWet').isEnabled(),'reverb available');
    await page.locator('[data-key="0"]').dispatchEvent('pointerdown',{pointerId:22,pointerType:'touch',button:0,bubbles:true});
    await until(()=>device.commands().some(e=>e.op===3&&e.a===1),'held On');
    await input(page,'reverbWet',16000);await until(()=>!!device.reverbPostGate,'held effect response');
    try{
      const count=device.commands().filter(e=>e.op===3&&e.a===1).length;
      await until(()=>device.commands().filter(e=>e.op===3&&e.a===1).length>count,'heartbeat before effect response',650);
      await page.locator('[data-key="0"]').dispatchEvent('pointerup',{pointerId:22,pointerType:'touch',button:0,bubbles:true});
      await until(()=>device.commands().at(-1).a===0,'Off before effect response',350);
    }finally{device.reverbPostGate();}
    await until(async()=>/已获设备确认/.test(await page.locator('#reverbStatus').textContent()),'effect ACK after response');
    assert.equal(device.reverbPosts.length,1);
  });
  await check('old FPGA capability leaves only the reverb controls disabled and retains the keyboard',async()=>{
    const {page,device}=await open(d=>{d.effect.available=false;d.effect.valid=false;d.effect.wet_q15=d.effect.damp_q15=null;});
    await until(async()=>/不支持|未准备好/.test(await page.locator('#reverbStatus').textContent()),'unsupported hint');
    assert.equal(await page.locator('#reverbWet').isDisabled(),true);assert.equal(await page.locator('.piano-key:enabled').count(),25);assert.equal(device.reverbPosts.length,0);
    await page.locator('#reverbPanel').screenshot({path:resolve(output,'phone-unavailable.png')});
  });
  await check('a missing endpoint is explained without greying the rest of the page',async()=>{
    const {page}=await open(d=>{d.reverbMissing=true;});await until(async()=>/尚未提供/.test(await page.locator('#reverbStatus').textContent()),'old firmware hint');
    assert.equal(await page.locator('#reverbWet').isDisabled(),true);assert.equal(await page.locator('.piano-key:enabled').count(),25);
  });
  await check('an unconfirmed setting reports timeout without automatic replay or Wi-Fi disconnect',async()=>{
    const {page,device}=await open(d=>{d.noReverbAck=true;});await until(()=>page.locator('#reverbWet').isEnabled(),'reverb available');
    await input(page,'reverbWet',16000);await until(async()=>/超时/.test(await page.locator('#reverbStatus').textContent()),'reverb timeout');
    assert.equal(device.reverbPosts.length,1);assert.match(await page.locator('#connectionText').textContent(),/已连接/);assert.equal(await page.locator('.piano-key:enabled').count(),25);
  });
  await check('desktop layout preserves the instrument and exposes the shared effect panel',async()=>{
    const {page}=await open(()=>{},1440);await until(()=>page.locator('#reverbWet').isEnabled(),'reverb available');
    assert.equal(await page.locator('.piano-key').count(),25);assert.equal(await page.locator('.engine-switch button').count(),3);assert.equal(await page.locator('#additive').count(),0);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.locator('#reverbPanel').screenshot({path:resolve(output,'desktop-reverb.png')});
    assert.deepEqual(errors,[]);
  });
}finally{
  if(live){await live.context.close();await live.close();}
  await writeFile(resolve(output,'report.json'),JSON.stringify({browser:browser.version(),physical_device_verified:false,html_bytes:html.length,gzip_bytes:gzip.length,checks,errors},null,2)+'\n');
  await browser.close();
}
