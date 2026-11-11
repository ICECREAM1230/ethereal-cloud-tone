// Loopback-only browser check; never connects to or programs the physical device.
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {startV2Server} from '../mock/v2-server.mjs';
import {V2Device} from '../mock/v2-device.mjs';

const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'artifacts/startup-browser');
await mkdir(output,{recursive:true});
const html=await readFile(new URL('../dist/index.html',import.meta.url),'utf8');
const scripts=[...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script\s*>/gi)];
assert.equal(scripts.length,2);
const device=new V2Device();
device.state.legacy.inputs={version:1,available:true,initialized:true,enabled:true,ready:true,
  fault:0,capabilities_known:true,independent_usb:true,usb_configured:false,usb_error:false,
  stop_requested:0,stop_completed:0,stop_fault:0,stop_pending:false};
const live=await startV2Server({device});
const browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const context=await browser.newContext({viewport:{width:390,height:844}});
const page=await context.newPage(),errors=[],requests=[];
page.on('pageerror',error=>errors.push(error.message));
page.on('request',request=>requests.push(request.url()));
const results=[],observations={};
try {
  await page.route('**/prefix',route=>route.fulfill({contentType:'text/html; charset=utf-8',body:html.slice(0,scripts[1].index)}));
  await page.goto(live.url+'/prefix');
  assert.equal(await page.locator('#connectionText').textContent(),'界面资源加载中');
  await page.waitForFunction(()=>document.getElementById('connectionText').textContent==='界面尚未启动',null,{timeout:11000});
  assert.equal(await page.locator('button:enabled,input:enabled,select:enabled').count(),0);
  assert.equal(requests.filter(url=>url.includes('/api/')).length,0);
  await page.screenshot({path:resolve(output,'partial-page.png'),fullPage:true});
  results.push('partial HTML: local timeout, disabled controls and zero API requests');
  await page.goto(live.url);
  await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);
  assert.match(await page.locator('#connectionText').textContent(),/已连接/);
  assert.match(await page.locator('#inputInfo').textContent(),/演奏链路就绪.*USB 键盘未枚举/);
  await page.locator('[data-key="0"]').dispatchEvent('pointerdown',{pointerId:17,pointerType:'touch',button:0,bubbles:true});
  let until=Date.now()+3000;
  while(!device.snapshot().clients.some(client=>client.keys===1)&&Date.now()<until)await new Promise(done=>setTimeout(done,20));
  assert.ok(device.snapshot().clients.some(client=>client.keys===1));
  observations.serverGateClosedAt=device.log.length;
  device.state.legacy.inputs.ready=false;device.state.legacy.inputs.usb_configured=true;
  await page.waitForFunction(()=>document.querySelector('[data-key="0"]').disabled);
  until=Date.now()+3000;
  while(device.snapshot().clients.some(client=>client.keys)&&Date.now()<until)await new Promise(done=>setTimeout(done,20));
  assert.ok(device.snapshot().clients.every(client=>!client.keys));
  const afterRelease=device.log.length;
  observations.releaseObservedAt=afterRelease;
  assert.match(await page.locator('#connectionText').textContent(),/已连接/);
  assert.match(await page.locator('#inputInfo').textContent(),/演奏链路未就绪.*USB 键盘已枚举（不代表已发声）/);
  device.state.legacy.inputs.ready=true;
  await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);
  // A 250ms heartbeat can precede the next 500ms state observation. The
  // browser must discard held input after observing and releasing that gate.
  await new Promise(done=>setTimeout(done,300));
  assert.equal(device.log.slice(afterRelease).filter(event=>event.stage==='received'&&event.op===3&&event.a).length,0);
  device.state.legacy.inputs.ready=false;
  device.state.legacy.inputs.stop_pending=true;device.state.legacy.inputs.stop_requested=7;
  await page.waitForFunction(()=>document.getElementById('feedback').textContent.includes('FPGA'));
  assert.match(await page.locator('#feedback').textContent(),/等待 FPGA 确认全部来源停止/);
  await page.screenshot({path:resolve(output,'connected-stop-pending.png'),fullPage:true});
  assert.deepEqual(errors,[]);assert.ok(requests.every(url=>url.startsWith(live.url)));
  results.push('full HTML: connected, performance readiness, USB enumeration and pending STOP stay distinct');
} finally {
  await writeFile(resolve(output,'report.json'),JSON.stringify({browser:browser.version(),physical_device_verified:false,results,errors,requests,observations,commands:device.log.filter(event=>event.stage==='received').map(({sequence,op,a})=>({sequence,op,a}))},null,2)+'\n');
  await context.close();await browser.close();await live.close();
}
console.log(results.map(result=>'PASS '+result).join('\n'));
