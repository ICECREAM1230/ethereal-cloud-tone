import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {chromium} from 'playwright';
import {bundleModules} from '../scripts/bundle.mjs';
import {startServer} from '../mock/server.mjs';
import {startV2Server} from '../mock/v2-server.mjs';
import {V2Device} from '../mock/v2-device.mjs';
import {CONTROLS,readControl} from '../src/controls.mjs';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const output=resolve(process.env.WEB_UI_ARTIFACT_DIR||'output/playwright/control-feedback');
await mkdir(output,{recursive:true});
const artifact=process.env.WEB_UI_HTML?resolve(process.env.WEB_UI_HTML):null;

// Source bundling stays in memory; the regression never rewrites a delivered
// page and all control requests terminate in independent loopback devices.
async function sourceHtml(){
  const script=await bundleModules(resolve(root,'src'));
  return (await readFile(resolve(root,'src/index.html'),'utf8'))
    .replace('/* STYLE_SLOT */',()=>style)
    .replace('/* SCRIPT_SLOT */',()=>script.replace(/<\/script/gi,match=>'<\\/'+match.slice(2)));
}
const style=await readFile(resolve(root,'src/style.css'),'utf8');
const pageHtml=artifact?await readFile(artifact,'utf8'):await sourceHtml();
const browser=await chromium.launch({executablePath:process.env.WEB_UI_BROWSER||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
const results=[],errors=[];let live;
const wait=ms=>new Promise(done=>setTimeout(done,ms));
async function until(check,label,timeout=5000){
  const start=Date.now();while(!await check()){
    if(Date.now()-start>timeout)throw Error('timeout: '+label);
    await wait(20);
  }
}
async function open(major,mode){
  if(live){await live.context.close();await live.close();}
  let server;
  if(major===1)server=await startServer();
  else{
    const device=new V2Device({ownerDelay:100});
    if(mode!==2){device.state.mode={requested:mode,applied:mode,switching:false,recovering:false};device.state.legacy.ready=true;}
    server=await startV2Server({device});
  }
  const context=await browser.newContext({viewport:{width:390,height:844},hasTouch:true,isMobile:true,serviceWorkers:'block'});
  const page=await context.newPage();live={...server,context,page};
  page.on('pageerror',error=>errors.push(error.message));
  await page.route(server.url+'/',route=>route.fulfill({status:200,contentType:'text/html; charset=utf-8',body:pageHtml}));
  await page.goto(server.url);
  await until(async()=>!await page.locator('#cc7').isDisabled(),'ready master controls');
  return page;
}
async function check(name,action){
  try{await action();results.push({name,result:'PASS'});console.log('PASS '+name);}
  catch(error){results.push({name,result:'FAIL',error:error.stack});console.error('FAIL '+name+': '+error.message);}
}
try{
  for(const [major,mode] of [[1,0],[2,0],[2,1],[2,2]])for(const cc of [7,10]){
    await check(`v${major} mode${mode} CC${cc} confirms label without losing slider focus`,async()=>{
      const page=await open(major,mode),input=page.locator('#cc'+cc),label=page.locator(`output[for="cc${cc}"]`);
      const value=Number(await input.inputValue())-1;
      await input.focus();await input.press('ArrowLeft');
      await until(async()=>{
        const state=JSON.parse(await page.locator('#raw').textContent());
        return readControl(CONTROLS.find(c=>c.cc===cc),state.legacy||state).value===value&&
          !/正在采用调整/.test(await page.locator('#soundNote').textContent());
      },'owner confirmed the value and pending request cleared');
      assert.equal(await input.evaluate(el=>el===document.activeElement),true,'no extra click or blur is used');
      const state=JSON.parse(await page.locator('#raw').textContent());
      const expected=readControl(CONTROLS.find(c=>c.cc===cc),state.legacy||state).text;
      const actual=await label.textContent();
      await page.screenshot({path:resolve(output,`v${major}-mode${mode}-cc${cc}.png`)});
      assert.equal(actual,expected,'confirmed feedback must replace adjusting text while focus remains');
      assert.equal(await input.inputValue(),String(value));
    });
  }
  await check('focused slider position is protected from another source while confirmed feedback stays current',async()=>{
    const page=await open(2,2),input=page.locator('#cc7');
    await input.focus();await input.press('ArrowLeft');const selected=await input.inputValue();
    await until(async()=>! /正在采用调整/.test(await page.locator('#soundNote').textContent()),'adopted own adjustment');
    live.device.state.legacy.volume=40;live.device.state.legacy.revision++;
    await until(async()=>JSON.parse(await page.locator('#raw').textContent()).legacy.volume===40,'external volume observed');
    assert.equal(await input.inputValue(),selected,'refresh cannot move a focused slider');
    assert.equal(await page.locator('output[for="cc7"]').textContent(),'16%');
    await page.locator('#velocity').focus();
    assert.equal(await input.inputValue(),'20','blur reconciles the protected thumb with current owner state');
  });
  await check('no uncaught browser errors',async()=>assert.deepEqual(errors,[]));
}finally{
  if(live){await live.context.close();await live.close();}
  await writeFile(resolve(output,'control-feedback-report.json'),JSON.stringify({
    browser:browser.version(),physical_device_verified:false,html_path:artifact,
    html_bytes:Buffer.byteLength(pageHtml),html_sha256:createHash('sha256').update(pageHtml).digest('hex'),results,errors
  },null,2)+'\n');
  await browser.close();
}
if(results.some(result=>result.result==='FAIL'))process.exitCode=1;
