import {renderFmPanel} from '/src/fm-panel.mjs';
import {createArtificialModel} from './ui-models.mjs';
let createPreview;let getMetadata;
try { ({createFmCatalogPreview:createPreview,getFmCatalogMetadata:getMetadata}=await import('/src/fm-catalog.mjs')); }
catch(error) { window.catalogImportError=error.message; }
const panel=document.getElementById('fmPanel');
window.parameterIntents=[];window.navigationIntents=[];window.currentModel=null;
const handlers={onParameterChange:intent=>{
  window.parameterIntents.push(structuredClone(intent));
  if(window.redrawOnChange)renderFmPanel(panel,window.currentModel,handlers);
},onVoiceSelect:intent=>{
  window.navigationIntents.push(structuredClone(intent));
  window.currentModel.selectedVoiceId=intent.voiceId;renderFmPanel(panel,window.currentModel,handlers);
}};
const showPresets=id=>{
  const list=document.getElementById('presets');list.replaceChildren();
  if(!getMetadata)return;
  for(const preset of getMetadata(id).presets){const item=document.createElement('li');item.textContent=`${preset.name}：${preset.description}`;list.append(item);}
};
window.mountCatalog=(profileId=1,voiceIndex=0)=>{
  panel.replaceChildren();window.parameterIntents=[];window.navigationIntents=[];window.redrawOnChange=false;
  window.currentModel=createPreview?createPreview(profileId,voiceIndex):null;
  if(window.currentModel)renderFmPanel(panel,window.currentModel,handlers);
  showPresets(profileId);
};
window.mountArtificial=()=>{
  panel.replaceChildren();window.parameterIntents=[];window.navigationIntents=[];window.redrawOnChange=false;
  window.currentModel=createArtificialModel();renderFmPanel(panel,window.currentModel,handlers);
  document.getElementById('presets').replaceChildren();
};
window.refreshArtificial=overrides=>{window.currentModel={...window.currentModel,...overrides};renderFmPanel(panel,window.currentModel,handlers);};
window.mountCatalog();window.fixtureReady=true;
