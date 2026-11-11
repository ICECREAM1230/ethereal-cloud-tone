import test from 'node:test';
import assert from 'node:assert/strict';
import {cp,mkdir,mkdtemp,readFile,rm,symlink,writeFile,access} from 'node:fs/promises';
import {resolve,dirname,join} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {tmpdir} from 'node:os';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {Script} from 'node:vm';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const execute=promisify(execFile);

async function buildRoot(t,{fixture,files={},current=false}={}) {
  const directory=await mkdtemp(join(tmpdir(),'web-bundle-test-'));
  t.after(()=>rm(directory,{recursive:true,force:true}));
  await cp(join(root,'scripts'),join(directory,'scripts'),{recursive:true});
  await symlink(join(root,'node_modules'),join(directory,'node_modules'),'dir');
  await mkdir(join(directory,'src'),{recursive:true});
  await mkdir(join(directory,'docs'),{recursive:true});
  for(const name of ['交接来源.json','固件容量基线.json'])await cp(join(root,'docs',name),join(directory,'docs',name));
  if(current)await cp(join(root,'src'),join(directory,'src'),{recursive:true});
  else {
    // These unused modules let the old fixed-list builder reach the real regression.
    const defaults={
      'src/protocol.mjs':'export const value = 1;\n',
      'src/controls.mjs':'', 'src/controller.mjs':'', 'src/keyboard.mjs':'',
      'src/app.mjs':'globalThis.result = 1;\n',
      'src/style.css':'body { color: #222; }\n',
      'src/index.html':'<!doctype html><html><head><style>/* STYLE_SLOT */</style></head><body><script>/* SCRIPT_SLOT */</script></body></html>\n',
    };
    for(const [name,source] of Object.entries(defaults))await writeFile(join(directory,name),source);
    if(fixture)await cp(join(root,'tests/fixtures/build',fixture),join(directory,'src'),{recursive:true});
  }
  for(const [name,source] of Object.entries(files)) {
    const path=join(directory,name);await mkdir(dirname(path),{recursive:true});await writeFile(path,source);
  }
  return directory;
}

async function runBuild(directory,resourceReport) {
  const env={...process.env};delete env.WEB_UI_RESOURCE_REPORT;
  if(resourceReport!==undefined)env.WEB_UI_RESOURCE_REPORT=resourceReport;
  try {
    const {stdout,stderr}=await execute(process.execPath,[join(directory,'scripts/build.mjs')],{cwd:directory,env});
    return {status:0,stdout,stderr};
  }catch(error) { return {status:error.code,stdout:error.stdout,stderr:error.stderr}; }
}

async function inlineScript(directory) {
  const html=await readFile(join(directory,'dist/index.html'),'utf8');
  const scripts=[...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script\s*>/gi)];
  assert.ok(scripts.length>=1&&scripts.length<=2,'offline startup script plus main program');
  assert.doesNotMatch(html,/<script\b[^>]*\bsrc\s*=|<link\b[^>]*\brel\s*=\s*["']?(?:stylesheet|preload)|url\(\s*["']?https?:/i);
  return new Script(scripts.map(script=>script[1]).join('\n'),{filename:'built-index.html'});
}

async function buildAndRun(t,options,context={}) {
  const directory=await buildRoot(t,options);
  const result=await runBuild(directory);assert.equal(result.status,0,result.stderr);
  (await inlineScript(directory)).runInNewContext(context,{timeout:1000});
  return {directory,context};
}

async function nativeResult(directory) {
  const code=`await import(${JSON.stringify(pathToFileURL(join(directory,'src/app.mjs')).href)}); await globalThis.finished; process.stdout.write(JSON.stringify(globalThis.result));`;
  try {
    const {stdout,stderr}=await execute(process.execPath,['--input-type=module','--eval',code],{cwd:directory});
    return {status:0,value:JSON.parse(stdout),stderr};
  }catch(error) { return {status:error.code,stderr:error.stderr}; }
}

async function compareNative(t,files,expected) {
  const directory=await buildRoot(t,{files});const native=await nativeResult(directory);
  assert.equal(native.status,0,native.stderr);assert.deepEqual(native.value,expected);
  const built=await runBuild(directory);assert.equal(built.status,0,built.stderr);
  const context={};(await inlineScript(directory)).runInNewContext(context,{timeout:1000});
  assert.deepEqual(JSON.parse(JSON.stringify(context.result)),expected);
  return directory;
}

test('review: imports after consumers are initialized before module execution like native ESM',async t=>{
  await compareNative(t,{
    'src/app.mjs':'globalThis.result=VALUE; import {VALUE} from "./dep.mjs";\n',
    'src/dep.mjs':'export const VALUE=42;\n',
  },42);
});

test('review: semicolonless named constant exports remain usable like native ESM',async t=>{
  await compareNative(t,{
    'src/app.mjs':'import {VALUE} from "./dep.mjs"; globalThis.result=VALUE;\n',
    'src/dep.mjs':'export const VALUE=42\nconst local=1,hidden=7;\n',
  },42);
});

test('review: semicolonless exports cannot expose later private declarations',async t=>{
  const directory=await buildRoot(t,{files:{
    'src/app.mjs':'import {hidden} from "./dep.mjs"; globalThis.result=hidden;\n',
    'src/dep.mjs':'export const VALUE=42\nconst local=1,hidden=7;\n',
  }});
  const native=await nativeResult(directory);assert.notEqual(native.status,0);
  assert.match(native.stderr,/export.*hidden|hidden.*export/i);
  const built=await runBuild(directory);assert.notEqual(built.status,0,'private declarations cannot become exports');
  assert.match(built.stderr,/export.*hidden|hidden.*export/i);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

test('review: multiline function initializers cannot hide subsequent const exports',async t=>{
  await compareNative(t,{
    'src/app.mjs':'import {value,OFFSET} from "./dep.mjs"; globalThis.result=value()+OFFSET;\n',
    'src/dep.mjs':'export const value=function number()\n{return 35;}, OFFSET=7;\n',
  },42);
});

test('review: multiline class initializers cannot hide subsequent const exports',async t=>{
  await compareNative(t,{
    'src/app.mjs':'import {Value,OFFSET} from "./dep.mjs"; globalThis.result=Value.number+OFFSET;\n',
    'src/dep.mjs':'export const Value=class LocalValue\n{static number=35;}, OFFSET=7;\n',
  },42);
});

for(const [name,body,expected] of [
  ['function assignment','value=()=>2;',2],
  ['parenthesized assignment','(value)=()=>2;',2],
  ['array destructuring','[value]=[()=>2];',2],
  ['object destructuring','({value}={value:()=>2});',2],
  ['for-of assignment','for(value of [()=>2]){}',2],
  ['for-await assignment','return (async()=>{for await(value of [()=>2]){}})();',2],
  ['direct eval','eval("value=()=>2");',2],
])test(`review: reject exported rebinding through ${name} before publishing snapshots`,async t=>{
  const directory=await buildRoot(t,{files:{
    'src/app.mjs':'import {value,replace} from "./dep.mjs"; globalThis.finished=Promise.resolve(replace()).then(()=>{globalThis.result=value();});\n',
    'src/dep.mjs':`export function value(){return 1} export function replace(){${body}}\n`,
  }});
  const native=await nativeResult(directory);assert.equal(native.status,0,native.stderr);assert.equal(native.value,expected);
  const built=await runBuild(directory);assert.notEqual(built.status,0,'mutable exported bindings need live imports or explicit rejection');
  assert.match(built.stderr,/unsupported.*(?:rebind|eval)|(?:rebind|eval).*unsupported/i);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

test('review: reject reassignment of exported class bindings before publishing snapshots',async t=>{
  const directory=await buildRoot(t,{files:{
    'src/app.mjs':'import {Value,replace} from "./dep.mjs"; replace(); globalThis.result=new Value().number;\n',
    'src/dep.mjs':'export class Value { number=1; } export function replace(){ Value=class {number=2;}; }\n',
  }});
  const native=await nativeResult(directory);assert.equal(native.status,0,native.stderr);assert.equal(native.value,2);
  const built=await runBuild(directory);assert.notEqual(built.status,0,'mutable exported classes need explicit rejection');
  assert.match(built.stderr,/unsupported.*rebind|rebind.*unsupported/i);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

for(const [name,body,read,expected] of [
  ['parenthesized eval','(eval)("value=()=>2");','value()',2],
  ['array rest assignment','[...value]=[2,3];','Array.isArray(value)',true],
  ['object rest assignment','({...value}={x:42});','[typeof value,value.x]',['object',42]],
])test(`final review: reject exported rebinding through ${name} before publishing HTML`,async t=>{
  const directory=await buildRoot(t,{files:{
    'src/app.mjs':`import {value,replace} from "./dep.mjs"; replace(); globalThis.result=${read};\n`,
    'src/dep.mjs':`export function value(){return 1} export function replace(){${body}}\n`,
  }});
  const native=await nativeResult(directory);assert.equal(native.status,0,native.stderr);assert.deepEqual(native.value,expected);
  const built=await runBuild(directory);let actual;
  if(built.status===0) {
    const context={};(await inlineScript(directory)).runInNewContext(context,{timeout:1000});
    actual=JSON.parse(JSON.stringify(context.result));
  }
  assert.notEqual(built.status,0,`unsupported rebinding must fail; native=${JSON.stringify(expected)}, bundle=${JSON.stringify(actual)}`);
  assert.match(built.stderr,/unsupported.*(?:rebind|eval)|(?:rebind|eval).*unsupported/i);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

test('review: statement regex after a control condition stays literal like native ESM',async t=>{
  await compareNative(t,{'src/app.mjs':'const flag=true; globalThis.result=false; if(flag) globalThis.result=/export/.test("export"); if(flag) /export/.test("x");\n'},true);
});

test('review: a keyword-named method call cannot turn following division into a regex',async t=>{
  await compareNative(t,{'src/app.mjs':'const source={if(){return 84;}}; globalThis.result=source.if()/2;\n'},42);
});

test('review: private registry names cannot collide with source declarations like native ESM',async t=>{
  await compareNative(t,{
    'src/app.mjs':'import {VALUE} from "./dep.mjs"; const __bundleModules=5,__bundleModules_1=6; globalThis.result=VALUE+__bundleModules+__bundleModules_1;\n',
    'src/dep.mjs':'export const VALUE=31;\n',
  },42);
});

test('review: top-level this and lexical arrows receive undefined like native ESM',async t=>{
  await compareNative(t,{
    'src/app.mjs':'import {isUndefined} from "./dep.mjs"; globalThis.result={top:this===undefined,arrow:(()=>this)()===undefined,dependency:isUndefined};\n',
    'src/dep.mjs':'export const isUndefined=this===undefined;\n',
  },{top:true,arrow:true,dependency:true});
});

test('review: bare module arguments is rejected rather than exposing wrapper arguments',async t=>{
  const directory=await buildRoot(t,{files:{'src/app.mjs':'globalThis.result=typeof arguments;\n'}});
  const native=await nativeResult(directory);assert.equal(native.status,0,native.stderr);assert.equal(native.value,'undefined');
  const built=await runBuild(directory);assert.notEqual(built.status,0,'wrapper arguments is outside the supported ESM subset');
  assert.match(built.stderr,/unsupported.*arguments/i);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

test('review: original module syntax is checked before wrappers can legalize top-level return',async t=>{
  const directory=await buildRoot(t,{files:{'src/app.mjs':'globalThis.result=42; return;\n'}});
  const native=await nativeResult(directory);assert.notEqual(native.status,0);assert.match(native.stderr,/return/i);
  const built=await runBuild(directory);assert.notEqual(built.status,0,'wrapping must not legalize invalid original ESM');
  assert.match(built.stderr,/module.*syntax|return/i);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

test('nested hyphen modules and named aliases execute their const, class and function exports',async t=>{
  const {context}=await buildAndRun(t,{fixture:'nested'});
  assert.equal(context.result,4294967295);
  assert.equal(context.label,'fixture:4294967295');
});

test('dependencies execute before consumers and a shared canonical path executes once',async t=>{
  const context={events:[]};
  await buildAndRun(t,{files:{
    'src/app.mjs':'import { left } from "./nested/left.mjs";\nimport { right } from "./right.mjs";\nglobalThis.events.push("app"); globalThis.result = left + right;\n',
    'src/nested/left.mjs':'import { base } from "../base.mjs";\nglobalThis.events.push("left"); export const left = base + 1;\n',
    'src/right.mjs':'import { base as number } from "./nested/../base.mjs";\nglobalThis.events.push("right"); export const right = number + 2;\n',
    'src/base.mjs':'globalThis.events.push("base"); export const base = 10;\n',
  }},context);
  assert.deepEqual(context.events,['base','left','right','app']);
  assert.equal(context.result,23);
});

test('only reachable modules are bundled',async t=>{
  const {context}=await buildAndRun(t,{files:{
    'src/protocol.mjs':'import { value } from "a-package-that-must-not-be-loaded";\n',
    'src/app.mjs':'globalThis.result = 7;\n',
  }});
  assert.equal(context.result,7);
});

test('multiline imports retain aliases and comments or strings do not become module syntax',async t=>{
  const {context}=await buildAndRun(t,{files:{
    'src/app.mjs':'/* import { absent } from "external"; */\nimport {\n  value as renamed,\n  label,\n} from "./generated/fm-contract.mjs";\nglobalThis.result = renamed; globalThis.label = label();\n',
    'src/generated/fm-contract.mjs':'// export default is only a comment\nexport const value = 17;\nexport function label() { const pattern = /import|export/g; return `export default ${pattern.test("import")}`; }\n',
  }});
  assert.equal(context.result,17);
  assert.equal(context.label,'export default true');
});

test('punctuation in exported string values cannot hide subsequent named constants',async t=>{
  const {context}=await buildAndRun(t,{files:{
    'src/app.mjs':'import { OPEN, CLOSE, COMMA, KEYWORD, value } from "./generated/fm-contract.mjs";\nglobalThis.result = OPEN + value + COMMA + KEYWORD + CLOSE;\n',
    'src/generated/fm-contract.mjs':'export const OPEN = "{", CLOSE = "}", COMMA = ",", KEYWORD = "export", value = 23;\n',
  }});
  assert.equal(context.result,'{23,export}');
});

test('case-insensitive script-closing text stays inside the single HTML script',async t=>{
  const {context}=await buildAndRun(t,{files:{'src/app.mjs':'globalThis.result = "</ScRiPt>";\n'}});
  assert.equal(context.result,'</ScRiPt>');
});

const rejectedImports=[
  ['package','import { value } from "some-package";\n',/relative|external|unsupported.*import/i],
  ['remote','import { value } from "https://example.invalid/module.mjs";\n',/relative|external|unsupported.*import/i],
  ['source escape','import { value } from "../outside.mjs";\n',/escape|outside/i],
  ['absolute path','import { value } from "/tmp/module.mjs";\n',/relative|external|escape/i],
  ['default binding','import value from "./protocol.mjs";\n',/unsupported.*import/i],
  ['namespace binding','import * as module from "./protocol.mjs";\n',/unsupported.*import/i],
  ['side-effect binding','import "./protocol.mjs";\n',/unsupported.*import/i],
  ['dynamic loading','const load = () => import("./protocol.mjs");\n',/unsupported.*import|dynamic.*import/i],
  ['dynamic loading in a template','globalThis.result = `value:${import("./protocol.mjs")}`;\n',/unsupported.*import|dynamic.*import/i],
  ['import.meta','globalThis.result = import.meta.url;\n',/unsupported.*import|import.meta/i],
];
for(const [name,source,diagnostic] of rejectedImports)test(`reject ${name} before publishing HTML`,async t=>{
  const directory=await buildRoot(t,{files:{'src/app.mjs':source,'outside.mjs':'export const value = 5;\n'}});
  const result=await runBuild(directory);
  assert.notEqual(result.status,0,'unsupported dependencies must fail the build');
  assert.match(result.stderr,diagnostic);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

test('a dependency symlink cannot escape the source directory',async t=>{
  const directory=await buildRoot(t,{files:{
    'src/app.mjs':'import { value } from "./protocol.mjs";\n',
    'outside.mjs':'export const value = 5;\n',
  }});
  await rm(join(directory,'src/protocol.mjs'));
  await symlink(join(directory,'outside.mjs'),join(directory,'src/protocol.mjs'));
  const result=await runBuild(directory);
  assert.notEqual(result.status,0,'symlink escape must fail the build');
  assert.match(result.stderr,/escape|outside/i);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

test('cycles are rejected before any cyclic module executes',async t=>{
  const directory=await buildRoot(t,{files:{
    'src/app.mjs':'import { value } from "./protocol.mjs";\nglobalThis.result=value;\n',
    'src/protocol.mjs':'import { item } from "./controller.mjs";\nexport const value = item;\n',
    'src/controller.mjs':'import { value } from "./protocol.mjs";\nexport const item = value;\n',
  }});
  const result=await runBuild(directory);
  assert.notEqual(result.status,0,'a dependency cycle must fail the build');
  assert.match(result.stderr,/cycl/i);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

for(const [name,source] of [
  ['default','export default 1;\n'],
  ['renamed list','const value = 1; export { value as renamed };\n'],
  ['initialized list binding','const value = 1; export { value };\n'],
  ['re-export','export * from "./protocol.mjs";\n'],
  ['mutable declaration','export let value = 1;\n'],
  ['destructuring declaration','export const { value } = { value:1 };\n'],
])test(`reject unsupported ${name} export before publishing HTML`,async t=>{
  const directory=await buildRoot(t,{files:{'src/app.mjs':source}});
  const result=await runBuild(directory);
  assert.notEqual(result.status,0,'unsupported exports must fail the build');
  assert.match(result.stderr,/unsupported.*export/i);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

test('identity export lists used by the controller bundle match native module execution',async t=>{
  await compareNative(t,{
    'src/protocol.mjs':'function value() { return 7; } function twice(n) { return n * 2; } export { value, twice as twice };\n',
    'src/app.mjs':'import { value, twice } from "./protocol.mjs"; globalThis.result = [value(), twice(value())];\n',
  },[7,14]);
});

test('missing dependencies fail before HTML is published',async t=>{
  const directory=await buildRoot(t,{files:{'src/app.mjs':'import { value } from "./missing.mjs";\n'}});
  const result=await runBuild(directory);
  assert.notEqual(result.status,0,'a missing module must fail the build');
  assert.match(result.stderr,/missing.mjs/);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

test('a missing named export fails before HTML is published',async t=>{
  const directory=await buildRoot(t,{files:{'src/app.mjs':'import { absent } from "./protocol.mjs";\n'}});
  const result=await runBuild(directory);
  assert.notEqual(result.status,0,'an unbound import must fail the build');
  assert.match(result.stderr,/export.*absent|absent.*export/i);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

test('an optional resource report creates its parent and preserves the previous report',async t=>{
  const directory=await buildRoot(t,{files:{'artifacts/resource-report.json':'{"previous_stage":true}\n'}});
  const result=await runBuild(directory,'artifacts/phase2/resource-report.json');
  assert.equal(result.status,0,result.stderr);
  assert.equal(await readFile(join(directory,'artifacts/resource-report.json'),'utf8'),'{"previous_stage":true}\n');
  const report=JSON.parse(await readFile(join(directory,'artifacts/phase2/resource-report.json'),'utf8'));
  assert.equal(report.html_bytes,(await readFile(join(directory,'dist/index.html'))).length);
  assert.equal(report.external_runtime_resources,0);
});

test('default builds keep publishing the existing resource report path',async t=>{
  const directory=await buildRoot(t);
  const result=await runBuild(directory);assert.equal(result.status,0,result.stderr);
  const report=JSON.parse(await readFile(join(directory,'artifacts/resource-report.json'),'utf8'));
  assert.equal(report.html_bytes,(await readFile(join(directory,'dist/index.html'))).length);
});

for(const reportPath of ['../outside-report.json','/tmp/outside-report.json'])test(`reject escaping report path ${reportPath} before publishing HTML`,async t=>{
  const directory=await buildRoot(t);
  const result=await runBuild(directory,reportPath);
  assert.notEqual(result.status,0,'resource report path escape must fail the build');
  assert.match(result.stderr,/report.*escape|report.*outside/i);
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

test('an optional report symlink cannot write outside the module',async t=>{
  const directory=await buildRoot(t);const outside=await mkdtemp(join(tmpdir(),'web-report-outside-'));
  t.after(()=>rm(outside,{recursive:true,force:true}));
  await mkdir(join(directory,'artifacts'));
  await symlink(outside,join(directory,'artifacts/redirect'));
  const result=await runBuild(directory,'artifacts/redirect/report.json');
  assert.notEqual(result.status,0,'resource report symlink escape must fail the build');
  assert.match(result.stderr,/report.*escape|report.*outside/i);
  await assert.rejects(access(join(outside,'report.json')),{code:'ENOENT'});
  await assert.rejects(access(join(directory,'dist/index.html')),{code:'ENOENT'});
});

class Element {
  constructor(tag='div') {
    this.tagName=tag;this.children=[];this.dataset={};this.style={setProperty(name,value){this[name]=value;}};this.attributes={};this.listeners=new Map();
    this.classList={toggle(){},add(){},remove(){}};this.disabled=false;this.hidden=false;
  }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children.length=0;this.append(...children); }
  setAttribute(name,value) { this.attributes[name]=value; }
  addEventListener(name,listener) { this.listeners.set(name,listener); }
  querySelector(selector) { return this.ownerDocument.getElementById(selector.slice(1)); }
}

test('current single HTML executes and mounts the DDS/KS controls without external loading',async t=>{
  const directory=await buildRoot(t,{current:true});
  const result=await runBuild(directory);assert.equal(result.status,0,result.stderr);
  const html=await readFile(join(directory,'dist/index.html'),'utf8');
  const elements=new Map([...html.matchAll(/\bid="([^"]+)"/g)].map(match=>[match[1],new Element()]));
  const document={
    getElementById:id=>elements.get(id),createElement:tag=>Object.assign(new Element(tag),{ownerDocument:document}),
    createTextNode:text=>({textContent:text}),querySelectorAll:()=>[],addEventListener(){},hidden:false,
  };
  for(const element of elements.values())element.ownerDocument=document;
  const context={
    document,TextEncoder,
    Option:function(text,value){return Object.assign(document.createElement('option'),{textContent:text,value});},
    window:{addEventListener(){}},location:{origin:'null',protocol:'file:'},
    fetch(){throw Error('offline startup must not fetch');},setInterval(){},setTimeout(){return 1;},clearTimeout(){},
  };
  (await inlineScript(directory)).runInNewContext(context,{timeout:1000});
  assert.equal(elements.get('strings').children.filter(child=>child.tagName==='button').length,36);
  assert(elements.get('strings').children.filter(child=>child.tagName==='button').every(key=>key.disabled));
  assert.equal(elements.get('keyboard').children.length,25);
  assert.equal(elements.get('keyboard').children[0].textContent,'C3');
  assert.equal(elements.get('keyboard').children[24].textContent,'C5');
  assert.equal(['master','osc','tune1','tune2','filter','amp','mod','lfo1','lfo2'].reduce((sum,id)=>sum+elements.get(id).children.length,0),38);
  assert(elements.get('keyboard').children.every(key=>key.disabled));
  assert.equal(elements.get('fm').disabled,true);
  assert.equal(elements.has('additive'),false,'retired engine must have no DOM entry');
  assert.equal(elements.has('additiveSound'),false,'retired editor must not mount');
  assert.doesNotMatch(html,/additive|加法合成器/i,'retired modules, styles, presets and HTTP routes cannot enter the offline artifact');
  assert.equal(elements.get('applyPreset').disabled,true);
  assert.equal(elements.get('fmSound').hidden,true);
  assert.equal(elements.get('fmPanel').children.length,0);
  assert.equal(elements.get('fmBackend').children[0].textContent,'尚无匹配后端');
  assert.match(elements.get('feedback').textContent,/请通过设备或本地预览服务打开此页/);
});

test('small independent startup script times out locally with no device requests',async t=>{
  const directory=await buildRoot(t,{current:true});
  const result=await runBuild(directory);assert.equal(result.status,0,result.stderr);
  const html=await readFile(join(directory,'dist/index.html'),'utf8');
  const scripts=[...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script\s*>/gi)];
  assert.equal(scripts.length,2);assert.ok(Buffer.byteLength(scripts[0][1])<1500);
  const nodes=new Map(['connectionText','feedback','engineStatus','voiceTag'].map(id=>[id,{textContent:''}]));
  const controls=[{disabled:true},{disabled:true}];let timeout,requests=0,cleared=false;
  const context={document:{getElementById:id=>nodes.get(id),querySelectorAll:()=>controls},
    fetch(){requests++;},setTimeout(fn,delay){assert.equal(delay,8000);timeout=fn;return 7;},
    clearTimeout(id){assert.equal(id,7);cleared=true;}};
  new Script(scripts[0][1]).runInNewContext(context);
  assert.equal(nodes.get('connectionText').textContent,'界面资源加载中');
  timeout();assert.match(nodes.get('feedback').textContent,/界面尚未启动/);
  assert.equal(requests,0);assert.ok(controls.every(control=>control.disabled));
  context.__synthStartup.start();assert.equal(nodes.get('connectionText').textContent,'连接设备');
  assert.equal(cleared,true);timeout();assert.equal(nodes.get('connectionText').textContent,'连接设备');
  assert.equal(requests,0);
});
test('AST minification measures its reduction while preserving literals and contract strings',async t=>{
  const digest='68d7d85ab97942edb128efcb3dffe01bdc50fb98baf9476ab4663d4f22c2cc73';
  const directory=await compareNative(t,{
    'src/app.mjs':`// This is deliberately long whitespace and commentary for the minifier.\nconst retainedNumericData=[0,4294967295,-2147483648,0x7fffffff,1e-18,0.12345678901234568];\nfunction retainedExampleFunction(argumentWithLongName){return {numbers:retainedNumericData,text:argumentWithLongName,pattern:/a\\/\\/b/.source,digest:${JSON.stringify(digest)}};}\nglobalThis.result=retainedExampleFunction('http://host/path // string </script>');\n`,
  },{numbers:[0,4294967295,-2147483648,2147483647,1e-18,0.12345678901234568],text:'http://host/path // string </script>',pattern:'a\\/\\/b',digest});
  const report=JSON.parse(await readFile(join(directory,'artifacts/resource-report.json'),'utf8'));
  assert.ok(report.components.javascript_unminified>report.components.javascript);
  assert.equal(report.minifier.name,'terser');
});
