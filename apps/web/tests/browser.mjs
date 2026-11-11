import assert from 'node:assert/strict';
import {mkdir,writeFile,mkdtemp} from 'node:fs/promises';
import {spawn} from 'node:child_process';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {chromium} from 'playwright';
import {startServer} from '../mock/server.mjs';
import {encodeCommand} from '../src/protocol.mjs';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||resolve(root,'artifacts'));await mkdir(output,{recursive:true});
const results=[];const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const wait=ms=>new Promise(done=>setTimeout(done,ms));
async function until(check,label,timeout=4000){const start=Date.now();while(!await check()){if(Date.now()-start>timeout)throw Error('timeout: '+label);await wait(20);}}
async function check(name,action){await action();results.push({name,result:'PASS'});console.log('PASS '+name);}
const requestLogs=[];const consoleErrors=[];
async function open(viewport={width:390,height:844}){
  const live=await startServer();const context=await browser.newContext({viewport,hasTouch:true,isMobile:viewport.width<700,deviceScaleFactor:1});
  const page=await context.newPage();page.on('pageerror',error=>consoleErrors.push(error.message));
  page.on('request',req=>requestLogs.push({url:req.url(),method:req.method(),body:req.postData()}));
  try{await page.goto(live.url);await until(()=>live.device.snapshot().processed[0]>=1,'initial owner bind');await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);}
  catch(error){await context.close();await live.close();throw error;}
  return {...live,context,page,shutdown:async()=>{await context.close();await live.close();}};
}
function received(device){return device.log.filter(event=>event.stage==='received');}
async function input(page,id,value){await page.locator('#'+id).evaluate((el,value)=>{el.value=String(value);el.dispatchEvent(new Event('input',{bubbles:true}));},value);}
async function pointer(page,key,type,id=77){await page.locator(`[data-key="${key}"]`).dispatchEvent(type,{pointerId:id,pointerType:'touch',button:0,bubbles:true});}
let live;
try{
  live=await open();const {page,device}=live;
  await check('phone 390px: connected, 25 accessible keys, no horizontal page overflow',async()=>{
    assert.equal(await page.locator('.piano-key').count(),25);assert.equal(await page.locator('#simulation').isVisible(),true);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    assert.equal(await page.locator('#cc7').inputValue(),'104');assert.equal(await page.locator('#cc10').inputValue(),'64');
    assert.equal(received(device).filter(e=>e.op===2||e.op===5||e.op===6).length,0);
    await page.screenshot({path:resolve(output,'phone-dds.png'),fullPage:true});await page.screenshot({path:resolve(output,'phone-perform.png')});await page.locator('#perform').screenshot({path:resolve(output,'phone-instrument.png')});
  });
  await check('real touch events: two simultaneous notes release one contact independently',async()=>{
    await page.locator('#keyboard').scrollIntoViewIfNeeded();const session=await live.context.newCDPSession(page);
    const a=await page.locator('[data-key="0"]').boundingBox();const b=await page.locator('[data-key="4"]').boundingBox();
    const points=[{x:a.x+a.width/2,y:a.y+a.height-22,id:1},{x:b.x+b.width/2,y:b.y+b.height-22,id:2}];
    await session.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:points});await until(()=>device.snapshot().keys[0]===17,'multitouch bitmap');
    await page.screenshot({path:resolve(output,'phone-multitouch.png')});
    // Installed Chrome uses the listed point ID as the contact to end.
    await session.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[points[0]]});await until(()=>device.snapshot().keys[0]===16,'one contact still held');
    await session.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});await until(()=>device.snapshot().keys[0]===0,'all touches released');await session.detach();
  });
  await check('fast tap preserves On and Off despite delayed HTTP receipt',async()=>{
    device.receiptDelay=120;const before=received(device).length;await pointer(page,7,'pointerdown',70);await pointer(page,7,'pointerup',70);
    await until(()=>received(device).slice(before).some(e=>e.op===3&&e.a===0),'tap Off');assert(received(device).slice(before).some(e=>e.op===3&&e.a===128));device.receiptDelay=0;
  });
  await check('same note with two pointers stays held until both release',async()=>{
    await pointer(page,2,'pointerdown',80);await pointer(page,2,'pointerdown',81);await until(()=>device.snapshot().keys[0]===4,'same note held');
    await pointer(page,2,'pointerup',80);await wait(80);assert.equal(device.snapshot().keys[0],4);await pointer(page,2,'pointercancel',81);await until(()=>device.snapshot().keys[0]===0,'cancel release');
  });
  await check('upper octave scroll control exposes C5 and supports highest bitmap bit',async()=>{
    await input(page,'view',100);assert((await page.locator('[data-key="24"]').boundingBox()).x<390);
    await pointer(page,24,'pointerdown',91);await until(()=>device.snapshot().keys[0]===(1<<24),'C5');await pointer(page,24,'lostpointercapture',91);await until(()=>device.snapshot().keys[0]===0,'lost capture');await input(page,'view',0);
  });
  await check('accessible focused key Space hold and keyup release',async()=>{
    await page.locator('[data-key="0"]').focus();await page.keyboard.down('Space');await until(()=>device.snapshot().keys[0]===1,'Space held');await page.keyboard.up('Space');await until(()=>device.snapshot().keys[0]===0,'Space up');
  });
  await check('editing protects focused control while other source updates patch',async()=>{
    await page.locator('#cc7').focus();await input(page,'cc7',60);await until(()=>device.snapshot().volume===120,'volume update');
    device.state.volume=40;device.state.revision++;await wait(600);assert.equal(await page.locator('#cc7').inputValue(),'60');await page.locator('#velocity').focus();await wait(50);assert.equal(await page.locator('#cc7').inputValue(),'20');
  });
  await check('KS switch gates keyboard, waits readback, shows T60 and preserves DDS patch',async()=>{
    const osc=JSON.stringify(device.snapshot().osc);device.modeDelay=300;await pointer(page,0,'pointerdown',100);await until(()=>device.snapshot().keys[0]===1,'held before mode');const before=device.log.length;
    await page.locator('#ks').click();assert.equal(await page.locator('[data-key="0"]').isDisabled(),true);await until(()=>device.snapshot().engine.applied===1&&!device.snapshot().engine.switching,'KS applied');
    const transition=device.log.slice(before);const mode=transition.findIndex(e=>e.stage==='received'&&e.op===5);const release=transition.findIndex(e=>e.stage==='owner'&&e.op===3&&e.a===0);
    assert(release>=0&&release<mode,'own empty bitmap adoption must precede the mode request');
    await page.waitForFunction(()=>document.querySelector('.string-cell:not(:disabled)'));assert.equal(device.snapshot().keys[0],0);assert.equal(await page.locator('#ddsPanels').isVisible(),false);assert.equal(await page.locator('#ksControls').isVisible(),true);
    await input(page,'decay',8000);await until(()=>device.snapshot().ks_decay_ms===8000,'T60 upper');await input(page,'decay',200);await until(()=>device.snapshot().ks_decay_ms===200,'T60 lower');
    await page.evaluate(()=>scrollTo(0,0));await page.screenshot({path:resolve(output,'phone-ks.png'),fullPage:true});
    await page.locator('#dds').click();await until(()=>device.snapshot().engine.applied===0&&!device.snapshot().engine.switching,'DDS applied');await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);assert.equal(JSON.stringify(device.snapshot().osc),osc);
  });
  await check('own release keeps another source; global panic clears sources and test tone',async()=>{
    const other=device.session('','1').body;device.command(encodeCommand(other.token,1,3,1,100),'1');await wait(50);await pointer(page,0,'pointerdown',110);await until(()=>device.snapshot().keys[0]===1,'own note');
    await page.locator('#release').click();await until(()=>device.snapshot().keys[0]===0,'own release');assert.equal(device.snapshot().keys[other.client],1);
    device.state.test_tone=true;device.externalNotes=[60];await page.locator('#panic').click();assert.equal(await page.locator('[data-key="0"]').isDisabled(),true);await until(()=>device.snapshot().panic>=3&&device.snapshot().keys.every(v=>v===0)&&!device.snapshot().test_tone,'panic');assert.deepEqual(device.externalNotes,[]);await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);
  });
  await check('blur and pagehide release instantly; resume requires new contacts',async()=>{
    await pointer(page,0,'pointerdown',120);await until(()=>device.snapshot().keys[0]===1,'blur held');await page.evaluate(()=>dispatchEvent(new Event('blur')));await until(()=>device.snapshot().keys[0]===0,'blur zero');
    assert.equal(await page.locator('[data-key="0"]').isDisabled(),true);await page.evaluate(()=>dispatchEvent(new Event('focus')));await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);await wait(100);assert.equal(device.snapshot().keys[0],0);
    await pointer(page,0,'pointerdown',121);await until(()=>device.snapshot().keys[0]===1,'pagehide held');await page.evaluate(()=>dispatchEvent(new Event('pagehide')));await until(()=>device.snapshot().keys[0]===0,'pagehide zero');await page.evaluate(()=>dispatchEvent(new Event('pageshow')));await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);
  });
  await check('recovering or missing capability disables keys/KS and clears held input',async()=>{
    await pointer(page,0,'pointerdown',130);await until(()=>device.snapshot().keys[0]===1,'recovery held');device.configure({recovering:true});await page.waitForFunction(()=>document.querySelector('[data-key="0"]').disabled);assert.equal(device.snapshot().keys[0],0);
    device.configure({recovering:false,available:false});await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);assert.equal(await page.locator('#ks').isDisabled(),true);assert.equal(device.snapshot().keys[0],0);
    await page.screenshot({path:resolve(output,'phone-dds-only.png'),fullPage:true});device.configure({available:true});
  });
  await check('network failure releases by lease, reconnect never replays held keys',async()=>{
    await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);await pointer(page,0,'pointerdown',140);await until(()=>device.snapshot().keys[0]===1,'offline held');device.offline=true;
    await page.waitForFunction(()=>document.querySelector('#connectionText').textContent==='未连接');await until(()=>device.snapshot().keys[0]===0,'lease release',2500);
    await page.evaluate(()=>scrollTo(0,0));await page.screenshot({path:resolve(output,'phone-offline.png')});device.offline=false;await page.locator('#reconnect').click();await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);await wait(200);assert.equal(device.snapshot().keys[0],0);
  });
  await check('HTTP response timeout after owner applied On does not replay the queued old notes',async()=>{
    device.receiptDelay=1100;const before=received(device).length;await pointer(page,0,'pointerdown',150);await until(()=>device.snapshot().keys[2]===1||device.snapshot().keys[0]===1,'ambiguous On applied');await pointer(page,4,'pointerdown',151);
    await page.waitForFunction(()=>document.querySelector('#connectionText').textContent==='未连接');device.receiptDelay=0;await page.locator('#reconnect').click();await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);
    assert(!received(device).slice(before).some(e=>e.op===3&&e.a===17));assert(device.snapshot().keys.every(mask=>mask===0));
  });
  await check('all runtime resources remain same origin and receipts never claim audio verification',async()=>{
    assert(requestLogs.every(req=>new URL(req.url).origin===new URL(live.url).origin));const raw=await page.locator('#raw').textContent();assert.match(raw,/"fpga": "unverified"/);
    for(const e of received(device)){assert.equal(decodeSafe(e),true);}
    assert.deepEqual(consoleErrors,[]);
  });
  await live.shutdown();live=null;
  live=await open();
  await check('release adoption timeout disables browser input without publishing a mode request',async()=>{
    live.device.ownerDelay=1000;await live.page.locator('#ks').click();
    await live.page.waitForFunction(()=>document.querySelector('#connectionText').textContent==='未连接');
    assert.equal(received(live.device).filter(e=>e.op===5).length,0);
    assert.equal(await live.page.locator('[data-key="0"]').isDisabled(),true);
    await live.page.screenshot({path:resolve(output,'mode-release-timeout.png')});
  });await live.shutdown();live=null;
  live=await open();
  await check('Panic cancels a browser mode request while release adoption is pending',async()=>{
    live.device.ownerDelay=300;const before=received(live.device).length;await live.page.locator('#ks').click();
    await until(()=>received(live.device).slice(before).some(e=>e.op===3&&e.a===0),'switch prerequisite received');
    await live.page.locator('#panic').click();await until(()=>received(live.device).slice(before).some(e=>e.op===4),'Panic received');
    assert.equal(live.device.snapshot().stop_processed[0],0);assert.equal(await live.page.locator('[data-key="0"]').isDisabled(),true);
    await until(()=>live.device.snapshot().stop_processed[0]>0,'Panic adopted');
    await live.page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);
    assert.equal(received(live.device).filter(e=>e.op===5).length,0);assert.equal(live.device.snapshot().keys[0],0);
  });await live.shutdown();live=null;
  await check('actual hidden-page transition in unforced Chrome releases and resumes empty',async()=>{
    const visibility=await startServer();await mkdir(resolve(root,'.cache'),{recursive:true});const profile=await mkdtemp(resolve(root,'.cache/visibility-'));
    const child=spawn(process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',['--headless=new','--no-sandbox','--remote-debugging-port=0','--remote-debugging-address=127.0.0.1','--no-first-run','--no-default-browser-check','--user-data-dir='+profile,'about:blank'],{stdio:['ignore','ignore','pipe']});
    let raw;
    try{
      const endpoint=await new Promise((done,reject)=>{let buffer='';const timer=setTimeout(()=>reject(Error('raw browser startup timeout')),5000);child.once('error',reject);child.stderr.on('data',part=>{buffer+=part;const match=buffer.match(/DevTools listening on (ws:\/\/[^\s]+)/);if(match){clearTimeout(timer);done(match[1]);}});});
      raw=await chromium.connectOverCDP(endpoint,{noDefaults:true});const context=raw.contexts()[0];const page=context.pages()[0];page.on('pageerror',error=>consoleErrors.push(error.message));await page.setViewportSize({width:390,height:844});await page.goto(visibility.url);await page.waitForFunction(()=>!document.querySelector('[data-key="0"]').disabled);
      await pointer(page,0,'pointerdown',160);await until(()=>visibility.device.snapshot().keys[0]===1,'hidden held');const beforeHide=visibility.device.now();
      const other=await context.newPage();await other.goto('about:blank');await other.bringToFront();await page.waitForFunction(()=>document.hidden);await until(()=>visibility.device.snapshot().keys[0]===0,'visibility release');
      assert.equal(await page.locator('#connectionText').textContent(),'演奏已暂停');assert(received(visibility.device).some(e=>e.op===3&&e.a===0&&e.received>=beforeHide&&e.received-beforeHide<500));
      await page.bringToFront();await page.waitForFunction(()=>!document.hidden&&!document.querySelector('[data-key="0"]').disabled);assert.equal(visibility.device.snapshot().keys[0],0);await other.close();assert.deepEqual(consoleErrors,[]);
    }finally{if(raw)await raw.close();if(child.exitCode===null)child.kill('SIGTERM');await visibility.close();}
  });
  live=await startServer();for(let i=0;i<4;i++)live.device.session('','1');
  live.context=await browser.newContext({viewport:{width:390,height:844},hasTouch:true,isMobile:true});live.page=await live.context.newPage();live.shutdown=async()=>{await live.context.close();await live.close();};
  await check('four occupied sessions show clear unavailable status and disabled playing',async()=>{
    await live.page.goto(live.url);await live.page.waitForFunction(()=>document.querySelector('#feedback').textContent.includes('四个控制席位'));
    assert.equal(await live.page.locator('[data-key="0"]').isDisabled(),true);assert.equal(await live.page.locator('#connectionText').textContent(),'未连接');assert.equal(received(live.device).length,0);
    await live.page.screenshot({path:resolve(output,'phone-busy.png'),fullPage:true});
  });await live.shutdown();live=null;
  for(const viewport of [{width:320,height:740},{width:768,height:1024},{width:1440,height:1000}]){
    live=await open(viewport);
    await check(`layout ${viewport.width}px: no document overflow, labelled controls, readable offline layout`,async()=>{
      assert.equal(await live.page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);assert.equal(await live.page.locator('label[for^="cc"]').count(),38);
      await live.page.screenshot({path:resolve(output,viewport.width===1440?'desktop-dds.png':`layout-${viewport.width}.png`),fullPage:true});
    });await live.shutdown();live=null;
  }
}catch(error){results.push({name:'browser run',result:'FAIL',error:error.stack,lastState:live?.device.snapshot(),recentCommands:live?.device.log.slice(-12)});throw error;}
finally{
  if(live)await live.shutdown();await browser.close();const recordedAt=new Date();
  await writeFile(resolve(output,'browser-report.json'),JSON.stringify({date:new Date(recordedAt.getTime()+9*60*60*1000).toISOString().slice(0,10),timezone:'Asia/Tokyo',recorded_at:recordedAt.toISOString(),browser:'system Chrome / Playwright 1.63.0',physical_device_verified:false,results,javascriptErrors:consoleErrors},null,2)+'\n');
}
function decodeSafe(event){return /^[0-9]+ [0-9]+ [0-9]+ [0-9]+ [0-9]+$/.test(encodeCommand(event.token,event.sequence,event.op,event.a,event.b));}
