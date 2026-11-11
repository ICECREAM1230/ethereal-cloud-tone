// Artificial UI inputs only: patch two / voice six is not a JT51 capability.
const ratio=()=>({id:'ratio',label:'频率比',kind:'number',value:3.5,min:0.5,max:8.5,step:0.5,unit:'倍',editable:true});
const voice=(id,count)=>({id,label:`人工声部 · ${count} 算子`,parameters:[],operators:Array.from({length:count},(_,index)=>({id:`voice-op-${index}`,label:`声部算子 ${index+1}`,parameters:[ratio()]}))});
export function createArtificialModel(){
  return {contextId:'artificial-ui/patch-two-voice-six/a',backend:{id:'artificial-ui',label:'人工 UI · patch 2 / voice 6'},available:true,editable:true,commonLabel:'共享音色（人工 UI）',selectedVoiceId:'voice-six',parameters:[{id:'algorithmCode',label:'共享算法代码',kind:'number',value:2,min:0,max:7,step:1,unit:'代码',editable:true}],patchOperators:[{id:'patch-modulator',label:'P-A',parameters:[{id:'amount',label:'共享算子代码',kind:'number',value:3,min:0,max:12,step:1,unit:'代码',editable:true}]},{id:'patch-carrier',label:'P-B',parameters:[{id:'amount',label:'共享算子代码',kind:'number',value:3,min:0,max:12,step:1,unit:'代码',editable:true}]}],voices:[voice('voice-two',2),voice('voice-six',6)]};
}
