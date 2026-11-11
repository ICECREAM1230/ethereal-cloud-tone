package cn.fpgasynth.control.core;

import cn.fpgasynth.control.generated.FmContract;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

/** Consumes the owner's fixed fixtures; never sends HTTP/SPI or claims runtime capability. */
public final class GeneratedContractTest {
    private static int checks;
    private static void check(boolean value, String message) {
        checks++; if (!value) throw new AssertionError(message);
    }
    private static long n(Map<String,Object> value, String key) { return Json.number(value.get(key)); }
    private static long[] words(Object value) {
        List<Object> values = Json.list(value); long[] out = new long[values.size()];
        for (int i=0;i<out.length;i++) out[i]=Json.number(values.get(i));
        return out;
    }
    private static void rejects(Runnable action) {
        boolean rejected=false;
        try { action.run(); } catch (IllegalArgumentException expected) { rejected=true; }
        check(rejected,"expected typed codec rejection");
    }
    private static void verifyMasks(Map<String,Object> schema) throws Exception {
        long flags=0, features=0;
        for(Map.Entry<String,Object> entry:Json.map(schema.get("constants")).entrySet()) {
            String key=entry.getKey();
            if(!key.startsWith("FLAG_") && !key.startsWith("FEATURE_")) continue;
            long expected=Json.number(entry.getValue());
            long actual=FmContract.class.getField("FM_"+key).getLong(null);
            check(actual==expected,"frozen schema mask "+key);
            check(expected>0 && Long.bitCount(expected)==1,"single-bit mask "+key);
            long prior=key.startsWith("FLAG_")?flags:features;
            check((prior&expected)==0,"distinct mask "+key);
            if(key.startsWith("FLAG_")) flags|=expected;else features|=expected;
        }
        check(flags!=0 && features!=0,"both generated mask namespaces provided");
        check(FmContract.FM_FLAG_READY!=FmContract.FM_FLAG_SELECTED,"READY and SELECTED are independent");
        check((FmContract.FM_FLAG_READY&FmContract.FM_FLAG_SELECTED)==0,"ready-only does not imply selected");
    }
    public static void main(String[] args) throws Exception {
        Map<String,Object> fixture=Json.object(new String(Files.readAllBytes(Paths.get(args[0])),StandardCharsets.UTF_8));
        Map<String,Object> manifest=Json.object(new String(Files.readAllBytes(Paths.get(args[1])),StandardCharsets.UTF_8));
        check(FmContract.FM_SCHEMA_VERSION.equals(manifest.get("schema_version")),"schema version");
        check(FmContract.FM_SCHEMA_SHA256.equals(manifest.get("schema_sha256")),"schema SHA");
        check(FmContract.FM_PROFILES_SHA256.equals(manifest.get("profiles_sha256")),"profiles SHA");
        check(FmContract.FM_CONTRACT_SHA256.equals(manifest.get("combined_contract_sha256")),"combined identity");
        check(FmContract.abiCompatible(FmContract.FM_ABI_VERSION_WORD),"ABI match");
        check(!FmContract.abiCompatible(0),"unknown ABI must fail");
        verifyMasks(Json.object(new String(Files.readAllBytes(Paths.get(args[2])),StandardCharsets.UTF_8)));

        List<Object> fields=Json.list(fixture.get("command_fields"));
        check(fields.size()==FmContract.FM_COMMAND_WORD_COUNT,"fixture word count");
        Class<?>[] types=new Class<?>[fields.size()]; Arrays.fill(types,long.class);
        Constructor<FmContract.Command> constructor=FmContract.Command.class.getConstructor(types);
        for (Object entry:Json.list(fixture.get("commands"))) {
            Map<String,Object> row=Json.map(entry), command=Json.map(row.get("command"));
            Object[] values=new Object[fields.size()];
            for(int i=0;i<values.length;i++) values[i]=n(command,(String)fields.get(i));
            FmContract.Command dto=constructor.newInstance(values);
            long[] expected=words(row.get("words"));
            check(Arrays.equals(FmContract.serializeCommand(dto),expected),"fixed field layout "+row.get("name"));
            check(Arrays.equals(FmContract.serializeCommand(FmContract.deserializeCommand(expected)),expected),"fixed word roundtrip");
            for(int field=0;field<values.length;field++) {
                for(Object invalid:Json.list(fixture.get("invalid_java_uint32"))) {
                    Object[] bad=values.clone();bad[field]=Json.number(invalid);boolean rejected=false;
                    try { constructor.newInstance(bad); }
                    catch(InvocationTargetException e) { if(e.getCause() instanceof IllegalArgumentException) rejected=true;else throw e; }
                    check(rejected,"u32 narrowing boundary "+fields.get(field));
                }
            }
        }
        for(Object invalid:Json.list(fixture.get("invalid_word_counts"))) {
            long[] bad=new long[(int)Json.number(invalid)];
            rejects(()->FmContract.deserializeCommand(bad));
        }

        for(String key:new String[]{"jt51","msfa_reference"}) {
            Map<String,Object> expected=Json.map(fixture.get(key));long id=n(expected,"backend_id");
            FmContract.Profile profile=FmContract.getProfile(id);
            check(profile!=null,"expected fixed profile");
            check(profile.backend_id==id && profile.profile_version==n(expected,"profile_version"),"profile identity");
            check(!profile.runtime_available,"offline catalog must never enable FM");
            check(profile.voice_count==n(expected,"voice_count"),"voice descriptor");
            check(profile.patch_operator_count==n(expected,"patch_operator_count"),"patch descriptor");
            check(profile.parameter_count==n(expected,"parameter_count") && profile.preset_count==n(expected,"preset_count"),"catalog counts");
            long[] counts=words(expected.get("operators_per_voice"));
            long staticVoices=key.equals("jt51")?FmContract.FM_PROFILE_JT51_OPM_VOICE_COUNT:FmContract.FM_PROFILE_MSFA_TWO_OP_REFERENCE_VOICE_COUNT;
            long staticPatch=key.equals("jt51")?FmContract.FM_PROFILE_JT51_OPM_PATCH_OPERATOR_COUNT:FmContract.FM_PROFILE_MSFA_TWO_OP_REFERENCE_PATCH_OPERATOR_COUNT;
            long staticMax=key.equals("jt51")?FmContract.FM_PROFILE_JT51_OPM_MAX_OPERATOR_COUNT:FmContract.FM_PROFILE_MSFA_TWO_OP_REFERENCE_MAX_OPERATOR_COUNT;
            long expectedMax=0;for(long count:counts) expectedMax=Math.max(expectedMax,count);
            check(staticVoices==n(expected,"voice_count"),"generated static voice count");
            check(staticPatch==n(expected,"patch_operator_count"),"generated static patch count");
            check(staticMax==expectedMax,"generated static capacity; not selected-voice topology");
            for(int voice=0;voice<counts.length;voice++) check(FmContract.profileOperatorCount(id,voice)==counts[voice],"selected voice count");
            check(FmContract.profileOperatorCount(id,profile.voice_count)<0,"out-of-range voice");
        }
        for(Object entry:Json.list(fixture.get("profile_targets"))) {
            Map<String,Object> row=Json.map(entry);long id=n(row,"backend_id"),target=n(row,"target");
            FmContract.Profile profile=FmContract.getProfile(id);boolean actual=false;
            if(profile!=null) {
                long selected=FmContract.profileOperatorCount(id,FmContract.targetVoice(target));
                actual=FmContract.validateTarget(n(row,"scope"),target,profile.voice_count,Math.max(0,selected),profile.patch_operator_count);
            } else {
                // Synthetic dimensions exercise only the generic codec, not an installed profile/backend.
                for(Object candidate:Json.list(fixture.get("synthetic_profiles"))) {
                    Map<String,Object> synthetic=Json.map(candidate);
                    if(n(synthetic,"backend_id")!=id) continue;
                    long voice=FmContract.targetVoice(target);long[] counts=words(synthetic.get("operators_per_voice"));
                    long selected=voice<counts.length?counts[(int)voice]:0;
                    actual=FmContract.validateTarget(n(row,"scope"),target,n(synthetic,"voice_count"),selected,n(synthetic,"patch_operator_count"));
                }
            }
            check(actual==Json.bool(row.get("expected")),"fixed target fixture "+row.get("name"));
        }
        Map<String,Object> jt=Json.map(fixture.get("jt51"));long id=n(jt,"backend_id");
        long[] slots=words(jt.get("native_register_slots")),bits=words(jt.get("native_key_on_bits"));
        for(int op=0;op<slots.length;op++) {
            check(FmContract.profileNativeRegisterSlot(id,op)==slots[op],"native register mapping");
            check(FmContract.profileNativeKeyOnBit(id,op)==bits[op],"independent key-on mapping");
        }
        for(Object entry:Json.list(jt.get("parameters"))) {
            Map<String,Object> expected=Json.map(entry);FmContract.Parameter p=FmContract.getParameter(id,n(expected,"id"));
            check(p!=null && p.name.equals(expected.get("key")),"fixed parameter name/id");
            check(p.scope_id==n(expected,"scope") && p.minimum==n(expected,"min") && p.maximum==n(expected,"max"),"fixed parameter metadata");
            check(p.unit_id==FmContract.FM_UNIT_CHIP_CODE && p.apply_time_id==FmContract.FM_APPLY_TIME_NEXT_NOTE,"chip-code/next-note metadata");
            long op=p.scope_id==FmContract.FM_SCOPE_PATCH?FmContract.FM_TARGET_NONE:0;
            long target=FmContract.packTarget(FmContract.FM_TARGET_NONE,op);
            check(FmContract.validateParameter(id,p.id,p.scope_id,target,p.minimum),"minimum parameter");
            check(FmContract.validateParameter(id,p.id,p.scope_id,target,p.maximum),"maximum parameter");
            check(!FmContract.validateParameter(id,p.id,p.scope_id,target,p.minimum-1),"below parameter");
            check(!FmContract.validateParameter(id,p.id,p.scope_id,target,p.maximum+1),"above parameter");
        }
        for(Object preset:Json.list(jt.get("presets"))) {
            FmContract.Preset p=FmContract.getPreset(id,Json.number(preset));check(p!=null,"fixed preset");
            for(long i=0;i<p.values_count;i++) {
                FmContract.PresetValue value=p.value(i);FmContract.Parameter parameter=FmContract.getParameter(id,value.parameter_id);
                check(FmContract.validateParameter(id,value.parameter_id,parameter.scope_id,FmContract.packTarget(FmContract.FM_TARGET_NONE,value.operator_index),value.value_u32),"preset value range/target");
            }
        }
        System.out.println("RESULT "+checks+" App generated-contract assertions passed; fixed codec/catalog/mapping/synthetic dimensions only; FM HTTP/hardware disabled");
    }
}
