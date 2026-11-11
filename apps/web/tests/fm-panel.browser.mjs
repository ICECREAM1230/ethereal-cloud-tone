import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {dirname,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {chromium} from 'playwright';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const stageIndex=process.argv.indexOf('--stage');
const stage=stageIndex<0?'green':process.argv[stageIndex+1];
if(!['red','green'].includes(stage))throw Error('stage must be red or green');
const output=resolve(process.env.WEB_UI_FM_PANEL_ARTIFACT_DIR||resolve(root,'artifacts/phase2-20261011/fm-panel'));
await mkdir(output,{recursive:true});
const routes=new Map([
  ['/',{path:'tests/fixtures/fm-panel/index.html',type:'text/html; charset=utf-8'}],
  ['/fixture/harness.mjs',{path:'tests/fixtures/fm-panel/harness.mjs',type:'text/javascript; charset=utf-8'}],
  ['/fixture/models.mjs',{path:'tests/fixtures/fm-panel/models.mjs',type:'text/javascript; charset=utf-8'}],
  ['/src/fm-panel.mjs',{path:'src/fm-panel.mjs',type:'text/javascript; charset=utf-8'}],
  ['/src/style.css',{path:'src/style.css',type:'text/css; charset=utf-8'}]
]);
const server=createServer(async(req,res)=>{
  if(req.url==='/favicon.ico'){res.writeHead(204);res.end();return;}
  const resource=routes.get(req.url);
  if(req.method!=='GET'||!resource){res.writeHead(404);res.end('Not found');return;}
  try{const body=await readFile(resolve(root,resource.path));res.writeHead(200,{'Content-Type':resource.type,'Cache-Control':'no-store'});res.end(body);}
  catch(error){res.writeHead(error.code==='ENOENT'?404:500);res.end('Static fixture file unavailable');}
});
await new Promise(done=>server.listen(0,'127.0.0.1',done));
const base=`http://127.0.0.1:${server.address().port}`;
const results=[];const javascriptErrors=[];const consoleErrors=[];const requests=[];
let browser;let context;let page;
const parameter=(id,operator)=>page.locator(`[data-fm-parameter="${id}"]${operator?`[data-fm-operator="${operator}"]`:''} input, [data-fm-parameter="${id}"]${operator?`[data-fm-operator="${operator}"]`:''} select`).first();
async function mount(name,overrides){await page.evaluate(({name,overrides})=>window.mountScenario(name,overrides),{name,overrides});}
async function rendered(){assert.equal(await page.locator('[data-fm-backend]').count(),1,'real component must present the current backend');}
async function check(name,action){
  try{await action();results.push({name,result:'PASS'});console.log('PASS '+name);}
  catch(error){results.push({name,result:'FAIL',error:error.stack});console.log('FAIL '+name+'\n'+error.message);}
}
try{
  browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
  context=await browser.newContext({viewport:{width:390,height:844},deviceScaleFactor:1});
  page=await context.newPage();page.setDefaultTimeout(2000);
  page.on('pageerror',error=>javascriptErrors.push(error.message));
  page.on('console',entry=>{if(entry.type()==='error')consoleErrors.push(entry.text());});
  page.on('request',request=>requests.push({url:request.url(),method:request.method(),body:request.postData()}));
  await page.goto(base);await page.waitForFunction(()=>window.fixtureReady===true);
  for(const [scenario,count,label] of [['two',2,'本地 UI 示例 · 2 算子'],['four',4,'本地 UI 示例 · 4 算子'],['six',6,'本地 UI 示例 · 6 算子']]){
    await check(`${count}-operator layout comes from the selected model without initial writes`,async()=>{
      await mount(scenario);await rendered();
      assert.equal(await page.locator('[data-fm-backend]').textContent(),label);
      assert.equal(await page.locator('[data-fm-voice-select] option').count(),1);
      assert.equal(await page.locator('[data-fm-operator-panel]').count(),count);
      assert.equal(await parameter('algorithm').locator('option').count(),3);
      assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[]);
      await page.screenshot({path:resolve(output,`${stage}-${scenario}-390.png`),fullPage:true});
    });
  }
  await check('heterogeneous voices use their own operator lists and navigation carries logical identity',async()=>{
    await mount('heterogeneous');await rendered();
    assert.deepEqual(await page.locator('[data-fm-voice-select] option').allTextContents(),['双算子声部','六算子声部','四算子声部']);
    assert.equal(await page.locator('[data-fm-operator-panel]').count(),2);
    await page.locator('[data-fm-voice-select]').selectOption({label:'六算子声部'});
    assert.equal(await page.locator('[data-fm-operator-panel]').count(),6);
    assert.deepEqual(await page.evaluate(()=>window.selectionIntents),[{contextId:'mixed-layout/revision-a',backendId:'mixed-layout',voiceId:'voice-zeta'}]);
    assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[]);
    await page.locator('[data-fm-voice-select]').selectOption({label:'四算子声部'});
    assert.equal(await page.locator('[data-fm-operator-panel]').count(),4);
  });
  await check('number bounds, enum labels and units come from model and changes preserve value types',async()=>{
    await mount('two');await rendered();
    const ratio=parameter('ratio','modulator');
    assert.equal(await ratio.getAttribute('min'),'0.5');assert.equal(await ratio.getAttribute('max'),'8.5');assert.equal(await ratio.getAttribute('step'),'0.5');
    assert.equal(await ratio.inputValue(),'3.5');assert.equal(await page.locator('[data-fm-parameter="ratio"][data-fm-operator="modulator"] output').textContent(),'3.5 倍');
    await parameter('algorithm').selectOption({label:'串联'});
    await ratio.focus();await ratio.press('ArrowRight');
    assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[
      {contextId:'test-only-backend-2/generation-a',target:{backendId:'test-only-2',voiceId:'voice-alpha',operatorId:null},parameterId:'algorithm',value:2},
      {contextId:'test-only-backend-2/generation-a',target:{backendId:'test-only-2',voiceId:'voice-alpha',operatorId:'modulator'},parameterId:'ratio',value:4}
    ]);
    assert.equal(await parameter('algorithm').locator('option').filter({hasText:'分层'}).isDisabled(),true);
    await parameter('algorithm').evaluate(input=>{input.value='2';input.dispatchEvent(new Event('change',{bubbles:true}));});
    assert.equal(await page.evaluate(()=>window.parameterIntents.length),2,'disabled algorithm choice must not write');
    await parameter('level').focus();await parameter('level').press('ArrowRight');
    assert.deepEqual((await page.evaluate(()=>window.parameterIntents)).at(-1),{contextId:'test-only-backend-2/generation-a',target:{backendId:'test-only-2',voiceId:null,operatorId:null},parameterId:'level',value:0.7});
  });
  await check('readonly, waiting and unavailable states reject dispatched edits with visible reasons',async()=>{
    for(const [scenario,reason] of [['readonly','当前会话只有查看权限'],['waiting','等待目标状态确认'],['unavailable','后端尚未就绪']]){
      await mount(scenario);await rendered();
      assert.equal(await page.locator('[data-fm-parameter] input:enabled, [data-fm-parameter] select:enabled').count(),0);
      assert((await page.locator('#fmPanel').textContent()).includes(reason));
      await page.locator('[data-fm-parameter] input, [data-fm-parameter] select').evaluateAll(inputs=>inputs.forEach(input=>{input.dispatchEvent(new Event('input',{bubbles:true}));input.dispatchEvent(new Event('change',{bubbles:true}));}));
      assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[]);
      if(scenario==='waiting')assert.equal(await page.locator('.fm-panel').getAttribute('aria-busy'),'true');
    }
  });
  await check('voice, operator and individual parameter restrictions prevent write intents',async()=>{
    await mount('scoped');await rendered();
    assert.equal(await parameter('feedback').isDisabled(),true);
    assert.equal(await parameter('ratio','modulator').isDisabled(),true);
    assert.equal(await parameter('ratio','carrier').isDisabled(),true);
    assert.equal(await parameter('wave','carrier').isDisabled(),false);
    await page.locator('[data-fm-parameter] input:disabled, [data-fm-parameter] select:disabled').evaluateAll(inputs=>inputs.forEach(input=>{input.disabled=false;input.dispatchEvent(new Event('input',{bubbles:true}));input.dispatchEvent(new Event('change',{bubbles:true}));}));
    assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[],'captured restrictions survive a forged DOM event');
    await mount('two');await page.evaluate(()=>{window.currentModel.voices[0].editable=false;window.currentModel.voices[0].reason='voice 只有查看权限';window.refreshModel({});});
    assert.equal(await page.locator('[data-fm-parameter][data-fm-voice="voice-alpha"] input:enabled, [data-fm-parameter][data-fm-voice="voice-alpha"] select:enabled').count(),0);
    assert((await page.locator('#fmPanel').textContent()).includes('voice 只有查看权限'));
  });
  await check('backend refresh removes old editing nodes and late events cannot reuse old target',async()=>{
    await mount('six');await rendered();
    const old=parameter('ratio','modulator');await old.focus();
    await old.evaluate(input=>window.previousFmInput=input);
    await mount('two');await rendered();
    assert.equal(await page.locator('[data-fm-operator-panel]').count(),2);
    assert.equal(await page.evaluate(()=>window.previousFmInput.isConnected),false);
    await page.evaluate(()=>{window.previousFmInput.value='8';window.previousFmInput.dispatchEvent(new Event('input',{bubbles:true}));});
    assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[]);
    await parameter('ratio','modulator').press('ArrowRight');
    assert.equal((await page.evaluate(()=>window.parameterIntents)).at(-1).contextId,'test-only-backend-2/generation-a');
    assert.equal(await page.locator('[data-fm-backend]').textContent(),'本地 UI 示例 · 2 算子');
  });
  await check('same-context refresh preserves an active edit and focus without sending another change',async()=>{
    await mount('two');await rendered();await parameter('ratio','modulator').focus();await parameter('ratio','modulator').press('ArrowRight');
    await page.evaluate(()=>window.refreshModel({}));
    assert.equal(await parameter('ratio','modulator').inputValue(),'4');
    assert.equal(await parameter('ratio','modulator').evaluate(input=>document.activeElement===input),true);
    assert.equal(await page.evaluate(()=>window.parameterIntents.length),1);
  });
  await check('held slider drag survives immediate same-context renders and stops when waiting begins',async()=>{
    await mount('two');await rendered();await page.evaluate(()=>window.renderOnChange=true);
    const slider=parameter('ratio','modulator');const box=await slider.boundingBox();
    await slider.evaluate(input=>window.heldFmSlider=input);
    const y=box.y+box.height/2;
    await page.mouse.move(box.x+box.width*0.2,y);await page.mouse.down();
    try{
      await page.mouse.move(box.x+box.width*0.5,y,{steps:5});
      assert.equal(await page.evaluate(()=>window.heldFmSlider.isConnected),true,'same-context owner render must retain the native drag node');
      await page.mouse.move(box.x+box.width*0.85,y,{steps:5});
      const edits=await page.evaluate(()=>window.parameterIntents);
      assert(edits.length>=3,'continuous native drag must produce more than its initial edit');
      assert(edits.at(-1).value>6,'the same held gesture must reach the later slider position');
      assert(edits.every(edit=>edit.contextId==='test-only-backend-2/generation-a'&&edit.target.operatorId==='modulator'));
      await page.evaluate(()=>window.refreshModel({waiting:true}));
      const before=await page.evaluate(()=>window.parameterIntents.length);
      await page.mouse.move(box.x+box.width*0.95,y,{steps:3});
      assert.equal(await page.evaluate(()=>window.parameterIntents.length),before);
      assert.equal(await page.evaluate(()=>window.heldFmSlider.isConnected),false,'permission transition must invalidate the old gesture');
      assert.equal(await page.locator('[data-fm-parameter] input:enabled, [data-fm-parameter] select:enabled').count(),0);
    }finally{await page.mouse.up();}
  });
  await check('waiting or unavailable voice navigation cannot bypass model permission through a changed DOM flag',async()=>{
    for(const overrides of [{waiting:true},{available:false},{backend:{id:'mixed-layout',label:'等待后端',waiting:true}}]){
      await mount('heterogeneous',overrides);await rendered();
      assert.equal(await page.locator('[data-fm-voice-select]').isDisabled(),true);
      await page.locator('[data-fm-voice-select]').evaluate(input=>{input.disabled=false;input.value='voice-zeta';input.dispatchEvent(new Event('change',{bubbles:true}));});
      assert.deepEqual(await page.evaluate(()=>window.selectionIntents),[]);
      assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[]);
    }
  });
  await check('incomplete identities and absent or malformed edit permission fail closed',async()=>{
    for(const overrides of [{contextId:null},{contextId:0},{contextId:' '},{editable:undefined},{editable:'true'},{available:0},{waiting:0},{backend:{id:'',label:'未确认后端'}},{backend:{id:'test-only-2',label:'未确认权限',editable:'true'}}]){
      await mount('two',overrides);await rendered();
      assert.equal(await page.locator('[data-fm-parameter] input:enabled, [data-fm-parameter] select:enabled').count(),0);
      await page.locator('[data-fm-parameter] input, [data-fm-parameter] select').evaluateAll(inputs=>inputs.forEach(input=>{input.disabled=false;input.dispatchEvent(new Event('input',{bubbles:true}));input.dispatchEvent(new Event('change',{bubbles:true}));}));
      assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[]);
    }
    for(const permission of [undefined,null,'true']){
      await mount('two');await page.evaluate(permission=>{window.currentModel.voices[0].operators[0].parameters[0].editable=permission;window.refreshModel({});},permission);
      assert.equal(await parameter('ratio','modulator').isDisabled(),true);
    }
    for(const owner of ['voice','operator','parameter']){
      await mount('two');await page.evaluate(owner=>{const voice=window.currentModel.voices[0];if(owner==='voice'){voice.id='';window.currentModel.selectedVoiceId='';}else if(owner==='operator')voice.operators[0].id='';else voice.operators[0].parameters[0].id='';window.refreshModel({});},owner);
      const control=page.locator('[data-fm-operator-panel]').first().locator('input').first();
      assert.equal(await control.isDisabled(),true);
      await control.evaluate(input=>{input.disabled=false;input.dispatchEvent(new Event('input',{bubbles:true}));});
      assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[]);
    }
  });
  await check('unread and unsupported parameter values have no inferred default or writable control',async()=>{
    await mount('missing');await rendered();
    assert.equal(await page.locator('[data-fm-parameter="ratio"][data-fm-operator="modulator"] input:enabled').count(),0);
    assert((await page.locator('[data-fm-parameter="ratio"][data-fm-operator="modulator"]').textContent()).includes('尚未读取'));
    assert.equal(await page.locator('[data-fm-parameter="unknown"] input, [data-fm-parameter="unknown"] select').count(),0);
    assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[]);
    await mount('empty');await rendered();assert.equal(await page.locator('[data-fm-operator-panel]').count(),0);
    assert.equal(await page.locator('[data-fm-parameter]').count(),0);assert((await page.locator('#fmPanel').textContent()).includes('没有可用的 voice 描述'));
  });
  await check('untrusted labels remain literal text across headings, options and hints',async()=>{
    await mount('malicious');await rendered();
    const label='<img src=x onerror=window.__fmInjected=true>';
    assert.equal(await page.locator('[data-fm-backend]').textContent(),label);
    assert.equal(await page.locator('[data-fm-voice-select] option').textContent(),label);
    assert((await page.locator('[data-fm-parameter="ratio"][data-fm-operator="modulator"]').textContent()).includes(label));
    assert.equal(await page.locator('#fmPanel img, #fmPanel script').count(),0);
    assert.equal(await page.evaluate(()=>window.__fmInjected),undefined);
  });
  await check('320px layout and keyboard focus expose labelled controls with visible focus ring',async()=>{
    await page.setViewportSize({width:320,height:740});await mount('six');await rendered();
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.locator('[data-fm-voice-select]').focus();await page.keyboard.press('Tab');
    assert.equal(await parameter('algorithm').evaluate(input=>document.activeElement===input),true);
    assert.notEqual(await parameter('algorithm').evaluate(input=>getComputedStyle(input).outlineStyle),'none');
    assert.equal(await page.locator('[data-fm-parameter] input, [data-fm-parameter] select').evaluateAll(inputs=>inputs.every(input=>input.labels.length>0)),true);
    await parameter('enabled','modulator').focus();await page.keyboard.press('Space');
    assert.deepEqual((await page.evaluate(()=>window.parameterIntents)).at(-1),{contextId:'test-only-backend-6/generation-a',target:{backendId:'test-only-6',voiceId:'voice-alpha',operatorId:'modulator'},parameterId:'enabled',value:false});
    await page.screenshot({path:resolve(output,`${stage}-six-320.png`),fullPage:true});
    await mount('malicious');assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.screenshot({path:resolve(output,`${stage}-literal-labels-320.png`),fullPage:true});
  });
  await check('rendering and edits never perform a transport request',async()=>{
    const before=requests.length;await mount('two');await rendered();await parameter('ratio','modulator').press('ArrowRight');await page.evaluate(()=>window.refreshModel({waiting:true}));
    assert.equal(requests.length,before);assert.equal(requests.some(request=>request.method!=='GET'),false);
    assert.equal(requests.every(request=>request.url.startsWith(base+'/')),true);
    assert.deepEqual(javascriptErrors,[]);assert.deepEqual(consoleErrors,[]);
  });
  if(stage==='red')await page.screenshot({path:resolve(output,'red-empty-panel-320.png'),fullPage:true});
}catch(error){results.push({name:'browser infrastructure',result:'FAIL',error:error.stack});console.error(error.stack);}
finally{
  const report={recordedAt:new Date().toISOString(),stage,scope:'Pure UI on loopback static fixture; no HTTP contract, mock device, hardware or FM audio validation',physicalDeviceVerified:false,contractVerified:false,node:process.version,playwright:JSON.parse(await readFile(resolve(root,'node_modules/playwright/package.json'),'utf8')).version,browser:browser?.version(),results,javascriptErrors,consoleErrors,requests,componentImportError:page?await page.evaluate(()=>window.componentImportError).catch(()=>null):null};
  await writeFile(resolve(output,`${stage}-report.json`),JSON.stringify(report,null,2)+'\n');
  await writeFile(resolve(output,`${stage}-tests.txt`),results.map(result=>`${result.result} ${result.name}${result.error?'\n'+result.error:''}`).join('\n')+'\n');
  if(context)await context.close();if(browser)await browser.close();await new Promise(done=>server.close(done));
}
process.exitCode=results.some(result=>result.result==='FAIL')?1:0;
