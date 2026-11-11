import {ADDITIVE_PRESETS,getAdditivePreset,validateAdditivePatch,envelopeIncrement,envelopeMilliseconds,additiveSampleRate} from './additive.mjs';

const clone=value=>value===null?null:JSON.parse(JSON.stringify(value));
const signature=value=>JSON.stringify(value);
const percent=value=>`${(value/65535*100).toFixed(1)}%`;

/** Local patch editor. update() consumes controller-confirmed state only.
 * onApply is called solely by the explicit apply button; its true return value
 * acknowledges a submitted intent, never a device commit or successful sound.
 */
export function mountAdditivePanel(container,{onApply}={}){
  const document=container.ownerDocument;
  let frame=null,contextId,hasContext=false,current=null,permissions={writable:false,pending:false};
  let draft=null,baseline=null,baseRevision=0,latestPatch=null,selected=0,conflict=false,submitted=null,notice='';
  const dirty=()=>!!draft&&signature(draft)!==signature(baseline);
  const knownPatch=value=>{try{return value===null?null:validateAdditivePatch(value);}catch{return null;}};
  const hasIdentity=()=>typeof contextId==='string'&&contextId.trim().length>0;
  const invalidEditors=()=>!!frame&&Array.from(frame.editor.querySelectorAll('input[type=number]'))
    .some(input=>!input.disabled&&(input.value===''||!input.validity.valid));
  const canApply=()=>!!frame&&hasIdentity()&&!!current?.available&&current.editable===true&&!current.busy
    &&permissions.writable===true&&!permissions.pending&&!submitted&&dirty()&&!invalidEditors()&&typeof onApply==='function';
  const node=(tag,className,text)=>{
    const element=document.createElement(tag);if(className)element.className=className;if(text!==undefined)element.textContent=text;return element;
  };
  function acceptPatch(patch,revision){
    draft=clone(patch);baseline=clone(patch);baseRevision=revision;conflict=false;submitted=null;notice='';
  }
  function sampleRate(){try{return additiveSampleRate(current?.sampleRate);}catch{return null;}}
  function changed(){notice='';paint();}
  function build(){
    const own={root:node('section','additive-panel'),weights:[],partialButtons:[],globals:[],envelopes:[]};
    frame=own;own.root.setAttribute('aria-label','加法合成器音色编辑器');
    const live=element=>frame===own&&own.root.parentNode===container&&element.isConnected&&own.root.contains(element)&&!permissions.pending&&!submitted;
    const heading=node('div','additive-heading');
    const title=node('div');title.append(node('h2','','加法合成器'),node('p','additive-caption','把 32 个分音，塑成自己的音色。'));
    own.state=node('span','additive-state','尚未读取设备音色');own.state.id='additiveState';own.state.setAttribute('role','status');
    heading.append(title,own.state);own.root.append(heading);

    const preset=node('div','additive-preset');
    const presetLabel=node('label','','示例音色');presetLabel.htmlFor='additivePreset';
    own.preset=node('select');own.preset.id='additivePreset';own.preset.append(new Option('选择一个示例…',''));
    for(const item of ADDITIVE_PRESETS)own.preset.append(new Option(item.name,item.id));
    own.load=node('button','','载入草稿');own.load.id='additiveLoadPreset';own.load.type='button';own.load.disabled=true;
    own.preset.addEventListener('change',()=>{if(live(own.preset))own.load.disabled=!own.preset.value;});
    own.load.addEventListener('click',()=>{
      if(!live(own.load))return;const patch=getAdditivePreset(own.preset.value);if(!patch)return;
      draft=patch;submitted=null;notice='示例已载入本地草稿，尚未应用到设备。';paint();
    });
    preset.append(presetLabel,own.preset,own.load,node('p','additive-caption','示例只载入草稿；选中或拖动控件均不会写入设备。'));own.root.append(preset);

    own.message=node('p','additive-message');own.message.setAttribute('role','status');own.root.append(own.message);
    own.conflict=node('div','additive-conflict');own.conflict.id='additiveConflict';
    own.conflict.append(node('p','','检测到较新的设备值。当前草稿已保留，可继续编辑，或重新读取并丢弃草稿。'));
    own.reload=node('button','','重新读取并丢弃草稿');own.reload.id='additiveReload';own.reload.type='button';
    own.reload.addEventListener('click',()=>{if(!live(own.reload))return;acceptPatch(latestPatch,current.revision);paint();});
    own.conflict.append(own.reload);own.root.append(own.conflict);
    own.empty=node('div','additive-empty');own.empty.append(node('h3','','尚未读取设备音色'),node('p','','读取到已确认音色后会显示参数；也可明确选择一个示例并载入草稿，开始编辑。'));own.root.append(own.empty);
    own.editor=node('div','additive-editor');own.root.append(own.editor);

    const spectrum=node('section','additive-spectrum');
    const spectrumHeading=node('div','additive-subheading');spectrumHeading.append(node('h3','','分音基础权重'),node('p','additive-caption','32 个分音 · 本地草稿，不是实时测量'));
    spectrum.append(spectrumHeading);
    const scroll=node('div','additive-spectrum-scroll');scroll.tabIndex=0;scroll.setAttribute('role','region');scroll.setAttribute('aria-label','32 个分音权重，窄屏可在区域内横向滚动');
    const bars=node('div','additive-spectrum-bars');
    for(let index=0;index<32;index++){
      const column=node('div','additive-partial-column');column.dataset.group='0';
      const value=node('output','additive-partial-value','—');
      const well=node('div','additive-partial-well');
      const fill=node('span','additive-partial-fill');fill.setAttribute('aria-hidden','true');
      const input=node('input');input.type='range';input.min='0';input.max='65535';input.step='1';
      input.dataset.additiveWeight=String(index);input.setAttribute('aria-label',`第 ${index+1} 分音基础权重`);input.setAttribute('aria-orientation','vertical');
      input.addEventListener('focus',()=>{if(!live(input)||!draft)return;selected=index;paintSelection();});
      input.addEventListener('input',()=>{
        if(!live(input)||!draft)return;selected=index;draft.partials[index].weight=Number(input.value);changed();
      });
      const button=node('button','additive-partial-select',String(index+1));button.type='button';button.dataset.additivePartial=String(index);
      button.setAttribute('aria-label',`选择第 ${index+1} 分音`);
      button.addEventListener('click',()=>{if(live(button)&&draft){selected=index;paintSelection();}});
      well.append(fill,input);column.append(value,well,button);bars.append(column);
      own.weights.push({input,value,fill,column});own.partialButtons.push(button);
    }
    scroll.append(bars);spectrum.append(scroll,node('p','additive-scroll-hint','拖动调权重，点分音编号查看细节。窄屏可在频谱内左右滑动。'));own.editor.append(spectrum);

    const detail=node('section','additive-partial-detail');
    own.partialTitle=node('h3','','第 1 分音');detail.append(own.partialTitle);
    const detailFields=node('div','additive-detail-fields');
    function numberField(label,id,min,max,step){
      const wrap=node('div','additive-number-field');const labelNode=node('label','',label);labelNode.htmlFor=id;
      const input=node('input');input.id=id;input.type='number';input.min=String(min);input.max=String(max);input.step=String(step);
      wrap.append(labelNode,input);detailFields.append(wrap);return input;
    }
    own.ratio=numberField('频率比（×基频）','additiveRatio',0,16777215/65536,'any');own.ratio.dataset.additiveRatio='';
    own.exactWeight=numberField('精确权重（0–65535）','additiveExactWeight',0,65535,1);
    const groupWrap=node('div','additive-number-field');const groupLabel=node('label','','包络组');groupLabel.htmlFor='additivePartialGroup';
    own.group=node('select');own.group.id='additivePartialGroup';for(let index=0;index<4;index++)own.group.append(new Option(`第 ${index+1} 组`,String(index)));
    groupWrap.append(groupLabel,own.group);detailFields.append(groupWrap);
    own.partialHint=node('p','additive-caption');own.partialHint.dataset.additiveRatioRaw='';detail.append(detailFields,own.partialHint);own.editor.append(detail);
    const validNumber=input=>input.value!==''&&input.validity.valid&&Number.isFinite(Number(input.value));
    own.ratio.addEventListener('input',()=>{
      if(!live(own.ratio)||!draft)return;if(!validNumber(own.ratio)){paint();return;}
      draft.partials[selected].ratio_q16=Math.round(Number(own.ratio.value)*65536);changed();
    });
    own.exactWeight.addEventListener('input',()=>{
      if(!live(own.exactWeight)||!draft)return;if(!validNumber(own.exactWeight)){paint();return;}
      draft.partials[selected].weight=Number(own.exactWeight.value);changed();
    });
    own.group.addEventListener('change',()=>{
      if(!live(own.group)||!draft)return;draft.partials[selected].group=Number(own.group.value);changed();
    });

    const globalLayout=node('div','additive-global-layout');
    function section(title,caption){const element=node('section','additive-control-section');element.append(node('h3','',title),node('p','additive-caption',caption));const fields=node('div','additive-fields');element.append(fields);globalLayout.append(element);return fields;}
    function slider(parent,key,label,max,format,hint){
      const wrap=node('div','additive-control');const labelNode=node('label','',label);const id=`additive-${key}`;labelNode.htmlFor=id;
      const value=node('output');value.htmlFor=id;labelNode.append(value);
      const input=node('input');input.id=id;input.type='range';input.min='0';input.max=String(max);input.step='1';input.dataset.additiveGlobal=key;
      input.addEventListener('input',()=>{if(live(input)&&draft){draft[key]=Number(input.value);changed();}});
      wrap.append(labelNode,input,node('small','',hint));parent.append(wrap);own.globals.push({key,input,value,format});
    }
    const master=section('整体声音','主增益、左右位置和弯音范围');
    slider(master,'master_gain','主增益',65535,value=>`${(value/65536*100).toFixed(1)}%`,'调整音色整体的声音强弱');
    slider(master,'master_pan','声像',65535,value=>value===32768?'中央':`${value<32768?'左':'右'} ${Math.round(Math.abs(value-32768)/32768*100)}%`,'在左声道与右声道之间移动');
    slider(master,'bend_range','弯音范围',127,value=>`${value} 半音`,'0–127 半音');
    const bypassLabel=node('label','additive-checkbox');own.bypass=node('input');own.bypass.type='checkbox';own.bypass.dataset.additiveGlobal='bypass_lowpass';
    bypassLabel.append(own.bypass,node('span','','旁路低通滤波器'));master.append(bypassLabel);
    own.bypass.addEventListener('change',()=>{if(live(own.bypass)&&draft){draft.bypass_lowpass=own.bypass.checked;changed();}});
    const shaping=section('频谱塑形','基础权重之外的全局塑形参数');
    slider(shaping,'centroid_q8','频谱重心',8191,value=>`第 ${Number((value/256+1).toFixed(3))} 个分音`,'移动分音重心');
    slider(shaping,'slope','斜率',65535,percent,'偏离重心时的衰减强度');
    slider(shaping,'comb_period','周期峰间隔',31,value=>value===0?'关闭':String(value),'0 为关闭；其余值 1–31');
    slider(shaping,'comb_depth','峰外深度',65535,percent,'非周期峰分音的减弱程度');
    own.editor.append(globalLayout);

    const envSection=node('section','additive-envelope-section');
    const envHeading=node('div','additive-subheading');envHeading.append(node('h3','','四组 ADSR 包络'));
    own.envHint=node('p','additive-caption');envHeading.append(own.envHint);envSection.append(envHeading);
    const envGrid=node('div','additive-envelope-grid');
    const stages=[['attack_inc','起音 A'],['decay_inc','衰减 D'],['sustain','保持 S'],['release_inc','释放 R']];
    for(let group=0;group<4;group++){
      const card=node('section','additive-envelope');card.dataset.group=String(group);card.append(node('h4','',`第 ${group+1} 组包络`));
      for(const [key,label] of stages){
        const row=node('div','additive-envelope-row');const id=`additive-envelope-${group}-${key}`;
        const labelNode=node('label','',label);labelNode.htmlFor=id;
        const input=node('input');input.id=id;input.type='number';input.min='0';input.step=key==='sustain'?'1':'any';input.dataset.additiveEnvelope=`${group}:${key}`;
        const unit=node('span','additive-envelope-unit',key==='sustain'?'':'ms');
        const detail=node('small','additive-envelope-value');
        input.addEventListener('input',()=>{
          if(!live(input)||!draft)return;if(!validNumber(input)){paint();return;}
          try{draft.envelopes[group][key]=key==='sustain'?Number(input.value):envelopeIncrement(Number(input.value),current.sampleRate);changed();}catch{/* Native validity keeps out-of-range text local. */}
        });
        row.append(labelNode,input,unit,detail);card.append(row);own.envelopes.push({group,key,input,detail});
      }
      envGrid.append(card);
    }
    envSection.append(envGrid);own.editor.append(envSection);

    const applyRow=node('div','additive-apply-row');const applyCopy=node('div');
    applyCopy.append(node('p','additive-apply-warning','应用音色会停止所有来源演奏，应用后需重新按键。'));
    own.applyStatus=node('p','additive-caption');applyCopy.append(own.applyStatus);
    own.apply=node('button','additive-apply','应用音色');own.apply.type='button';own.apply.id='additiveApply';
    own.apply.addEventListener('click',()=>{
      if(!live(own.apply)||!canApply())return;
      let patch;try{patch=validateAdditivePatch(draft);}catch{notice='草稿参数无效，请检查后重试。';paint();return;}
      const request={patch:clone(patch),revision:current.revision,seenPending:false};submitted=request;notice='';paint();
      let accepted=false;try{accepted=onApply(clone(patch),contextId)===true;}catch{accepted=false;}
      if(frame!==own)return;
      if(!accepted){if(submitted===request)submitted=null;notice='应用请求未提交。草稿已保留，请检查设备状态后重试。';}
      paint();
    });
    applyRow.append(applyCopy,own.apply);own.root.append(applyRow);container.replaceChildren(own.root);
  }
  function setValue(input,value){if(document.activeElement!==input||input.type==='range')input.value=String(value);}
  function paintSelection(){
    if(!frame||!draft)return;const own=frame,partial=draft.partials[selected];
    own.partialTitle.textContent=`第 ${selected+1} 分音`;
    for(let index=0;index<32;index++){
      own.partialButtons[index].setAttribute('aria-pressed',String(index===selected));
      own.weights[index].column.classList.toggle('selected',index===selected);
    }
    setValue(own.ratio,partial.ratio_q16/65536);setValue(own.exactWeight,partial.weight);own.group.value=String(partial.group);
    own.partialHint.textContent=`频率为基频的 ${partial.ratio_q16/65536} 倍；基础权重 ${percent(partial.weight)}；使用第 ${partial.group+1} 组包络。`;
  }
  function paint(){
    if(!frame)return;const own=frame,modified=dirty(),pending=!!permissions.pending||!!submitted;
    const editors=[...own.weights.map(item=>item.input),...own.partialButtons,own.ratio,own.exactWeight,own.group,
      ...own.globals.map(item=>item.input),own.bypass,...own.envelopes.map(item=>item.input),own.preset];
    for(const input of editors)input.disabled=pending;
    own.load.disabled=pending||!own.preset.value;
    own.empty.hidden=!!draft;own.editor.hidden=!draft;own.conflict.hidden=!conflict;
    own.reload.disabled=pending||!latestPatch;own.apply.disabled=!canApply();own.apply.textContent=pending?'等待设备确认…':'应用音色';
    own.state.textContent=pending?'等待设备确认':modified?'本地草稿 · 未应用':draft?'设备已确认':'尚未读取设备音色';
    own.state.dataset.kind=pending?'pending':modified?'draft':draft?'confirmed':'empty';
    let reason='';
    if(pending)reason='正在等待设备确认，草稿编辑、载入和重读暂时锁定。';
    else if(!current?.available)reason='设备未提供可用的加法合成器。可载入示例编辑本地草稿。';
    else if(current.busy)reason='设备正在处理音色，暂不能应用。';
    else if(!permissions.writable||!current.editable)reason='当前仅可编辑本地草稿，设备就绪后才能应用。';
    own.message.textContent=pending?reason:notice||reason;own.message.hidden=!own.message.textContent;
    own.applyStatus.textContent=pending?'请求已提交，等待设备读回确认；收到请求不代表音色已生效。':!draft?'先读取设备音色，或显式载入示例。':modified?'草稿尚未应用到设备。':'所有控件与当前设备已确认音色一致。';
    if(!draft)return;
    for(let index=0;index<32;index++){
      const partial=draft.partials[index],item=own.weights[index];item.input.value=String(partial.weight);
      item.input.setAttribute('aria-valuetext',`${partial.weight}，${percent(partial.weight)}，第 ${partial.group+1} 组包络`);
      item.value.textContent=`${Math.round(partial.weight/65535*100)}`;item.fill.style.height=percent(partial.weight);item.column.dataset.group=String(partial.group);
    }
    paintSelection();
    for(const item of own.globals){item.input.value=String(draft[item.key]);item.value.textContent=item.format(draft[item.key]);}
    own.bypass.checked=draft.bypass_lowpass;
    const hz=sampleRate(),max=hz?65535*1000/hz:null;
    own.envHint.textContent=hz?`线性满量程时间，按设备 ${(hz/1000).toFixed(3)} kHz 量化；0 ms 表示保持。最长 ${max.toFixed(3)} ms。`:'尚未读取有效采样率，时间控件暂不可编辑；0 表示保持。';
    for(const item of own.envelopes){
      const raw=draft.envelopes[item.group][item.key],sustain=item.key==='sustain';
      item.input.disabled=pending||!sustain&&!hz;item.input.max=String(sustain?65535:(max||0));
      const value=sustain?raw:hz?Math.min(max,Number(envelopeMilliseconds(raw,current.sampleRate).toFixed(6))):'';
      setValue(item.input,value);item.detail.textContent=sustain?`0–65535 · ${percent(raw)}`:raw===0?'保持':'满量程线性时间';
    }
    for(const input of own.editor.querySelectorAll('input[type=number]'))input.setAttribute('aria-invalid',String(!input.disabled&&(input.value===''||!input.validity.valid)));
    if(!pending&&invalidEditors())own.applyStatus.textContent='存在未完成或超出范围的数值，修正后再应用。';
    own.apply.disabled=!canApply();
  }
  function update(runtime,{writable=false,pending=false}={}){
    current=runtime||{};permissions={writable,pending};const nextContext=current.contextId??null;
    const incoming=knownPatch(current.patch??null);
    if(!hasContext||contextId!==nextContext||!frame){
      contextId=nextContext;hasContext=true;selected=0;latestPatch=incoming;acceptPatch(incoming,current.revision);build();
    }else{
      latestPatch=incoming;
      if(submitted){
        if(pending)submitted.seenPending=true;
        if(!pending&&incoming&&current.revision!==submitted.revision&&signature(incoming)===signature(submitted.patch)){
          const newerDraft=signature(draft)!==signature(submitted.patch)?clone(draft):null;
          acceptPatch(incoming,current.revision);
          if(newerDraft){draft=newerDraft;notice='上一版音色已获设备确认；当前继续编辑的草稿尚未应用。';}
        }else if(!pending&&submitted.seenPending){submitted=null;notice='尚未确认本次草稿已生效。草稿已保留，可检查设备后重试。';}
      }
      if(!dirty()&&!submitted){acceptPatch(incoming,current.revision);}
      else if(current.revision!==baseRevision){conflict=true;}
    }
    paint();
  }
  function clear(){frame=null;current=null;draft=null;baseline=null;latestPatch=null;submitted=null;hasContext=false;contextId=undefined;container.replaceChildren();}
  return {update,clear};
}
