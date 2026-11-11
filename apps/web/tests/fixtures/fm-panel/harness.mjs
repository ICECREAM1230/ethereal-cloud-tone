import { fixtures } from './models.mjs';

// Load the real production module. A missing module leaves the panel empty in
// the RED run; no substitute component or device transport is installed.
let render;
try { ({renderFmPanel:render} = await import('/src/fm-panel.mjs')); }
catch(error) { window.componentImportError=error.message; }
const container=document.getElementById('fmPanel');
window.parameterIntents=[];
window.selectionIntents=[];
window.currentModel=null;
const handlers={
  onParameterChange:intent=>{
    window.parameterIntents.push(structuredClone(intent));
    if(window.renderOnChange && render)render(container,window.currentModel,handlers);
  },
  onVoiceSelect:intent=>{
    window.selectionIntents.push(structuredClone(intent));
    window.currentModel.selectedVoiceId=intent.voiceId;
    if(render)render(container,window.currentModel,handlers);
  }
};
window.mountScenario=(name,overrides={})=>{
  window.currentModel={...structuredClone(fixtures[name]),...overrides};
  window.parameterIntents=[];
  window.selectionIntents=[];
  window.renderOnChange=false;
  // Each named test scenario is a fresh mount. Keep same-context refreshes in
  // refreshModel() so one scenario cannot inherit another's focused draft.
  container.replaceChildren();
  if(render)render(container,window.currentModel,handlers);
};
window.refreshModel=overrides=>{
  window.currentModel={...window.currentModel,...overrides};
  if(render)render(container,window.currentModel,handlers);
};
window.mountScenario('two');
window.fixtureReady=true;
