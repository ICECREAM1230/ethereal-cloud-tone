import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {resolve, dirname} from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {chromium} from 'playwright';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const output=resolve(root,process.env.WEB_UI_FM_PREVIEW_ARTIFACT_DIR||'artifacts/phase2-20261011/catalog-preview/browser');
await mkdir(output,{recursive:true});
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const results=[];const errors=[];const requests=[];
const check=async(name,action)=>{await action();results.push({name,result:'PASS'});console.log('PASS '+name);};
const context=await browser.newContext({viewport:{width:320,height:844},offline:true});
const page=await context.newPage();
page.on('pageerror',error=>errors.push(error.message));
page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
page.on('request',request=>requests.push(request.url()));
try{
  await page.goto(pathToFileURL(resolve(root,'artifacts/phase2-20261011/catalog-preview/index.html')).href);
  await page.waitForFunction(()=>window.catalogPreviewReady===true);
  await check('built single HTML opens from file with browser networking offline',async()=>{
    assert.equal(await page.locator('script[src],link[rel="stylesheet"]').count(),0);
    assert(requests.every(url=>url.startsWith('file:')));
    assert.equal(await page.locator('.fm-panel').count(),1);
    assert.equal(await page.locator('[data-fm-voice-select] option').count(),8);
    assert.equal(await page.locator('#presets li').count(),3);
    assert.equal(await page.evaluate(()=>window.previewModel.editable),false);
  });
  await check('shared JT51 patch descriptors stay unread and create no parameter editors',async()=>{
    assert.equal(await page.locator('[data-fm-parameter]').count(),46);
    assert.equal(await page.locator('[data-fm-parameter] input,[data-fm-parameter] select').count(),0);
    assert((await page.locator('[data-fm-parameter] output').allTextContents()).every(value=>value==='尚未读取'));
    assert.deepEqual(await page.evaluate(()=>window.previewParameterIntents),[]);
  });
  await check('voice browsing preserves shared patch scopes and empty current values',async()=>{
    const last=await page.locator('[data-fm-voice-select] option').last().getAttribute('value');
    await page.locator('[data-fm-voice-select]').selectOption(last);
    assert.equal(await page.evaluate(()=>window.previewModel.selectedVoiceId),last);
    assert.equal(await page.locator('[data-fm-parameter][data-fm-voice]').count(),0);
    assert.equal(await page.locator('[data-fm-parameter]').count(),46);
    assert.deepEqual(await page.evaluate(()=>window.previewParameterIntents),[]);
  });
  await check('offline MSFA reference has actual two operators and no guessed parameters or presets',async()=>{
    await page.locator('#profile').selectOption('2');
    assert.equal(await page.locator('[data-fm-voice-select] option').count(),8);
    assert.equal(await page.evaluate(()=>window.previewModel.voices.find(v=>v.id===window.previewModel.selectedVoiceId).operators.length),2);
    assert.equal(await page.locator('[data-fm-parameter]').count(),0);
    assert.equal(await page.locator('#noPresets').isVisible(),true);
    assert.deepEqual(await page.evaluate(()=>window.previewParameterIntents),[]);
  });
  await check('switching catalog back never applies defaults or presets',async()=>{
    await page.locator('#profile').selectOption('1');
    assert.equal(await page.locator('[data-fm-parameter]').count(),46);
    assert.equal(await page.locator('#presets li').count(),3);
    assert.deepEqual(await page.evaluate(()=>window.previewParameterIntents),[]);
    assert(requests.every(url=>url.startsWith('file:')));
  });
  await check('320px built catalog has no page overflow or runtime errors',async()=>{
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    assert.deepEqual(errors,[]);
    await page.screenshot({path:resolve(output,'catalog-320.png'),fullPage:true});
    await page.screenshot({path:resolve(output,'catalog-320-top.png')});
    await page.locator('[data-fm-patch-operator-panel]').first().screenshot({path:resolve(output,'catalog-shared-patch-320.png')});
  });
}catch(error){results.push({name:'built catalog browser run',result:'FAIL',error:error.stack});throw error;}
finally{
  const report={recorded_at:new Date().toISOString(),scope:'read-only single HTML catalog on file URL with browser networking offline',node:process.version,playwright:JSON.parse(await readFile(resolve(root,'node_modules/playwright/package.json'),'utf8')).version,browser:browser.version(),results,javascript_errors:errors,requests,transport_bound:false,physical_device_verified:false};
  await context.close();await browser.close();
  await writeFile(resolve(output,'report.json'),JSON.stringify(report,null,2)+'\n');
}
