// Pure presentation fixtures. These are not HTTP fields, backend capability
// claims, chip profiles, or evidence of any FM hardware behaviour.
const ratio = () => ({id:'ratio',label:'频率比',kind:'number',value:3.5,min:0.5,max:8.5,step:0.5,unit:'倍',editable:true});
const waveform = () => ({id:'wave',label:'波形',kind:'enum',value:'sine',options:[{value:'sine',label:'正弦'},{value:'triangle',label:'三角'}],editable:true});
const algorithm = () => ({id:'algorithm',label:'连接算法',kind:'enum',value:7,options:[{value:2,label:'串联'},{value:7,label:'并联'},{value:19,label:'分层',disabled:true,reason:'当前 voice 不支持分层'}],editable:true});
const operator = (id,label) => ({id,label,parameters:[ratio(),waveform(),{id:'enabled',label:'算子启用',kind:'boolean',value:true,editable:true}]});
const voice = (id,label,count) => ({id,label,algorithm:algorithm(),parameters:[{id:'feedback',label:'反馈',kind:'number',value:2,min:0,max:7,step:1,unit:'级',editable:true}],operators:Array.from({length:count},(_,index)=>operator(index===0?'modulator':index===1?'carrier':`logical-${index}`,`算子 ${index+1}`))});
const model = (count) => ({contextId:`test-only-backend-${count}/generation-a`,backend:{id:`test-only-${count}`,label:`本地 UI 示例 · ${count} 算子`},available:true,editable:true,selectedVoiceId:'voice-alpha',parameters:[{id:'level',label:'主音量',kind:'number',value:0.65,min:0,max:1,step:0.05,unit:'倍',editable:true}],voices:[voice('voice-alpha','音色声部',count)]});

export const fixtures = {
  two:model(2),
  four:model(4),
  six:model(6),
  heterogeneous:{...model(2),contextId:'mixed-layout/revision-a',backend:{id:'mixed-layout',label:'异构布局 · UI 示例'},voices:[voice('voice-alpha','双算子声部',2),voice('voice-zeta','六算子声部',6),voice('voice-middle','四算子声部',4)]},
  readonly:{...model(4),editable:false,reason:'当前会话只有查看权限'},
  waiting:{...model(2),waiting:true,reason:'等待目标状态确认'},
  unavailable:{...model(6),available:false,editable:false,reason:'后端尚未就绪'},
  scoped:model(2),
  empty:{contextId:'no-layout/a',backend:{id:'empty',label:'待发现后端'},available:false,editable:false,reason:'没有可用的 voice 描述',selectedVoiceId:null,voices:[],parameters:[]},
  missing:model(2),
  malicious:model(2)
};
fixtures.scoped.voices[0].parameters[0].editable=false;
fixtures.scoped.voices[0].parameters[0].reason='反馈由设备只读';
fixtures.scoped.voices[0].operators[0].editable=false;
fixtures.scoped.voices[0].operators[0].reason='调制算子暂不允许编辑';
fixtures.scoped.voices[0].operators[1].parameters[0].waiting=true;
fixtures.scoped.voices[0].operators[1].parameters[0].reason='频率比等待采用';
fixtures.missing.voices[0].operators[0].parameters[0].value=null;
fixtures.missing.voices[0].operators[0].parameters[0].reason='频率比尚未读取';
fixtures.missing.voices[0].operators[1].parameters.push({id:'unknown',label:'未解释的参数',kind:'opaque',value:'raw',editable:true,reason:'该类型只可查看'});
const malicious = '<img src=x onerror=window.__fmInjected=true>';
fixtures.malicious.backend.label=malicious;
fixtures.malicious.voices[0].label=malicious;
fixtures.malicious.voices[0].operators[0].label=malicious;
fixtures.malicious.voices[0].operators[0].parameters[0].label=malicious;
fixtures.malicious.voices[0].operators[0].parameters[0].unit=malicious;
fixtures.malicious.voices[0].operators[0].parameters[0].hint=malicious;
fixtures.malicious.voices[0].algorithm.options[0].label=malicious;
fixtures.malicious.reason=malicious;
