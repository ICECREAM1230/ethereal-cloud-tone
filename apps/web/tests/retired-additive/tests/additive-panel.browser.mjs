import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {dirname,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {chromium} from 'playwright';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const output=resolve(process.env.WEB_UI_ADDITIVE_PANEL_ARTIFACT_DIR||resolve(root,'artifacts/additive-ui-20261019/panel'));
const stage=process.argv.includes('--red')?'red':'green';
await mkdir(output,{recursive:true});
const fixture=`<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>加法合成器组件验证</title><link rel="stylesheet" href="/src/style.css"><link rel="stylesheet" href="/src/additive-panel.css"><body><main><div id="host"></div></main><script type="module">
import {getAdditivePreset} from '/src/additive.mjs';
window.ready=(async()=>{try{
  const {mountAdditivePanel}=await import('/src/additive-panel.mjs');
  let panel;window.acceptApply=true;
  window.start=(preset='organ',overrides={},permissions={})=>{
    panel?.clear();window.applies=[];
    window.runtime={available:true,playable:true,editable:true,reason:null,contextId:'test/one',revision:1,patch:preset?getAdditivePreset(preset):null,sampleRate:{numerator:91015625,denominator:948},busy:false,applied:null,...overrides};
    window.permissions={writable:true,pending:false,...permissions};
    panel=mountAdditivePanel(document.querySelector('#host'),{onApply:(patch,contextId)=>{window.applies.push({patch,contextId});return window.acceptApply;}});
    panel.update(window.runtime,window.permissions);window.panel=panel;
  };
  window.refresh=(overrides={},permissions={})=>{Object.assign(window.runtime,overrides);Object.assign(window.permissions,permissions);panel.update(window.runtime,window.permissions);};
  window.start(null);window.mountLoaded=true;
}catch(error){window.mountError=error.message;window.mountLoaded=false;}})();
</script></html>`;
const server=createServer(async(req,res)=>{
  if(req.url==='/'){res.writeHead(200,{'Content-Type':'text/html; charset=utf-8'});res.end(fixture);return;}
  if(req.url==='/favicon.ico'){res.writeHead(204);res.end();return;}
  if(req.method!=='GET'||!/^\/src\/[a-zA-Z0-9_./-]+\.(mjs|css)$/.test(req.url)||req.url.includes('..')){res.writeHead(404);res.end();return;}
  try{const body=await readFile(resolve(root,'.'+req.url));res.writeHead(200,{'Content-Type':req.url.endsWith('.css')?'text/css':'text/javascript','Cache-Control':'no-store'});res.end(body);}
  catch{res.writeHead(404);res.end('Component unavailable');}
});
await new Promise(done=>server.listen(0,'127.0.0.1',done));
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const context=await browser.newContext({viewport:{width:390,height:844},deviceScaleFactor:1});
const page=await context.newPage();page.setDefaultTimeout(2500);
const results=[];const pageErrors=[];page.on('pageerror',error=>pageErrors.push(error.message));
async function check(name,run){await run();results.push({name,result:'PASS'});console.log('PASS '+name);}
async function input(selector,value){await page.locator(selector).evaluate((element,value)=>{element.value=String(value);element.dispatchEvent(new Event('input',{bubbles:true}));},value);}
try{
  await page.goto(`http://127.0.0.1:${server.address().port}`);await page.evaluate(()=>window.ready);
  await check('unknown device patch is empty until an explicit preset load, without an apply intent',async()=>{
    assert.equal(await page.evaluate(()=>window.mountLoaded),true,'additive panel component must exist and mount');
    assert.equal(await page.locator('#additivePreset').inputValue(),'');
    assert.equal(await page.locator('#additiveLoadPreset').isDisabled(),true);
    assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    assert.match(await page.locator('#host').textContent(),/尚未读取/);
    await page.locator('#additivePreset').selectOption('bell');
    assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    await page.locator('#additiveLoadPreset').click();
    assert.equal(await page.locator('[data-additive-weight]').count(),32);
    assert.equal(await page.locator('[data-additive-weight="0"]').inputValue(),'65535');
    assert.equal(await page.evaluate(()=>window.applies.length),0);
    assert.equal(await page.locator('#additiveApply').isEnabled(),true);
    await page.screenshot({path:resolve(output,'phone-draft.png'),fullPage:true});
  });
  await check('confirmed patch initializes controls; weight gestures only edit the local draft and survive same-context polling',async()=>{
    await page.evaluate(()=>window.start('organ'));
    assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    await page.locator('[data-additive-weight="0"]').focus();await page.keyboard.press('ArrowDown');
    const edited=await page.locator('[data-additive-weight="0"]').inputValue();assert.notEqual(edited,'65535');
    await page.evaluate(()=>window.refresh());
    assert.equal(await page.locator('[data-additive-weight="0"]').inputValue(),edited);
    assert.equal(await page.evaluate(()=>window.applies.length),0);
    assert.equal(await page.locator('#additiveApply').isEnabled(),true);
  });
  await check('partial selection edits frequency ratio, exact weight and envelope group without changing the other 31 partials',async()=>{
    await page.evaluate(()=>window.start('organ'));
    await page.locator('[data-additive-partial="5"]').click();
    await page.locator('[data-additive-ratio]').fill('2.5');
    await page.locator('#additiveExactWeight').fill('12345');
    await page.locator('#additivePartialGroup').selectOption('3');
    await page.locator('#additiveApply').click();
    const {getAdditivePreset}=await import('../src/additive.mjs');const expected=getAdditivePreset('organ');
    expected.partials[5]={ratio_q16:163840,weight:12345,group:3};
    assert.deepEqual(await page.evaluate(()=>window.applies),[{patch:expected,contextId:'test/one'}]);
    assert.match(await page.locator('#host').textContent(),/停止所有来源演奏/);
    assert.match(await page.locator('#host').textContent(),/等待设备确认/);
    assert.doesNotMatch(await page.locator('#additiveState').textContent(),/已生效/);
  });
  await check('all global and envelope controls preserve the wire values until explicitly edited, including zero-duration hold',async()=>{
    await page.evaluate(()=>window.start('soft-pad'));
    await input('[data-additive-global="master_gain"]',65535);
    await input('[data-additive-global="master_pan"]',32768);
    await input('[data-additive-global="bend_range"]',12);
    await input('[data-additive-global="centroid_q8"]',1024);
    await input('[data-additive-global="slope"]',9000);
    await input('[data-additive-global="comb_period"]',7);
    await input('[data-additive-global="comb_depth"]',12000);
    await page.locator('[data-additive-global="bypass_lowpass"]').uncheck();
    await page.locator('[data-additive-envelope="0:attack_inc"]').fill('0');
    await page.locator('[data-additive-envelope="1:release_inc"]').fill('100');
    await page.locator('[data-additive-envelope="3:sustain"]').fill('23456');
    await page.locator('#additiveApply').click();
    const {getAdditivePreset,envelopeIncrement}=await import('../src/additive.mjs');const expected=getAdditivePreset('soft-pad');
    Object.assign(expected,{master_gain:65535,master_pan:32768,bend_range:12,centroid_q8:1024,slope:9000,comb_period:7,comb_depth:12000,bypass_lowpass:false});
    expected.envelopes[0].attack_inc=0;expected.envelopes[1].release_inc=envelopeIncrement(100,{numerator:91015625,denominator:948});expected.envelopes[3].sustain=23456;
    assert.deepEqual(await page.evaluate(()=>window.applies[0].patch),expected);
  });
  await check('newer device revision preserves an edited draft and explicit reread discards it',async()=>{
    await page.evaluate(()=>window.start('organ'));
    await input('[data-additive-weight="0"]',1234);
    await page.evaluate(()=>{window.runtime.patch.partials[0].weight=2222;window.refresh({revision:2});});
    assert.equal(await page.locator('[data-additive-weight="0"]').inputValue(),'1234');
    assert.match(await page.locator('#additiveConflict').textContent(),/较新的设备值/);
    await page.locator('#additiveReload').click();
    assert.equal(await page.locator('[data-additive-weight="0"]').inputValue(),'2222');
    assert.equal(await page.locator('#additiveApply').isDisabled(),true);
  });
  await check('context changes invalidate draft and stale DOM callbacks; clear also invalidates previous callbacks',async()=>{
    await page.evaluate(()=>window.start('organ'));
    await input('[data-additive-weight="0"]',1234);
    await page.evaluate(()=>{window.staleApply=document.querySelector('#additiveApply');window.staleWeight=document.querySelector('[data-additive-weight="0"]');window.refresh({contextId:'test/two',revision:3,patch:null});window.staleApply.click();window.staleWeight.value='4567';window.staleWeight.dispatchEvent(new Event('input'));});
    assert.equal(await page.evaluate(()=>window.applies.length),0);
    assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    assert.match(await page.locator('#host').textContent(),/尚未读取/);
    await page.locator('#additivePreset').selectOption('organ');await page.locator('#additiveLoadPreset').click();
    await page.evaluate(()=>{window.staleApply=document.querySelector('#additiveApply');window.panel.clear();window.staleApply.click();});
    assert.equal(await page.locator('#host').textContent(),'');assert.equal(await page.evaluate(()=>window.applies.length),0);
  });
  await check('read-only, busy, pending and missing identity block device apply without auto-applying local presets',async()=>{
    await page.evaluate(()=>window.start(null,{}, {writable:false}));
    await page.locator('#additivePreset').selectOption('organ');await page.locator('#additiveLoadPreset').click();
    assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    await page.evaluate(()=>window.refresh({}, {writable:true,pending:true}));assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    await page.evaluate(()=>window.refresh({busy:true}, {pending:false}));assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    await page.evaluate(()=>window.refresh({busy:false,editable:false}));assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    await page.evaluate(()=>window.refresh({editable:true}));assert.equal(await page.locator('#additiveApply').isEnabled(),true);
    await page.evaluate(()=>window.refresh({contextId:null}));assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    assert.equal(await page.evaluate(()=>window.applies.length),0);
  });
  await check('submitted draft waits for a new confirmed revision; an unconfirmed request completion remains retryable',async()=>{
    await page.evaluate(()=>window.start('organ'));await input('[data-additive-weight="0"]',1234);await page.locator('#additiveApply').click();
    await page.evaluate(()=>window.refresh({}, {pending:true}));assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    await page.evaluate(()=>window.refresh({}, {pending:false}));assert.equal(await page.locator('#additiveApply').isEnabled(),true);
    await page.locator('#additiveApply').click();
    await page.evaluate(()=>{window.refresh({}, {pending:true});window.refresh({revision:2,patch:window.applies.at(-1).patch},{pending:false});});
    assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    assert.match(await page.locator('#additiveState').textContent(),/设备已确认/);
  });
  await check('pending submission freezes draft edits and resumes after confirmed state',async()=>{
    await page.evaluate(()=>window.start('organ'));await input('[data-additive-weight="0"]',1234);await page.locator('#additiveApply').click();
    await page.evaluate(()=>window.refresh({}, {pending:true}));
    for(const selector of ['[data-additive-weight="0"]','#additivePreset','#additiveLoadPreset','#additiveReload'])assert.equal(await page.locator(selector).isDisabled(),true);
    await input('[data-additive-weight="0"]',5678);
    await page.evaluate(()=>window.refresh({revision:2,patch:window.applies[0].patch},{pending:false}));
    assert.equal(await page.locator('[data-additive-weight="0"]').inputValue(),'1234','disabled pending input cannot change the submitted draft');
    assert.equal(await page.locator('#additiveApply').isDisabled(),true);
  });
  await check('invalid precise values block applying a previously valid draft until corrected',async()=>{
    await page.evaluate(()=>window.start('organ'));await input('[data-additive-weight="0"]',1234);
    await page.locator('[data-additive-ratio]').fill('256');assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    assert.equal(await page.evaluate(()=>window.applies.length),0);
    await page.locator('[data-additive-ratio]').fill('2.5');assert.equal(await page.locator('#additiveApply').isEnabled(),true);
    await page.locator('#additiveExactWeight').fill('');assert.equal(await page.locator('#additiveApply').isDisabled(),true);
    await page.locator('#additiveExactWeight').fill('1234');assert.equal(await page.locator('#additiveApply').isEnabled(),true);
  });
  await check('320, 390, 768 and 1440px layouts contain overflow within the spectrum and expose 32 labelled keyboard sliders',async()=>{
    await page.evaluate(()=>window.start('bell'));
    for(const width of [320,390,768,1440]){
      await page.setViewportSize({width,height:900});
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,`${width}px document overflow`);
      assert.equal(await page.locator('[data-additive-weight][aria-label]').count(),32);
      assert.equal(await page.locator('[data-additive-envelope]').count(),16);
      assert.equal(await page.locator('[data-additive-ratio]').isVisible(),true);
      await page.screenshot({path:resolve(output,`layout-${width}.png`),fullPage:true});
    }
    assert.deepEqual(pageErrors,[]);
  });
}catch(error){results.push({name:'additive component browser verification',result:'FAIL',error:error.stack});console.error(error.stack);process.exitCode=1;}
finally{
  const browserVersion=browser.version();await context.close();await browser.close();await new Promise(done=>server.close(done));
  await writeFile(resolve(output,`${stage}-report.json`),JSON.stringify({recordedAt:new Date().toISOString(),stage,node:process.version,playwright:'1.63.0',browser:browserVersion,results,pageErrors},null,2)+'\n');
}
