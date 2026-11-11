import {readFile,writeFile,mkdir,realpath} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {resolve,dirname,relative,isAbsolute,sep} from 'node:path';
import {createHash} from 'node:crypto';
import {gzipSync} from 'node:zlib';
import {createRequire} from 'node:module';
import {minify} from 'terser';
import {bundleModules} from './bundle.mjs';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const resourceReport=resolve(root,process.env.WEB_UI_RESOURCE_REPORT||'artifacts/resource-report.json');
function checkReportPath(path){
  const offset=relative(root,path);
  if(offset==='..'||offset.startsWith('..'+sep)||isAbsolute(offset))throw Error('resource report path escape: '+path);
}
checkReportPath(resourceReport);
let reportAncestor=resourceReport;
while(true){
  try{checkReportPath(await realpath(reportAncestor));break;}
  catch(error){if(error.code!=='ENOENT')throw error;reportAncestor=dirname(reportAncestor);}
}
const unminifiedScript=await bundleModules(resolve(root,'src'));
// Parse/print and local name mangling only. No expression folding, dead-code
// elimination or property mangling: protocol tables and numeric values stay intact.
const minifyOptions={compress:false,mangle:true,keep_fnames:true,keep_classnames:true,
  format:{inline_script:true,comments:/^!|@license|@preserve|SPDX/}};
const script=(await minify(unminifiedScript,minifyOptions)).code;
if(!script)throw Error('empty minified application');
const css=await readFile(resolve(root,'src/style.css'),'utf8');
const template=await readFile(resolve(root,'src/index.html'),'utf8');
const startup=template.includes('/* STARTUP_SLOT */')?(await minify(await readFile(resolve(root,'src/startup.js'),'utf8'),minifyOptions)).code:'';
const embed=code=>code.replace(/<\/script/gi,match=>'<\\/'+match.slice(2));
const render=code=>template.replace('/* STYLE_SLOT */',()=>css).replace('/* STARTUP_SLOT */',()=>startup).replace('/* SCRIPT_SLOT */',()=>embed(code));
const html=render(script),unminifiedHtml=render(unminifiedScript);
if(/<script[^>]+src=|<link[^>]+(?:stylesheet|preload)|url\(https?:/i.test(html))throw Error('offline resource invariant failed');
await mkdir(resolve(root,'dist'),{recursive:true});
await mkdir(dirname(resourceReport),{recursive:true});
await writeFile(resolve(root,'dist/index.html'),html);
const bytes=Buffer.byteLength(html);const sha256=createHash('sha256').update(html).digest('hex');
const baseline=JSON.parse(await readFile(resolve(root,'docs/交接来源.json'),'utf8'));
const original=baseline.reference_files.find(file=>file.path.endsWith('/main/web/index.html'));
const capacity=JSON.parse(await readFile(resolve(root,'docs/固件容量基线.json'),'utf8'));
await writeFile(resourceReport,JSON.stringify({built_at:new Date().toISOString(),html_bytes:bytes,gzip_bytes:gzipSync(html).length,unminified_html_bytes:Buffer.byteLength(unminifiedHtml),unminified_gzip_bytes:gzipSync(unminifiedHtml).length,sha256,minifier:{name:'terser',version:createRequire(import.meta.url)('terser/package.json').version,compress:false,mangle:true},external_runtime_resources:0,original_html_bytes:original.bytes,html_growth_bytes:bytes-original.bytes,firmware_fit_verified:false,firmware_context:capacity,components:{html:Buffer.byteLength(html)-Buffer.byteLength(script)-Buffer.byteLength(css)-Buffer.byteLength(startup),css:Buffer.byteLength(css),javascript:Buffer.byteLength(script),javascript_unminified:Buffer.byteLength(unminifiedScript),startup:Buffer.byteLength(startup)}},null,2)+'\n');
console.log(`Single HTML: ${bytes} bytes (${(bytes/1024).toFixed(1)} KiB); gzip ${gzipSync(html).length} bytes; SHA256 ${sha256}`);
