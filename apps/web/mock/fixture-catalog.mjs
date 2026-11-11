import {readFileSync} from 'node:fs';
import {FM_SCHEMA_SHA256,FM_PROFILES_SHA256,FM_CONTRACT_SHA256,getProfile} from '../src/generated/fm-contract.mjs';

// Preserve the real 2026-10-11 handler captures as historical evidence. These
// transport/state models use their JSON structure with the current catalog.
// Only the known original identity is rebound; corrupt/unknown identities are
// left intact so the production client's compatibility checks still reject them.
const originalProfiles='51a6af55646fe2adb54bf4f9f3d06e2d262bd830ec5f30f80c0e5bfa2eadf49f';
const originalContract='86aa104f60f3e8fc586a9f64052032e45b24d7148f43610b3ef0e86c9e5fed38';
export function currentCatalogFixture(name) {
  const data=JSON.parse(readFileSync(new URL('../artifacts/fm-v2-runtime-20261011/inputs/'+name+'.json',import.meta.url)));
  if(data.profiles_sha256===originalProfiles&&data.contract_sha256===originalContract&&data.schema_sha256===FM_SCHEMA_SHA256) {
    data.profiles_sha256=FM_PROFILES_SHA256;data.contract_sha256=FM_CONTRACT_SHA256;
  }
  if(data.fm?.profile_sha256===originalProfiles)data.fm.profile_sha256=FM_PROFILES_SHA256;
  // Paired FM32 model: historical captures remain unchanged on disk.
  if(data.fm?.profile_id===1 && data.fm.profile_sha256===FM_PROFILES_SHA256) {
    const profile=getProfile(1);data.fm.voice_count=profile.voice_count;
    data.fm.operators_per_voice=[...profile.operators_per_voice];
    data.fm.voice_generations=Array.from({length:profile.voice_count},(_,i)=>data.fm.voice_generations?.[i]??0);
  }
  return data;
}
