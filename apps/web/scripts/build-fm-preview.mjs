import {readFile, writeFile, mkdir, mkdtemp, cp} from 'node:fs/promises';
import {resolve, dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {bundleModules} from './bundle.mjs';

// A separate, read-only catalog artifact. The product app and its dist stay on
// the frozen DDS/KS HTTP v1 interface until the FM transport is handed over.
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const output = resolve(root, 'artifacts/phase2-20261011/catalog-preview');
await mkdir(resolve(root, '.cache'), {recursive:true});
const temporary = await mkdtemp(resolve(root, '.cache/catalog-preview-'));
const sources = resolve(temporary, 'src');
await cp(resolve(root, 'src'), sources, {recursive:true});
const entry = `import {createFmCatalogPreview} from './fm-catalog.mjs';
import {renderFmPanel} from './fm-panel.mjs';
import {FM_PROFILE_IDS,FM_SCHEMA_VERSION,getProfile} from './generated/fm-contract.mjs';
const container=document.getElementById('fmPanel');
const selector=document.getElementById('profile');
let profileId=FM_PROFILE_IDS[0];
let selectedVoiceIndex=0;
let model;
window.previewParameterIntents=[];
for(const id of FM_PROFILE_IDS){
  const option=document.createElement('option');
  option.value=String(id);option.textContent=getProfile(id).key;selector.append(option);
}
const handlers={
  onParameterChange:intent=>window.previewParameterIntents.push(intent),
  onVoiceSelect:intent=>{
    const index=model.voices.findIndex(voice=>voice.id===intent.voiceId);
    if(index<0)return;
    selectedVoiceIndex=index;mount();
  }
};
function mount(){
  model=createFmCatalogPreview(profileId,selectedVoiceIndex);
  renderFmPanel(container,model,handlers);
  const presets=document.getElementById('presets');presets.replaceChildren();
  for(const preset of getProfile(profileId).presets){
    const item=document.createElement('li');
    item.textContent=preset.name+' · '+preset.description;presets.append(item);
  }
  document.getElementById('noPresets').hidden=presets.children.length>0;
  document.getElementById('catalogVersion').textContent='目录版本 '+FM_SCHEMA_VERSION+' · 只读资料';
  window.previewModel=model;
}
selector.addEventListener('change',()=>{
  const id=Number(selector.value);if(!FM_PROFILE_IDS.includes(id))return;
  profileId=id;selectedVoiceIndex=0;mount();
});
mount();
window.catalogPreviewReady=true;
`;
await writeFile(resolve(sources, 'fm-preview.mjs'), entry);
const script = await bundleModules(sources, 'fm-preview.mjs');
const css = await readFile(resolve(root, 'src/style.css'), 'utf8');
const html = `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>声场 · FM 只读目录</title><style>${css}</style></head>
<body><main>
<header class="topbar"><a class="brand" href="#catalog"><span class="brand-mark">≈</span><span>声场<small>FM CATALOG PREVIEW</small></span></a><span class="status-pill">离线目录</span></header>
<p class="simulation-banner">只读目录预览 · 参数尚未从设备读取 · FM 演奏未启用</p>
<section class="intro"><div><p class="eyebrow">SHARED PATCH · VOICE TOPOLOGY</p><h1>查看音色，<span>保持只读。</span></h1><p>共享音色与声部算子分别呈现。预置仅列为资料，不能代替设备当前值。</p></div></section>
<div class="control" id="catalog"><label for="profile">查看后端目录<output id="catalogVersion"></output></label><select id="profile"></select></div>
<div id="fmPanel"></div>
<section class="connection-guide"><h2>目录中的预置</h2><ul id="presets" style="overflow-wrap:anywhere"></ul><p id="noPresets" class="guide-foot" hidden>该目录未提供预置。</p><p class="guide-foot">实时参数和编辑权限需要匹配的设备能力与正式 HTTP 接口。这份页面只浏览本地目录。</p></section>
</main><script>${script.replace(/<\/script/gi,match=>'<\\/'+match.slice(2))}</script></body></html>`;
if (/<script[^>]+src=|<link[^>]+(?:stylesheet|preload)|url\(https?:/i.test(html)) throw Error('offline preview resource invariant failed');
await mkdir(output, {recursive:true});
await writeFile(resolve(output, 'index.html'), html);
await writeFile(resolve(output, 'entry.mjs'), entry);
const inputPaths = ['scripts/build-fm-preview.mjs','scripts/bundle.mjs','src/fm-catalog.mjs','src/fm-panel.mjs','src/generated/fm-contract.mjs','src/style.css'];
const inputs = [];
for (const path of inputPaths) {
  const bytes = await readFile(resolve(root,path));
  inputs.push({path,bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')});
}
const sha256 = createHash('sha256').update(html).digest('hex');
await writeFile(resolve(output,'build-report.json'),JSON.stringify({recorded_at:new Date().toISOString(),html_bytes:Buffer.byteLength(html),sha256,inputs,external_runtime_resources:0,product_bound:false,transport_bound:false,physical_device_verified:false},null,2)+'\n');
console.log(`Read-only single HTML catalog: ${Buffer.byteLength(html)} bytes; SHA256 ${sha256}`);
