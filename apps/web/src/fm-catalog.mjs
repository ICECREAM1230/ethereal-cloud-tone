import {
  FM_SCHEMA_VERSION, FM_SCHEMA_SHA256, FM_PROFILES_SHA256, FM_CONTRACT_SHA256,
  FM_SCOPE_PATCH, FM_SCOPE_PATCH_OPERATOR, FM_SCOPE_VOICE, FM_SCOPE_VOICE_OPERATOR,
  FM_VALUE_TYPE_U32, FM_UNIT_CHIP_CODE, FM_APPLY_TIME_NEXT_NOTE,
  getProfile, getParameter, getPreset, profileOperatorCount
} from './generated/fm-contract.mjs';

const requireProfile = profileId => {
  const profile = getProfile(profileId);
  if (!profile) throw new RangeError('Unknown FM catalog profile ID');
  return profile;
};
const operatorLabel = (profile,index) => profile.operator_labels[index] ?? `算子 ${index+1}`;
const catalogParameter = descriptor => {
  if (descriptor.value_type_id !== FM_VALUE_TYPE_U32)
    throw new RangeError('Unsupported FM catalog parameter value type');
  return {
    id:String(descriptor.id), label:descriptor.name, kind:'number',
    value:null, editable:false, min:descriptor.minimum, max:descriptor.maximum, step:1,
    unit:descriptor.unit_id === FM_UNIT_CHIP_CODE ? '芯片代码' : descriptor.unit,
    hint:descriptor.apply_time_id === FM_APPLY_TIME_NEXT_NOTE
      ? 'next_note：下次起音时生效；目录不含当前读数' : descriptor.apply_time,
    reason:'目录资料，当前值尚未读取'
  };
};

/**
 * Make an internal, read-only presentation model from the frozen catalog.
 * This has no runtime values or transport capability. available:true means the
 * catalog can be browsed; editable:false and value:null never grant a write.
 * IDs (voice:<index>, operator:<index>, patch-operator:<index>) are presentation
 * identities only. The static catalog context cannot replace a live contextId.
 * Unknown profile IDs and invalid voice indices throw RangeError.
 */
export function createFmCatalogPreview(profileId, selectedVoiceIndex = 0) {
  const profile = requireProfile(profileId);
  if (profileOperatorCount(profileId,selectedVoiceIndex) === null)
    throw new RangeError('Invalid FM catalog voice index');
  const descriptors = profile.parameters.map(parameter=>getParameter(profileId,parameter.id));
  const scopes = [FM_SCOPE_PATCH,FM_SCOPE_PATCH_OPERATOR,FM_SCOPE_VOICE,FM_SCOPE_VOICE_OPERATOR];
  if (descriptors.some(descriptor=>!descriptor || !scopes.includes(descriptor.scope_id)))
    throw new RangeError('Unsupported FM catalog parameter scope');
  const parameters = scope => descriptors.filter(descriptor=>descriptor.scope_id === scope).map(catalogParameter);
  const voices = Array.from({length:profile.voice_count},(_,voiceIndex)=>({
    id:`voice:${voiceIndex}`, label:`声部 ${voiceIndex+1}`, editable:false,
    parameters:parameters(FM_SCOPE_VOICE),
    operators:Array.from({length:profileOperatorCount(profileId,voiceIndex)},(_,operatorIndex)=>({
      id:`operator:${operatorIndex}`, label:operatorLabel(profile,operatorIndex), editable:false,
      parameters:parameters(FM_SCOPE_VOICE_OPERATOR)
    }))
  }));
  return {
    contextId:`catalog:${FM_CONTRACT_SHA256}:${profile.id}:${profile.profile_version}`,
    backend:{id:String(profile.backend_id),label:profile.key,editable:false},
    available:true, editable:false,
    reason:`${profile.role === 'offline_reference' ? '离线参考目录' : '离线参数目录'}；当前参数值尚未读取，FM 演奏尚未启用。`,
    selectedVoiceId:voices[selectedVoiceIndex].id, commonLabel:'共享音色（Patch）',
    parameters:parameters(FM_SCOPE_PATCH),
    patchOperators:Array.from({length:profile.patch_operator_count},(_,operatorIndex)=>({
      id:`patch-operator:${operatorIndex}`,label:operatorLabel(profile,operatorIndex),editable:false,
      parameters:parameters(FM_SCOPE_PATCH_OPERATOR)
    })),
    voices
  };
}

/** Catalog and preset documentation only; excludes preset values/apply actions. */
export function getFmCatalogMetadata(profileId) {
  const profile = requireProfile(profileId);
  return {
    profileId:profile.id, profileKey:profile.key, backendId:profile.backend_id,
    profileVersion:profile.profile_version, role:profile.role, runtimeAvailable:profile.runtime_available,
    voiceCount:profile.voice_count,
    operatorCounts:Array.from({length:profile.voice_count},(_,index)=>profileOperatorCount(profileId,index)),
    patchOperatorCount:profile.patch_operator_count, operatorLabels:[...profile.operator_labels],
    parameterCount:profile.parameter_count, presetCount:profile.preset_count,
    schemaVersion:FM_SCHEMA_VERSION, schemaSha256:FM_SCHEMA_SHA256,
    profileSha256:FM_PROFILES_SHA256, contractSha256:FM_CONTRACT_SHA256,
    presets:profile.presets.map(item=>{
      const {id,name,description} = getPreset(profileId,item.id);
      return {id,name,description};
    })
  };
}
