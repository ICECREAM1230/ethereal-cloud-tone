import {validatePresetPath} from './dds-presets.mjs';

export function presetImportTarget(category,name){
  return validatePresetPath('Patches/User/'+category+'/'+name.replace(/\.toml$/i,'')+'.toml',{userOnly:true});
}

// Decode bytes strictly: File.text() silently replaces invalid UTF-8, which
// could otherwise change imported names or values without telling the user.
export const readPresetImport=async function readPresetImport(file){
  if(!file||!/\.toml$/i.test(file.name)||!file.size||file.size>16384)throw Error('请选择不超过 16 KiB 的非空 .toml 文件。');
  let text;
  try{text=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(await file.arrayBuffer());}
  catch{throw Error('无法读取 UTF-8 文件；请检查文件编码后重新选择。');}
  if(!text||text.includes('\0'))throw Error('文件须为不含 NUL 的非空 UTF-8 TOML。');
  // dds_files_save adds a final LF. At the exact limit there is no spare byte.
  if(file.size===16384&&!text.endsWith('\n'))throw Error('16 KiB 文件须以换行结尾；请补充换行或缩小文件。');
  return {name:file.name,bytes:file.size,text};
};
