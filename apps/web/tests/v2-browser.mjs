import assert from 'node:assert/strict';
import {mkdir,writeFile,mkdtemp} from 'node:fs/promises';
import {spawn} from 'node:child_process';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {startV2Server} from '../mock/v2-server.mjs';
import {V2Device} from '../mock/v2-device.mjs';
const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'artifacts/fm-v2-runtime-20261011/v2-browser');await mkdir(output,{recursive:true});
const browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const results=[],errors=[],requests=[];let live;
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function until(fn,label,timeout=7000){const start=Date.now();while(!await fn()){if(Date.now()-start>timeout)throw Error('timeout: '+label);await wait(25);}}
async function check(name,fn){await fn();results.push({name,result:'PASS',state:live?.device.snapshot(),deviceLog:live?.device.log.slice()});console.log('PASS '+name);}
const commands=d=>d.log.filter(e=>e.stage==='received');
const keys=d=>d.snapshot().clients.reduce((mask,c)=>mask|c.keys,0);
async function open(options={},width=390){
  if(live){await live.context.close();await live.close();live=null;}
  const server=await startV2Server(options);const context=await browser.newContext({viewport:{width,height:844},hasTouch:true,isMobile:true});const page=await context.newPage();live={...server,context,page};
  page.on('pageerror',e=>errors.push(e.message));page.on('request',req=>requests.push({url:req.url(),method:req.method(),body:req.postData(),origin:server.url}));
  await page.goto(server.url);return live;
}
const playable=page=>page.waitForFunction(()=>document.querySelector('.piano-key:not(:disabled),.string-cell:not(:disabled)'));
async function pointer(page,key,type,id=77){await page.locator(`[data-key="${key}"]`).dispatchEvent(type,{pointerId:id,pointerType:'touch',button:0,bubbles:true});}
async function input(page,selector,value){await page.locator(selector).evaluate((el,v)=>{el.value=String(v);el.dispatchEvent(new Event('input',{bubbles:true}));},value);}
try{
  await open();
  await check('same product HTML has explicit FM and live 8 voice panel',async()=>{
    assert.equal(await live.page.locator('#fm').count(),1);await playable(live.page);assert.match(await live.page.locator('#voiceTag').textContent(),/FM.*8/);
    assert.equal(await live.page.locator('[data-fm-voice-select] option').count(),8);assert.equal(await live.page.locator('[data-fm-operator-panel]').count(),4);assert.equal(await live.page.locator('[data-fm-patch-operator-panel]').count(),4);
  });
  await check('unique completed cache only, no automatic parameter/preset/SELECT; voice navigation is read-only',async()=>{
    const {page,device}=live;assert.equal(await page.locator('#fmPanel input').count(),1);assert.equal(await page.locator('#fmPanel input').inputValue(),'31');assert.equal(await page.locator('#fmPanel input[type="checkbox"]').count(),0);assert.match(await page.locator('#fmPanel').textContent(),/芯片代码.*next_note/s);assert.equal(await page.locator('#fmPreset').inputValue(),'');assert.equal(await page.locator('#applyPreset').isDisabled(),true);
    for(let i=0;i<8;i++){await page.locator('[data-fm-voice-select]').selectOption('voice:'+i);assert.equal(await page.locator('[data-fm-voice-panel]').getAttribute('data-fm-voice-panel'),'voice:'+i);assert.equal(await page.locator('[data-fm-operator-panel]').count(),4);}
    assert.equal(commands(device).filter(c=>[5,7,8].includes(c.op)).length,0);await page.screenshot({path:resolve(output,'phone-FM.png'),fullPage:true});
  });
  await check('detached parameter and voice callbacks cannot write after navigation context changes',async()=>{
    const {page,device}=live;await page.evaluate(()=>{const input=document.querySelector('#fmPanel input');const select=document.querySelector('[data-fm-voice-select]');window.staleFm={input,handler:input.oninput,select,navigate:select.onchange};});await page.locator('[data-fm-voice-select]').selectOption('voice:0');const before=commands(device).length;await page.evaluate(()=>{const old=window.staleFm;old.input.value='19';old.handler({currentTarget:old.input});old.select.value='voice:3';old.navigate({currentTarget:old.select});delete window.staleFm;});await wait(100);assert.equal(await page.locator('[data-fm-voice-select]').inputValue(),'voice:0');assert.equal(commands(device).slice(before).filter(c=>[5,7,8].includes(c.op)).length,0);
  });
  await check('parameter18 target4294901760 separates received, owner, completed cache and preserves original native diagnostics',async()=>{
    const {page,device}=live;device.ownerDelay=180;device.nativeDelay=400;const before=commands(device).length;
    await input(page,'[data-fm-parameter="18"][data-fm-operator="patch-operator:0"] input',20);await until(()=>commands(device).length>before,'parameter received');
    const cmd=commands(device).at(-1);assert.deepEqual([cmd.op,cmd.a,cmd.b,cmd.c],[7,18,4294901760,20]);assert.equal(device.snapshot().fm.parameter_values[0].value_u32,31);assert(!device.log.some(e=>e.stage==='owner'&&e.sequence===cmd.sequence));
    await until(()=>device.log.some(e=>e.stage==='owner'&&e.sequence===cmd.sequence),'parameter owner');assert.equal(device.snapshot().fm.parameter_values[0].value_u32,31);assert.equal(device.snapshot().fm.completed.sequence_id,6);
    await until(()=>device.snapshot().fm.parameter_values[0].value_u32===20,'completed cache');await page.locator('.diagnostics').evaluate(el=>el.open=true);await until(async()=> (await page.locator('#nativeInfo').textContent()).includes('"sequence_id":8'),'native independent seq');
    device.state.fm.accepted={session_id:39,engine_epoch:2,sequence_id:900};await until(async()=> (await page.locator('#nativeInfo').textContent()).includes('"session_id":39'),'old native tuple preserved');assert.equal(device.state.fm.session_id,42);device.ownerDelay=25;device.nativeDelay=100;
  });
  await check('explicit preset first adopts empty, changes context/token then empty; no old touch replay',async()=>{
    const {page,device}=live;await playable(page);await pointer(page,0,'pointerdown',111);await until(()=>keys(device)===1,'preset held');const token=device.state.clients[0].token,before=device.log.length;
    await page.locator('#fmPreset').selectOption('1');assert.equal(commands(device).filter(c=>c.op===8).length,0);await page.locator('#applyPreset').click();await until(()=>commands(device).some(c=>c.op===8),'preset received');assert.equal(await page.locator('[data-key="0"]').isDisabled(),true);
    await until(()=>device.state.clients[0].token!==token&&device.state.clients[0].processed>=1,'fresh preset session');await playable(page);const log=device.log.slice(before),preset=log.findIndex(e=>e.stage==='received'&&e.op===8);assert(log.slice(0,preset).some(e=>e.stage==='owner'&&e.op===3&&e.a===0));assert(log.slice(preset).some(e=>e.stage==='owner'&&e.op===3&&e.a===0&&e.token!==token));assert.equal(keys(device),0);assert.equal(await page.locator('#fmPreset').inputValue(),'');assert.equal(device.state.fm.parameter_values.find(p=>p.parameter_id===18&&p.target_word===4294901760).value_u32,28);await pointer(page,0,'pointerup',111);assert.equal(keys(device),0);
  });
  await check('FM volume and pan use owner CC7/10 with DDS/KS sound panels hidden',async()=>{
    const {page,device}=live;assert.equal(await page.locator('#ddsPanels').isVisible(),false);assert.equal(await page.locator('#ksControls').isVisible(),false);await input(page,'#cc7',64);await until(()=>device.snapshot().legacy.volume===128,'FM volume');await input(page,'#cc10',80);await until(()=>device.snapshot().legacy.pan===16,'FM pan');
  });
  await check('DDS to FM, KS to FM and leaving FM wait for settled owner and never auto select',async()=>{
    const {page,device}=live;for(const [id,engine] of [['dds',0],['fm',2],['ks',1],['fm',2]]){await page.locator('#'+id).click();assert.equal(await page.locator('[data-key="0"]').isDisabled(),true);await until(()=>device.snapshot().mode.applied===engine&&!device.state.mode.switching,'mode '+engine);await playable(page);assert.equal(keys(device),0);assert.deepEqual(device.state.fm.parameter_values,[]);if(engine===2){assert.equal(await page.locator('#fmPanel input').count(),0);assert.match(await page.locator('#fmPanel').textContent(),/尚未读取/);}assert.equal(await page.locator('#ddsPanels').isVisible(),engine===0);assert.equal(await page.locator('#ksControls').isVisible(),engine===1);}
    const selects=commands(device).filter(e=>e.op===5);assert.deepEqual(selects.map(e=>[e.a,e.b]),[[0,0],[2,1],[1,0],[2,1]]);assert.equal(commands(device).filter(c=>c.op===7).length,1,'SELECT must not auto-fill parameters');
  });
  await check('explicit preset repopulates confirmed cache; repeated SELECT clears it and fences old DOM without default writes',async()=>{
    const {page,device}=live;await page.locator('#fmPreset').selectOption('1');await page.locator('#applyPreset').click();await playable(page);assert.equal(device.state.fm.parameter_values.length,46);assert.equal(await page.locator('#fmPanel input').count(),46);await page.evaluate(()=>{const input=document.querySelector('#fmPanel input');window.beforeSelect={input,callback:input.oninput};});const writes=commands(device).filter(c=>c.op===7).length,presets=commands(device).filter(c=>c.op===8).length;await page.locator('#fm').click();await playable(page);assert.deepEqual(device.state.fm.parameter_values,[]);assert.equal(await page.locator('#fmPanel input').count(),0);await page.evaluate(()=>{const old=window.beforeSelect;old.callback({currentTarget:old.input});delete window.beforeSelect;});await wait(100);assert.equal(commands(device).filter(c=>c.op===7).length,writes);assert.equal(commands(device).filter(c=>c.op===8).length,presets);assert.equal(await page.locator('#fmPreset').inputValue(),'');assert.match(await page.locator('#fmPanel').textContent(),/尚未读取/);
  });
  await check('musical BUSY keeps held contacts new On and safe releases while structural writes wait',async()=>{
    const {page,device}=live;await pointer(page,0,'pointerdown',120);await until(()=>keys(device)===1,'BUSY held');device.configure({busy:true});await page.waitForFunction(()=>JSON.parse(document.querySelector('#raw').textContent).fm.flags===11);await playable(page);assert.equal(keys(device),1);assert.equal(await page.locator('[data-key="0"]').getAttribute('aria-pressed'),'true');assert.equal(await page.locator('#fm').isDisabled(),true);await pointer(page,4,'pointerdown',121);await until(()=>keys(device)===17,'BUSY new On');await pointer(page,0,'pointerup',120);await until(()=>keys(device)===16,'BUSY subset Off');await pointer(page,4,'pointerup',121);await until(()=>keys(device)===0,'BUSY final Off');device.configure({busy:false});await playable(page);assert.equal(keys(device),0);
  });
  await check('external context clears touch and binds empty without replay',async()=>{
    const {page,device}=live;await pointer(page,0,'pointerdown',122);await until(()=>keys(device)===1,'external held');const token=device.state.clients[0].token;device.moveContext();await until(()=>device.state.clients[0].token!==token,'context rebind');await playable(page);await wait(350);assert.equal(keys(device),0);assert.equal(await page.locator('[data-key="0"]').getAttribute('aria-pressed'),'false');
  });
  await check('native PLL/session0 with owner recovering retains FM and requires explicit SELECT after READY MUTED',async()=>{
    const {page,device}=live;await pointer(page,0,'pointerdown',130);await until(()=>keys(device)===1,'reset held');const selects=commands(device).filter(c=>c.op===5).length;device.nativeReset();await until(async()=> (await page.locator('#engineStatus').textContent()).includes('显式重新选择'),'needs SELECT');assert.equal(device.state.fm.flags,5);assert.equal(device.state.fm.session_id,0);assert.equal(await page.locator('#fm').isDisabled(),false);assert.match(await page.locator('#voiceTag').textContent(),/FM/);assert.equal(await page.locator('[data-key="0"]').isDisabled(),true);assert.equal(commands(device).filter(c=>c.op===5).length,selects);await page.screenshot({path:resolve(output,'selection-required.png'),fullPage:true});await page.locator('#fm').click();await playable(page);assert(device.state.fm.session_id>42);assert.equal(keys(device),0);assert.equal(commands(device).at(-1).a,0);
  });
  await check('blur/pagehide release and resume empty; offline cannot resurrect old touches',async()=>{
    const {page,device}=live;for(const [hide,show] of [['blur','focus'],['pagehide','pageshow']]){await pointer(page,0,'pointerdown',140);await until(()=>keys(device)===1,'lifecycle held');await page.evaluate(name=>dispatchEvent(new Event(name)),hide);await until(()=>keys(device)===0,'lifecycle Off');await page.evaluate(name=>dispatchEvent(new Event(name)),show);await playable(page);assert.equal(keys(device),0);}
    await pointer(page,0,'pointerdown',141);await until(()=>keys(device)===1,'offline held');device.offline=true;await until(async()=> (await page.locator('#connectionText').textContent())==='未连接','offline');const oldHeld=device.state.clients.find(c=>c.keys!==0),heldToken=oldHeld.token,lastOn=commands(device).findLast(c=>c.token===heldToken&&c.op===3&&c.a!==0);const before=commands(device).length;device.offline=false;await page.evaluate(()=>dispatchEvent(new Event('online')));await playable(page);await until(()=>keys(device)===0,'old disconnected slot lease release');const expired=device.log.find(e=>e.stage==='lease-expired'&&e.token===heldToken);assert(expired&&expired.at>=lastOn.expires&&expired.at-lastOn.expires<120);results.push({name:'disconnect old slot lease timing',result:'EVIDENCE',token:heldToken,lastReceivedAt:lastOn.at,leaseDeadline:lastOn.expires,expiredAt:expired.at,elapsedMs:expired.at-lastOn.at});assert.equal(commands(device).slice(before).filter(c=>c.op===3&&c.a!==0).length,0);
  });
  await open({noFM:true});await playable(live.page);
  await check('real noFM v2 fixture keeps DDS/KS and disables FM',async()=>{const {page,device}=live;assert.equal(await page.locator('#fm').isDisabled(),true);await input(page,'#cc7',50);await until(()=>device.snapshot().legacy.volume===100,'noFM volume');await page.screenshot({path:resolve(output,'noFM-DDS.png'),fullPage:true});await page.locator('#ks').click();await playable(page);assert.equal(device.state.mode.applied,1);await input(page,'#decay',8000);await until(()=>device.snapshot().legacy.ks_decay_ms===8000,'noFM T60');await page.locator('#panic').click();await playable(page);assert.equal(device.state.fm.panic,null);});
  for(const [name,change] of [
    ['schema digest',d=>d.caps.schema_sha256='0'.repeat(64)],['profiles digest',d=>d.caps.profiles_sha256='0'.repeat(64)],['contract digest',d=>d.caps.contract_sha256='0'.repeat(64)],
    ['actual voice dimension',d=>d.state.fm.voice_count=7],['actual operator dimension',d=>d.state.fm.operators_per_voice[0]=3],['actual patch dimension',d=>d.state.fm.patch_operator_count=3],
    ['valid false',d=>d.state.fm.valid=false],['bad phase',d=>d.state.fm.phase=4],['no READY',d=>d.state.fm.flags=2],['FAULT',d=>d.state.fm.flags=19],['offline profile',d=>{d.state.fm.backend_id=2;d.state.fm.profile_id=2;}]
  ]){const d=new V2Device();d.state.mode={requested:0,applied:0,switching:false,recovering:false};d.state.legacy.ready=true;change(d);await open({device:d});await playable(live.page);await check(name+' closes FM without blocking valid DDS/KS',async()=>{assert.equal(await live.page.locator('#fm').isDisabled(),true);assert.equal(await live.page.locator('#ks').isDisabled(),false);await pointer(live.page,0,'pointerdown',160);await until(()=>keys(d)===1,'negative DDS note');await pointer(live.page,0,'pointerup',160);await until(()=>keys(d)===0,'negative DDS Off');assert.equal(commands(d).filter(c=>[5,7,8].includes(c.op)).length,0);});}
  for(const [name,flags,session] of [['READY MUTED initial',5,0],['SELECTED MUTED',7,42],['READY without SELECTED',1,42]]){
    const d=new V2Device();d.state.fm.flags=flags;d.state.fm.session_id=session;await open({device:d});await until(async()=>!await live.page.locator('#fm').isDisabled(),'initial selectable');await check(name+' is selectable but silent until explicit new selection',async()=>{assert.equal(await live.page.locator('[data-key="0"]').isDisabled(),true);assert.equal(await live.page.locator('#applyPreset').isDisabled(),true);assert.equal(commands(d).filter(c=>c.op===5).length,0);assert(!/selection_required|engine_muted/.test(await live.page.locator('#fmSound').textContent()));await live.page.locator('#fm').click();await playable(live.page);assert.equal(d.state.fm.flags,3);assert(d.state.fm.session_id>42);assert.deepEqual(d.state.fm.parameter_values,[]);assert.equal(await live.page.locator('#fmPanel input').count(),0);assert.equal(commands(d).filter(c=>[7,8].includes(c.op)).length,0);assert.equal(keys(d),0);});
  }
  const unbound=new V2Device();unbound.nativeReset();await open({device:unbound});
  await check('unbound recovery Panic preserves FM needs_select and does not invent native confirmation',async()=>{await until(async()=>!await live.page.locator('#fm').isDisabled(),'unbound selectable');const before=structuredClone(unbound.state.fm.panic),context=unbound.state.context_epoch;await live.page.locator('#panic').click();await until(()=>unbound.state.context_epoch>context,'unbound STOP owner');await until(async()=>!await live.page.locator('#fm').isDisabled(),'unbound STOP rebound');assert.deepEqual(unbound.state.mode,{requested:2,applied:0,switching:true,recovering:true});assert.deepEqual(unbound.state.fm.panic,before);assert.equal(await live.page.locator('[data-key="0"]').isDisabled(),true);assert.equal(commands(unbound).filter(c=>c.op===5).length,0);assert.match(await live.page.locator('#engineStatus').textContent(),/显式重新选择/);});
  for(const width of [320,390]){
    await open({},width);await playable(live.page);await check(width+'px actual multitouch/highest bitmap and keyboard Space, no overflow',async()=>{
      const {page,device,context}=live;assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await input(page,'#view',100);await page.locator('#keyboard').scrollIntoViewIfNeeded();const cdp=await context.newCDPSession(page);const high=await page.locator('[data-key="24"]').boundingBox(),lower=await page.locator('[data-key="21"]').boundingBox();const points=[{id:1,x:high.x+high.width/2,y:high.y+high.height-20},{id:2,x:lower.x+lower.width/2,y:lower.y+lower.height-20}];assert(points.every(p=>p.x>0&&p.x<width));await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:points});await until(()=>keys(device)===((1<<24)|(1<<21)),'actual high multitouch');await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[points[1]]});await until(()=>keys(device)===(1<<24),'high remains');await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});await until(()=>keys(device)===0,'touch released');await page.locator('[data-key="24"]').focus();await page.keyboard.down('Space');await until(()=>keys(device)===(1<<24),'high Space');await page.keyboard.up('Space');await until(()=>keys(device)===0,'Space released');await cdp.detach();await page.screenshot({path:resolve(output,width+'px-FM.png'),fullPage:true});
    });
  }

  await open();await playable(live.page);
  await check('1600ms idle followed by first new user On is accepted; stalled held lease never renews old On',async()=>{
    const {page,device}=live;await wait(1600);await pointer(page,0,'pointerdown',180);await until(()=>keys(device)===1,'idle first On');await page.evaluate(()=>{const end=performance.now()+1400;while(performance.now()<end){/* deliberate event-loop stall models background throttling */}});await until(()=>keys(device)===0,'held lease expires');await playable(page);const expiry=device.log.findLast(e=>e.stage==='lease-expired');assert(expiry);assert.equal(commands(device).filter(e=>e.at>expiry.at&&e.op===3&&e.a!==0).length,0);assert.equal(await page.locator('[data-key="0"]').getAttribute('aria-pressed'),'false');
  });
  await open();await playable(live.page);
  await check('Panic preempts pending normal receipt and fences late cache/HTTP without replay',async()=>{
    const {page,device}=live;device.receiptDelay=420;device.nativeDelay=700;const before=commands(device).length;await input(page,'#fmPanel input',20);await until(()=>commands(device).slice(before).some(c=>c.op===7),'normal received');const normal=commands(device).at(-1);await until(()=>device.log.some(e=>e.op===7&&e.stage==='native-accepted'),'native pending');await page.locator('#panic').click();await until(()=>commands(device).some(c=>c.op===4),'Panic received');const stop=commands(device).find(c=>c.op===4);assert(stop.at-normal.at<420,'Panic must not wait delayed normal receipt');device.receiptDelay=0;await playable(page);await wait(800);assert.equal(device.state.fm.parameter_values[0].value_u32,31);assert.equal(commands(device).filter(c=>c.op===7).length,1);assert(device.log.some(e=>e.stage==='native-discarded'&&e.op===7));assert.equal(keys(device),0);assert.equal(device.state.fm.panic.completed.engine_epoch,4);assert.equal(device.state.fm.engine_epoch,5);
  });
  for(const kind of ['mode prerequisite','preset prerequisite','mode native','preset native','Panic native']){
    await open();await playable(live.page);await check('Panic preempts '+kind+' without late selection/preset/On revival',async()=>{
      const {page,device}=live;const prerequisite=kind.includes('prerequisite');device.ownerDelay=prerequisite?250:25;device.nativeDelay=500;
      if(kind.startsWith('preset')){await page.locator('#fmPreset').selectOption('1');await page.locator('#applyPreset').click();}
      else if(kind.startsWith('mode'))await page.locator('#dds').click();else await page.locator('#panic').click();
      const op=kind.startsWith('preset')?8:kind.startsWith('mode')?5:4;
      if(prerequisite)await until(()=>commands(device).filter(c=>c.op===3).length>=2,'transition empty pending');else await until(()=>device.log.some(e=>e.op===op&&['native-accepted','panic-accepted'].includes(e.stage)),'native pending');
      const at=device.now(),count=commands(device).filter(c=>c.op===4).length;await page.locator('#panic').click();await until(()=>commands(device).filter(c=>c.op===4).length>count,'new Panic');const stop=commands(device).filter(c=>c.op===4).at(-1);assert(stop.at-at<500,'Panic must preempt native wait');await playable(page);await wait(650);assert.equal(keys(device),0);assert.equal(commands(device).filter(c=>c.op===3&&c.a!==0).length,0);assert.equal(commands(device).filter(c=>c.op===op).length,prerequisite?0:op===4?2:1);if(kind==='preset native')assert.equal(device.state.fm.parameter_values.length,1);
    });
  }
  await open();await playable(live.page);
  await check('received-only leased heartbeats stop on timeout and fresh binding never replays On',async()=>{
    const {page,device}=live;device.receivedOnly=true;await pointer(page,0,'pointerdown',190);
    await until(()=>commands(device).some(c=>c.op===3&&c.a===1),'received-only On');
    await until(async()=> (await page.locator('#connectionText').textContent())==='未连接','owner timeout');
    assert.match(await page.locator('#feedback').textContent(),/控制请求确认超时/);assert.match(await page.locator('#receiptInfo').textContent(),/owner adoption timeout/);
    // Repeated held bitmaps renew the lease; S3 emits MIDI from their diff.
    // They must keep one binding and unique sequences, then cease after failure.
    const held=commands(device).filter(c=>c.op===3&&c.a===1),first=held[0],boundary=commands(device).length;
    assert(held.every(c=>c.token===first.token&&c.context===first.context));
    assert(held.every((c,i)=>!i||c.sequence>held[i-1].sequence));
    device.receivedOnly=false;await page.locator('#reconnect').click();await playable(page);await wait(350);
    assert.equal(keys(device),0);
    assert.equal(commands(device).filter(c=>c.op===3&&c.a===1).length,held.length);
    const fresh=commands(device).slice(boundary);assert(fresh.length>0);
    assert(fresh.every(c=>c.op===3&&c.a===0&&c.token!==first.token));
  });
  const full=new V2Device();for(let i=0;i<4;i++)full.session('','1');await open({device:full});
  await check('four active v2 slots reject fifth browser with safe disabled keyboard',async()=>{await until(async()=> (await live.page.locator('#connectionText').textContent())==='未连接','full pool');assert.equal(await live.page.locator('[data-key="0"]').isDisabled(),true);assert.equal(commands(full).length,0);assert.match(await live.page.locator('#feedback').textContent(),/session_full/);});
  const noParameter=new V2Device();noParameter.state.fm.features&=~16;await open({device:noParameter});await playable(live.page);
  await check('parameter feature absent still permits performance volume/pan but disables FM parameter editing',async()=>{assert.equal(await live.page.locator('#fmPanel input').isDisabled(),true);assert.equal(await live.page.locator('#cc7').isDisabled(),false);await input(live.page,'#cc7',40);await until(()=>noParameter.snapshot().legacy.volume===80,'no param feature volume');});
  await check('actual hidden-page transition releases and resumes empty',async()=>{
    const server=await startV2Server();await mkdir(resolve('.cache'),{recursive:true});const profile=await mkdtemp(resolve('.cache/visibility-v2-'));const child=spawn('/usr/bin/google-chrome',['--headless=new','--no-sandbox','--remote-debugging-port=0','--remote-debugging-address=127.0.0.1','--no-first-run','--no-default-browser-check','--user-data-dir='+profile,'about:blank'],{stdio:['ignore','ignore','pipe']});let raw;
    try{const endpoint=await new Promise((done,reject)=>{let buffer='';const timer=setTimeout(()=>reject(Error('raw browser startup timeout')),5000);child.once('error',reject);child.stderr.on('data',part=>{buffer+=part;const match=buffer.match(/DevTools listening on (ws:\/\/[^\s]+)/);if(match){clearTimeout(timer);done(match[1]);}});});raw=await chromium.connectOverCDP(endpoint,{noDefaults:true});const context=raw.contexts()[0],page=context.pages()[0];page.on('pageerror',error=>errors.push(error.message));await page.setViewportSize({width:390,height:844});await page.goto(server.url);await playable(page);await pointer(page,0,'pointerdown',199);await until(()=>keys(server.device)===1,'background held');const other=await context.newPage();await other.goto('about:blank');await other.bringToFront();await page.waitForFunction(()=>document.hidden);await until(()=>keys(server.device)===0,'background release');await page.bringToFront();await page.waitForFunction(()=>!document.hidden);await playable(page);assert.equal(keys(server.device),0);results.push({name:'actual background device evidence',result:'EVIDENCE',deviceLog:server.device.log});}
    finally{if(raw)await raw.close();if(child.exitCode===null)child.kill('SIGTERM');await server.close();}
  });
  await check('single HTML and same-origin API only, no uncaught browser script errors',async()=>{assert.deepEqual(errors,[]);assert(requests.every(r=>r.url===r.origin+'/'||r.url.startsWith(r.origin+'/api/')));assert(requests.filter(r=>r.body).every(r=>/^\d+(?: \d+){6}$/.test(r.body)));});

}catch(error){results.push({name:'browser run',result:'FAIL',error:error.stack,state:live?.device.snapshot(),log:live?.device.log});throw error;}
finally{if(live){await live.context.close();await live.close();}await writeFile(resolve(output,'v2-browser-report.json'),JSON.stringify({browser:browser.version(),results,errors,requests,physical_device_verified:false},null,2));await browser.close();}
