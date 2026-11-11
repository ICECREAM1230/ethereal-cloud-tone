
const full=new Set([7,71,72,73,74,75,76,77,79,87,102,103,104,105,106,107,109,110,111,112,113]);
function record(value){return !!value&&typeof value==="object"&&!Array.isArray(value)}
export function getSubPatch(capabilities,state){
 const cap=capabilities?.sub_patch,status=state?.sub_patch;
 if(cap===undefined&&status===undefined)return null;
 if(!record(cap)||!record(status)||cap.version!==1||status.version!==1||cap.required!==true||status.required!==true||typeof cap.supported!=="boolean"||typeof status.supported!=="boolean"||typeof status.valid!=="boolean"||cap.supported!==status.supported)return {available:false,reason:"sub_patch_contract"};
 return {...status,available:cap.supported&&status.valid,reason:cap.supported&&status.valid?null:"sub_patch_unavailable"};
}
export const requiresSubPatch=cc=>full.has(cc);

