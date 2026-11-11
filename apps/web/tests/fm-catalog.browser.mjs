import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {dirname,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {chromium} from 'playwright';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const index=process.argv.indexOf('--stage');const stage=index<0?'green':process.argv[index+1];
if(!['red','green'].includes(stage))throw Error('stage must be red or green');
const run=`${stage}-${new Date().toISOString().replace(/[:.]/g,'-')}-${process.pid}`;
const output=resolve(process.env.WEB_UI_FM_CATALOG_ARTIFACT_DIR||resolve(root,'artifacts/phase2-20261011/fm-catalog'),run);
await mkdir(output,{recursive:true});
const routes=new Map([['/','tests/fixtures/fm-catalog/index.html'],['/fixture/harness.mjs','tests/fixtures/fm-catalog/harness.mjs'],['/fixture/ui-models.mjs','tests/fixtures/fm-catalog/ui-models.mjs'],['/src/fm-panel.mjs','src/fm-panel.mjs'],['/src/fm-catalog.mjs','src/fm-catalog.mjs'],['/src/generated/fm-contract.mjs','src/generated/fm-contract.mjs'],['/src/style.css','src/style.css']]);
const httpFixture=process.env.WEB_UI_FM_HTTP_FIXTURE_DIR;
if(httpFixture){
  for(const name of ['fm-runtime-view','v2-protocol','protocol','cpu-mode'])routes.set(`/src/${name}.mjs`,`src/${name}.mjs`);
  for(const name of ['capabilities','state'])routes.set(`/fixture/${name}.json`,resolve(httpFixture,`${name}.json`));
}
const server=createServer(async(request,response)=>{
  if(request.url==='/favicon.ico'){response.writeHead(204);response.end();return;}
  const file=routes.get(request.url);
  if(request.method!=='GET'||!file){response.writeHead(404);response.end('Not found');return;}
  try{const body=await readFile(resolve(root,file));response.writeHead(200,{'Content-Type':file.endsWith('.mjs')?'text/javascript; charset=utf-8':file.endsWith('.css')?'text/css; charset=utf-8':'text/html; charset=utf-8','Cache-Control':'no-store'});response.end(body);}
  catch(error){response.writeHead(error.code==='ENOENT'?404:500);response.end('Static fixture unavailable');}
});
await new Promise(done=>server.listen(0,'127.0.0.1',done));
const base=`http://127.0.0.1:${server.address().port}`;const results=[];const javascriptErrors=[];const consoleErrors=[];const requests=[];
let browser;let context;let page;
const patchControl=()=>page.locator('[data-fm-patch-operator-panel="patch-modulator"] [data-fm-parameter="amount"] input');
async function rendered(){assert.equal(await page.locator('[data-fm-backend]').count(),1,'real catalog component must render');}
async function check(name,action){try{await action();results.push({name,result:'PASS'});console.log('PASS '+name);}catch(error){results.push({name,result:'FAIL',error:error.stack});console.log('FAIL '+name+'\n'+error.message);}}
try{
  browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
  context=await browser.newContext({viewport:{width:390,height:844},deviceScaleFactor:1});page=await context.newPage();page.setDefaultTimeout(2000);
  page.on('pageerror',error=>javascriptErrors.push(error.message));page.on('console',entry=>{if(entry.type()==='error')consoleErrors.push(entry.text());});
  page.on('request',request=>requests.push({url:request.url(),method:request.method(),body:request.postData()}));
  await page.goto(base);await page.waitForFunction(()=>window.fixtureReady===true);
  await check('JT51 displays eight voices, four shared patch operators and all 46 unread parameter instances',async()=>{
    await page.evaluate(()=>window.mountCatalog(1,0));await rendered();
    assert.equal(await page.locator('[data-fm-voice-select] option').count(),8);
    assert.equal(await page.locator('[data-fm-operator-panel]').count(),4);
    assert.equal(await page.locator('[data-fm-patch-operator-panel]').count(),4);
    assert.equal(await page.locator('[data-fm-parameter]').count(),46);
    assert.equal(await page.locator('[data-fm-operator-panel] [data-fm-parameter]').count(),0);
    assert.equal(await page.locator('[data-fm-common-panel] h3').textContent(),'共享音色（Patch）');
    assert.equal(await page.locator('[data-fm-patch-operator-panel] [data-fm-voice]').count(),0);
    assert.equal(await page.locator('[data-fm-parameter] output').evaluateAll(outputs=>outputs.every(output=>output.textContent==='尚未读取')),true);
    assert.equal(await page.locator('#presets li').count(),5);
    assert.match(await page.locator('#presets').textContent(),/electric_piano_tine/);
    assert.match(await page.locator('#presets').textContent(),/metal_bell/);
    await page.screenshot({path:resolve(output,'jt51-catalog-390.png'),fullPage:true});
  });
  await check('catalogs offer no parameter edit or preset action and voice changes do not copy patch values',async()=>{
    await page.evaluate(()=>window.mountCatalog(1,0));await rendered();
    await page.locator('[data-fm-voice-select]').selectOption('voice:7');
    assert.equal(await page.locator('[data-fm-voice-select]').inputValue(),'voice:7');
    assert.equal(await page.locator('[data-fm-parameter] input:enabled, [data-fm-parameter] select:enabled').count(),0);
    assert.equal(await page.locator('#presets button, #presets input, #presets select').count(),0);
    assert.equal(await page.locator('[data-fm-patch-operator-panel]').count(),4);
    assert.equal(await page.locator('[data-fm-patch-operator-panel] [data-fm-voice]').count(),0);
    assert.equal(await page.evaluate(()=>window.currentModel.patchOperators.every(operator=>operator.parameters.every(parameter=>parameter.value===null&&parameter.editable===false))),true);
    assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[]);
  });
  await check('MSFA reference uses its own two-operator layout and provides no parameter values',async()=>{
    await page.evaluate(()=>window.mountCatalog(2,0));await rendered();
    assert.equal(await page.locator('[data-fm-voice-select] option').count(),8);assert.equal(await page.locator('[data-fm-operator-panel]').count(),2);
    assert.equal(await page.locator('[data-fm-patch-operator-panel]').count(),2);assert.equal(await page.locator('[data-fm-parameter]').count(),0);
    assert.equal(await page.locator('#presets li').count(),0);assert((await page.locator('#fmPanel').textContent()).includes('离线参考'));
    assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[]);
  });
  await check('artificial patch-two and voice-six controls keep separate counts, ranges and callback scopes',async()=>{
    await page.evaluate(()=>window.mountArtificial());
    assert.equal(await page.locator('[data-fm-patch-operator-panel]').count(),2,'patch groups must exist independently of voice groups');
    assert.equal(await page.locator('[data-fm-operator-panel]').count(),6);
    assert.equal(await patchControl().getAttribute('min'),'0');assert.equal(await patchControl().getAttribute('max'),'12');assert.equal(await patchControl().getAttribute('step'),'1');
    await patchControl().press('ArrowRight');
    await page.locator('[data-fm-operator-panel="voice-op-0"] [data-fm-parameter="ratio"] input').press('ArrowRight');
    assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[
      {contextId:'artificial-ui/patch-two-voice-six/a',target:{backendId:'artificial-ui',voiceId:null,operatorId:'patch-modulator'},parameterId:'amount',value:4},
      {contextId:'artificial-ui/patch-two-voice-six/a',target:{backendId:'artificial-ui',voiceId:'voice-six',operatorId:'voice-op-0'},parameterId:'ratio',value:4}
    ]);
    await page.locator('[data-fm-voice-select]').selectOption('voice-two');
    assert.equal(await page.locator('[data-fm-operator-panel]').count(),2);assert.equal(await page.locator('[data-fm-patch-operator-panel]').count(),2);
    await patchControl().press('ArrowRight');
    assert.equal((await page.evaluate(()=>window.parameterIntents)).at(-1).target.voiceId,null);
    await page.locator('[data-fm-common-panel] input').press('ArrowRight');
    assert.deepEqual((await page.evaluate(()=>window.parameterIntents)).at(-1).target,{backendId:'artificial-ui',voiceId:null,operatorId:null});
  });
  await check('shared patch permission and changed patch layout reject old edits without affecting voice layout',async()=>{
    await page.evaluate(()=>{window.mountArtificial();window.currentModel.patchOperators[0].editable=false;window.currentModel.patchOperators[0].reason='共享算子只读';window.refreshArtificial({});});
    assert.equal(await page.locator('[data-fm-patch-operator-panel]').count(),2);
    assert.equal(await patchControl().isDisabled(),true);
    await patchControl().evaluate(input=>{input.disabled=false;input.dispatchEvent(new Event('input',{bubbles:true}));});
    assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[]);
    assert.equal(await page.locator('[data-fm-operator-panel] input:enabled').count(),6);
    await page.evaluate(()=>window.mountArtificial());await patchControl().evaluate(input=>window.oldPatchInput=input);
    await page.evaluate(()=>window.refreshArtificial({patchOperators:window.currentModel.patchOperators.slice(0,1)}));
    assert.equal(await page.locator('[data-fm-patch-operator-panel]').count(),1);assert.equal(await page.locator('[data-fm-operator-panel]').count(),6);
    assert.equal(await page.evaluate(()=>window.oldPatchInput.isConnected),false);
    await page.evaluate(()=>window.oldPatchInput.dispatchEvent(new Event('input',{bubbles:true})));assert.deepEqual(await page.evaluate(()=>window.parameterIntents),[]);
  });
  await check('shared slider survives callback redraw, and commonLabel remains literal text at 320px',async()=>{
    await page.setViewportSize({width:320,height:740});await page.evaluate(()=>{window.mountArtificial();window.redrawOnChange=true;});
    assert.equal(await page.locator('[data-fm-patch-operator-panel]').count(),2);
    await patchControl().scrollIntoViewIfNeeded();
    const box=await patchControl().boundingBox();await patchControl().evaluate(input=>window.heldPatch=input);const y=box.y+box.height/2;
    const hit=await page.evaluate(({x,y})=>document.elementFromPoint(x,y)?.outerHTML,{x:box.x+box.width*0.15,y});
    await page.screenshot({path:resolve(output,'held-drag-before-320.png')});
    await page.mouse.move(box.x+box.width*0.15,y);await page.mouse.down();
    try{await page.mouse.move(box.x+box.width*0.8,y,{steps:8});assert.equal(await page.evaluate(()=>window.heldPatch.isConnected),true);const intents=await page.evaluate(()=>window.parameterIntents);assert(intents.length>=3,JSON.stringify({box,viewport:page.viewportSize(),hit,intents}));}finally{await page.mouse.up();}
    await page.evaluate(()=>window.refreshArtificial({commonLabel:'<img src=x onerror=window.catalogInjected=true>'}));
    assert.equal(await page.locator('[data-fm-common-panel] h3').textContent(),'<img src=x onerror=window.catalogInjected=true>');
    assert.equal(await page.locator('#fmPanel img, #fmPanel script').count(),0);assert.equal(await page.evaluate(()=>window.catalogInjected),undefined);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await patchControl().focus();await patchControl().press('ArrowRight');assert.notEqual(await patchControl().evaluate(input=>getComputedStyle(input).outlineStyle),'none');
    await page.screenshot({path:resolve(output,'artificial-patch2-voice6-320.png'),fullPage:true});
    await page.evaluate(()=>window.mountCatalog(1,0));await rendered();assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.screenshot({path:resolve(output,'jt51-catalog-320.png'),fullPage:true});
  });
  await check('actual component catalog flow emits no transport request and has no browser errors',async()=>{
    await page.evaluate(()=>window.mountCatalog(1,0));await rendered();const before=requests.length;
    await page.evaluate(()=>window.mountCatalog(1,7));await page.evaluate(()=>window.mountCatalog(2,0));
    assert.equal(requests.length,before);assert.equal(requests.every(request=>request.method==='GET'&&request.url.startsWith(base+'/')),true);
    assert.deepEqual(javascriptErrors,[]);assert.deepEqual(consoleErrors,[]);
  });
  if(httpFixture)await check('actual AE350 HTTP descriptor enables 46 shared editors and resolves an operator intent without direct voice features',async()=>{
    await page.evaluate(async()=>{
      const {createFmRuntimeView,resolveFmParameterIntent}=await import('/src/fm-runtime-view.mjs');
      const {renderFmPanel}=await import('/src/fm-panel.mjs');
      const caps=await(await fetch('/fixture/capabilities.json')).json();
      const state=await(await fetch('/fixture/state.json')).json();
      const view=createFmRuntimeView(caps,state,{writable:true,sessionIdentity:'host-http-cpu-fmc1'});
      if(!view.model)throw Error(view.reason);
      window.cpuRuntimeView=view;window.cpuParameterIntents=[];
      renderFmPanel(document.getElementById('fmPanel'),view.model,{onParameterChange:e=>{
        const command=resolveFmParameterIntent(caps,state,view,e);if(command)window.cpuParameterIntents.push(command);
      }});
      state.cpu_mode.fault=true;
      window.cpuFaultBlocked=createFmRuntimeView(caps,state,{writable:true}).model===null;
      state.cpu_mode.fault=false;
    });
    assert.equal(await page.locator('[data-fm-parameter] input:enabled').count(),46);
    assert.deepEqual(await page.evaluate(()=>window.cpuRuntimeView.presets.map(p=>p.id)),[0,1,2,3,4]);
    const input=page.locator('[data-fm-patch-operator-panel="patch-operator:0"] [data-fm-parameter="17"] input');
    const old=Number(await input.inputValue());const next=old<127?old+1:old-1;
    await input.press(old<127?'ArrowRight':'ArrowLeft');
    assert.deepEqual(await page.evaluate(()=>window.cpuParameterIntents),[{parameterId:17,targetWord:4294901760,valueU32:next}]);
    assert.equal(await page.evaluate(()=>window.cpuFaultBlocked),true);
    assert.deepEqual(javascriptErrors,[]);assert.deepEqual(consoleErrors,[]);
    await page.locator('[data-fm-patch-operator-panel]').first().screenshot({path:resolve(output,'ae350-shared-editors-320.png')});
  });
  if(stage==='red')await page.screenshot({path:resolve(output,'red-component.png'),fullPage:true});
}catch(error){results.push({name:'browser infrastructure',result:'FAIL',error:error.stack});console.error(error.stack);}
finally{
  const report={recordedAt:new Date().toISOString(),stage,run,scope:'Catalog/artificial UI and optional actual C-handler AE350 JSON fixtures on loopback; no socket-to-SPI/device/audio validation',httpFixture:httpFixture||null,node:process.version,playwright:JSON.parse(await readFile(resolve(root,'node_modules/playwright/package.json'),'utf8')).version,browser:browser?.version(),physicalDeviceVerified:false,httpVerified:false,results,javascriptErrors,consoleErrors,requests,catalogImportError:page?await page.evaluate(()=>window.catalogImportError).catch(()=>null):null};
  await writeFile(resolve(output,'report.json'),JSON.stringify(report,null,2)+'\n');await writeFile(resolve(output,'tests.txt'),results.map(result=>`${result.result} ${result.name}${result.error?'\n'+result.error:''}`).join('\n')+'\n');
  if(context)await context.close();if(browser)await browser.close();await new Promise(done=>server.close(done));console.log('Report '+resolve(output,'report.json'));
}
process.exitCode=results.some(result=>result.result==='FAIL')?1:0;
