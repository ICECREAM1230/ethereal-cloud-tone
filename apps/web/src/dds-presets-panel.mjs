import {DdsPresetClient,validatePresetPath} from './dds-presets.mjs';
import {readPresetImport,presetImportTarget} from './dds-import.mjs';
import {subPresetName} from './timbre-labels.mjs';

// Keep file jobs outside the performance queue, so Off and STOP stay responsive.
export function mountDdsPresets(container,controller){
  const by=id=>container.querySelector('#'+id),client=new DdsPresetClient();
  let busy=false,attempted=false,mounted=null,paths=[],nextOffset=0,importFile=null,reading=false,selection=0,replacement=null;
  let selectedMessage='尚未选择文件。';
  let message='连接设备后读取减法音色库。',loaded='尚未通过本页面加载音色。';
  const mode=()=>controller.modeState;
  const online=()=>controller.apiMajor===2&&controller.status==='online'&&!controller.suspended&&mode()?.requested===0&&mode()?.applied===0&&!mode()?.switching&&!mode()?.recovering;
  const identity=()=>JSON.stringify([controller.epoch,controller.token,controller.runtimeState?.context_epoch,mode()?.requested,mode()?.applied]);
  const target=()=>validatePresetPath('Patches/User/'+by('ddsCategory').value+'/'+by('ddsName').value.replace(/\.toml$/i,'')+'.toml',{userOnly:true});
  const importTarget=()=>presetImportTarget(by('ddsImportCategory').value,by('ddsImportName').value);
  function clearReplacement(){replacement=null;by('ddsImportConflict').hidden=true;}
  function renderList(){
    const selected=by('ddsPresetList').value;by('ddsPresetList').replaceChildren();
    for(const [kind,label] of [['Factory','内置音色'],['User','我的音色']]){
      const group=document.createElement('optgroup');group.label=label;
      for(const path of paths.filter(p=>p.toLowerCase().startsWith('patches/'+kind.toLowerCase()+'/'))){
        const option=document.createElement('option');option.value=path;option.textContent=subPresetName(path);group.append(option);
      }
      if(group.children.length)by('ddsPresetList').append(group);
    }
    if(paths.includes(selected))by('ddsPresetList').value=selected;
  }
  function update(){
    container.hidden=mode()?.requested!==0;
    const connected=online(),ready=connected&&controller.editable&&!busy;
    if(replacement&&(!connected||document.hidden||replacement.stamp!==identity())){clearReplacement();message='设备状态已变化，替换确认已取消；请重新导入。';}
    by('ddsImportSelected').textContent=selectedMessage;
    let destination=null;try{destination=importTarget();by('ddsImportTarget').textContent='目标：'+destination;}catch(error){by('ddsImportTarget').textContent='目标未就绪：'+error.message;}
    by('ddsImportConflict').hidden=!replacement;
    by('ddsImportReplace').disabled=!ready||!replacement;
    by('ddsPresetStatus').textContent=message;by('ddsLoadedPreset').textContent=loaded;
    by('ddsStorage').textContent=mounted===false?'用户存储未挂载：仍可使用内置音色；保存、导入和开机设置暂不可用。':mounted===true?'用户存储已挂载。':'等待存储状态。';
    by('ddsPresetList').disabled=!connected||busy||!paths.length;
    by('ddsRefresh').disabled=!connected||busy;by('ddsMore').hidden=!nextOffset;by('ddsMore').disabled=!connected||busy;
    by('ddsLoad').disabled=!ready||!paths.length||controller.pending.size>0;
    by('ddsExport').disabled=!ready||!paths.length;
    by('ddsDefault').disabled=!ready||mounted!==true||!paths.length;by('ddsDefaultReset').disabled=!ready||mounted!==true;
    by('ddsSave').disabled=!ready||mounted!==true||controller.pending.size>0;by('ddsImport').disabled=!ready||mounted!==true||!importFile||reading||!destination||!!replacement;
    for(const id of ['ddsCategory','ddsName','ddsImportFile','ddsImportCategory','ddsImportName'])by(id).disabled=!connected||busy||mounted!==true;
    container.dataset.busy=String(busy);
    if(connected&&controller.editable&&!attempted){attempted=true;queueMicrotask(()=>run('list'));}
  }
  async function run(operation,{path='',text='',offset=0}={}){
    if(busy||!online()||operation!=='list'&&!controller.editable)return;
    const stamp=identity(),file=importFile;let loadBarrier=false,ticket=null;
    clearReplacement();
    // Loading itself advances the owner context and rebinds the HTTP session.
    // Its dedicated ticket survives that adoption, but STOP/mode/blur invalidate it.
    const valid=()=>!document.hidden&&(loadBarrier?
      !!ticket&&controller.ddsPresetLoadTicket===ticket&&!controller.suspended:
      online()&&identity()===stamp);
    busy=true;message=operation==='load'?'正在释放旧按键并加载音色；可用“全来源停止”（STOP）中止等待。':'正在处理音色库请求，等待设备完成…';update();
    try{
      if(operation==='load'){
        loadBarrier=true;
        if(!await controller.beginDdsPresetLoad())throw Error('演奏状态已变化，未提交加载；请重新选择音色。');
        ticket=controller.ddsPresetLoadTicket;
      }
      const result=await client.execute(operation,{path,text,offset,valid});mounted=result.status.mounted;
      const label=subPresetName(path);
      if(operation==='list'){
        paths=offset?[...new Set([...paths,...result.paths])]:result.paths;nextOffset=result.status.next_offset;renderList();message='已读取 '+paths.length+' 个音色'+(nextOffset?'，可继续读取更多。':'。');
      }else if(operation==='load'){
        if(!await controller.endDdsPresetLoad(ticket,result.status.context_epoch))throw Error('音色任务已完成，但演奏通道尚未确认；请重新连接后核对，页面不会重试加载。');
        ticket=null;
        loaded='本次已加载：'+label;message='已加载 '+label+'，请重新按键。';if(result.status.warnings)message+=' 有 '+result.status.warnings+' 项兼容提示。';
      }else if(operation==='read'){
        const url=URL.createObjectURL(new Blob([result.text],{type:'text/plain;charset=utf-8'})),a=document.createElement('a');
        a.href=url;a.download=path.split('/').at(-1);a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);message='已导出 '+label+' 的 TOML 文件。';
      }else if(operation==='save'||operation==='write'||operation==='import'){
        if(!paths.includes(path)){paths.push(path);renderList();}by('ddsPresetList').value=path;
        message=operation==='save'?'已保存当前减法音色。':'已导入 '+label+'；已保存到 User，尚未加载。选择后点击加载才能采用。';
        if(operation!=='save'){selection++;importFile=null;by('ddsImportFile').value='';selectedMessage='已完成导入；请重新选择文件以开始下一次导入。';}
      }else message=path?'已将 '+label+' 设置为开机音色；当前演奏音色未改变。':'已恢复固件开机默认音色；当前演奏音色未改变。';
    }catch(error){
      if(operation==='import'&&error.code==='failed'&&error.message.startsWith('target exists')&&online()&&identity()===stamp&&file===importFile){
        replacement={path,file,stamp};by('ddsImportConflictText').textContent='目标 '+path+' 已存在或有可恢复副本。原文件未改变；确认替换将覆盖该文件。';
        message='同名文件已受保护，未导入。修改目标名称，或明确确认替换。';
      }else message=error.code==='unsupported'?'此固件尚未提供音色库接口，其他控制仍可使用。':error.code==='failed'?'音色操作失败：'+error.message:error.message||'音色操作失败。';
      if(error.resultUnknown)message+=' 请刷新列表和设备状态核对，页面不会自动重试。';
    }finally{if(loadBarrier&&ticket)await controller.endDdsPresetLoad(ticket);busy=false;update();}
  }
  function invoke(operation,path){try{run(operation,{path:typeof path==='function'?path():path||''});}catch(error){message=error.message;update();}}
  by('ddsRefresh').addEventListener('click',()=>run('list'));by('ddsMore').addEventListener('click',()=>run('list',{offset:nextOffset}));
  by('ddsLoad').addEventListener('click',()=>invoke('load',by('ddsPresetList').value));by('ddsExport').addEventListener('click',()=>invoke('read',by('ddsPresetList').value));
  by('ddsDefault').addEventListener('click',()=>invoke('default',by('ddsPresetList').value));by('ddsDefaultReset').addEventListener('click',()=>invoke('default',''));
  by('ddsSave').addEventListener('click',()=>invoke('save',target));
  for(const id of ['ddsImportCategory','ddsImportName'])by(id).addEventListener('input',()=>{clearReplacement();update();});
  by('ddsImportFile').addEventListener('change',async()=>{
    const version=++selection;importFile=null;reading=false;clearReplacement();
    const file=by('ddsImportFile').files?.[0];
    if(!file){selectedMessage='尚未选择文件。';update();return;}
    reading=true;selectedMessage='正在读取 '+file.name+'…';update();
    try{
      const result=await readPresetImport(file);
      if(version!==selection)return;
      importFile=result;by('ddsImportName').value=file.name.replace(/\.toml$/i,'');
      selectedMessage='已选择：'+result.name+' · '+result.bytes+' 字节（UTF-8）';
      message='本地文件已就绪；点击导入后才会上传，不会自动加载。';
    }catch(error){if(version!==selection)return;selectedMessage=error.message;message='文件未就绪，尚未上传。';}
    finally{if(version===selection){reading=false;update();}}
  });
  by('ddsImport').addEventListener('click',()=>{if(!importFile||reading||replacement)return;try{run('import',{path:importTarget(),text:importFile.text});}catch(error){message=error.message;update();}});
  by('ddsImportReplace').addEventListener('click',()=>{
    const pending=replacement;
    if(!pending||pending.stamp!==identity()||pending.file!==importFile){clearReplacement();message='替换确认已失效，请重新导入。';update();return;}
    run('write',{path:pending.path,text:pending.file.text});
  });
  by('ddsImportCancel').addEventListener('click',()=>{clearReplacement();message='已取消替换，原文件未改变。';update();});
  return {update};
}
