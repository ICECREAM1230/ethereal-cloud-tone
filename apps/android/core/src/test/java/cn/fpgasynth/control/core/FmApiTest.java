package cn.fpgasynth.control.core;

import cn.fpgasynth.control.generated.FmContract;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.LinkedHashMap;
import java.util.Map;

/** Actual frozen handler JSON plus synthetic edge inputs; no hardware discovery claims. */
public final class FmApiTest {
    private static Path fixtures;
    private static int checks,groups;
    private static long profileId;
    private FmApiTest() {}
    private interface Action { void run() throws Exception; }
    public static void main(String[] args) throws Exception {
        fixtures=Paths.get(args[0]);profileId=Json.number(object("v2-profile.json").get("profile_id"));
        String group=args.length>1?args[1]:"all";
        if(group.equals("all")||group.equals("u32"))run("strict JSON uint32",FmApiTest::uint32);
        if(group.equals("all")||group.equals("caps"))run("capability identity and limits",FmApiTest::capabilities);
        if(group.equals("all")||group.equals("state"))run("runtime status and FM safety gates",FmApiTest::states);
        if(group.equals("all")||group.equals("catalog"))run("generated catalog and bounded pages",FmApiTest::catalogs);
        if(group.equals("all")||group.equals("encode"))run("seven uint32 fields and actual FM targets",FmApiTest::commands);
        if(group.equals("layout"))run("synthetic heterogeneous VOICE_OPERATOR actual target",FmApiTest::heterogeneous);
        if(groups==0)throw new IllegalArgumentException("unknown test group");
        System.out.println("FmApi: "+groups+" groups, "+checks+" assertions passed; profile="+profileId+"; host JSON only");
    }
    private static void run(String label,Action action) throws Exception {
        try{action.run();groups++;System.out.println("PASS "+label);}
        catch(Exception failure){throw new AssertionError(label+" failed",failure);}
    }
    private static void check(boolean value,String label){checks++;if(!value)throw new AssertionError(label);}
    private static void equal(String want,String actual,String label){check(want.equals(actual),label+": "+actual);}
    private static void reject(Action action,String label) throws Exception {
        try{action.run();}catch(IllegalArgumentException|IOException expected){checks++;return;}
        throw new AssertionError("accepted "+label);
    }
    private static String text(String name) throws IOException{return new String(Files.readAllBytes(fixtures.resolve(name)),StandardCharsets.UTF_8);}
    private static Map<String,Object> object(String name) throws IOException{return Json.object(text(name));}
    private static Map<String,Object> copy(Map<String,Object> map){return Json.object(Json.stringify(map));}
    private static Map<String,Object> nested(Map<String,Object> map,String key){return Json.map(map.get(key));}
    private static Map<String,Object> row(Map<String,Object> map,String key,int index){return Json.map(Json.list(map.get(key)).get(index));}
    private static FmApi.Caps caps(boolean live) throws IOException{return new FmApi.Caps(text(live?"v2-live-capabilities.json":"v2-capabilities.json"));}
    private static FmApi.State state(boolean live) throws IOException{return new FmApi.State(text(live?"v2-live-state.json":"v2-state.json"),caps(live));}
    private static FmApi.State altered(Map<String,Object> raw) throws IOException{return new FmApi.State(Json.stringify(raw),caps(true));}
    private static void uint32() throws Exception {
        check(FmApi.u32(0L)==0,"uint32 zero");check(FmApi.u32(4294967295L)==4294967295L,"full word");
        for(Object value:new Object[]{null,-1L,4294967296L,1,1.0,true,"1"})reject(()->FmApi.u32(value),"non-JSON uint32");
        reject(()->FmApi.u32(Json.object("{\"v\":1.0}").get("v")),"fractional JSON");
    }
    private static void capabilities() throws Exception {
        FmApi.Caps offline=caps(false),live=caps(true);
        check(offline.context==9&&offline.contractMatches,"actual unsupported caps identity");
        check(offline.engineAvailable(0)&&offline.engineAvailable(1)&&!offline.engineAvailable(2),"actual unsupported runtime");
        check(live.context==10&&live.engineAvailable(2),"actual live runtime");
        check(live.profiles.size()==2&&live.profiles.get(0).id==profileId,"directory IDs");
        check(!live.profiles.get(0).runtime_available,"generated offline flag remains false");
        check(!live.engineAvailable(-1)&&!live.engineAvailable(3),"unknown engine unavailable");
        Map<String,Object> offlineFlag=object("v2-live-capabilities.json");row(offlineFlag,"catalog_profiles",0).put("runtime_available",false);
        check(new FmApi.Caps(Json.stringify(offlineFlag)).engineAvailable(2),"ignore offline runtime flag");
        for(String digest:new String[]{"schema_sha256","profiles_sha256","contract_sha256"}){
            Map<String,Object> bad=object("v2-live-capabilities.json");bad.put(digest,"bad");FmApi.Caps c=new FmApi.Caps(Json.stringify(bad));
            check(!c.contractMatches&&!c.engineAvailable(2)&&c.engineAvailable(0),"digest FM gate");
            check(!c.incompatibility.isEmpty(),"digest explanation");
        }
        for(String field:new String[]{"api_major","api_minor","context_epoch"}){
            Map<String,Object> bad=object("v2-capabilities.json");bad.put(field,field.equals("context_epoch")?0L:Json.number(bad.get(field))+1);
            reject(()->new FmApi.Caps(Json.stringify(bad)),"API/context "+field);
        }
        for(String field:new String[]{"clients","key_count","lease_ms","max_body_bytes","catalog_page_max"}){
            Map<String,Object> bad=object("v2-capabilities.json");Map<String,Object> limit=nested(bad,"limits");limit.put(field,Json.number(limit.get(field))+1);
            reject(()->new FmApi.Caps(Json.stringify(bad)),"limit "+field);
        }
        Map<String,Object> duplicate=object("v2-capabilities.json");Json.list(duplicate.get("catalog_profiles")).add(copy(row(duplicate,"catalog_profiles",0)));
        reject(()->new FmApi.Caps(Json.stringify(duplicate)),"duplicate profile");
        Map<String,Object> unknown=object("v2-capabilities.json");row(unknown,"catalog_profiles",0).put("id",4294967295L);
        check(!new FmApi.Caps(Json.stringify(unknown)).contractMatches,"unknown directory profile");
        Map<String,Object> tuple=object("v2-capabilities.json");row(tuple,"catalog_profiles",0).put("backend_id",2L);
        check(!new FmApi.Caps(Json.stringify(tuple)).contractMatches,"backend/profile tuple mismatch");
        Map<String,Object> engine=object("v2-capabilities.json");row(engine,"engines",2).put("id",1L);
        reject(()->new FmApi.Caps(Json.stringify(engine)),"duplicate engine");
        Map<String,Object> missing=object("v2-capabilities.json");missing.remove("catalog_profiles");
        check(!new FmApi.Caps(Json.stringify(missing)).engineAvailable(2),"missing FM catalog is unsupported");
    }
    private static void states() throws Exception {
        FmApi.State dds=state(false),live=state(true);
        for(String digest:new String[]{"schema_sha256","profiles_sha256","contract_sha256"}){
            Map<String,Object> bad=object("v2-live-capabilities.json");bad.put(digest,"bad");
            check(new FmApi.State(text("v2-state.json"),new FmApi.Caps(Json.stringify(bad))).playable(),"DDS survives FM digest mismatch");
        }
        check(dds.playable()&&!dds.canSelectFm()&&dds.fm.voiceCount==0,"actual no-FM state");
        check(live.playable()&&live.canSelectFm()&&live.fm.profile.id==profileId,"actual live state");
        check(live.fm.backendId==1&&live.fm.session==42&&live.fm.epoch==4&&live.context==10,"HTTP/native identities separated");
        check(live.fm.operators.length==8&&live.fm.voiceGenerations.length==8&&live.fm.patchCount==4,"actual dimensions");
        check(live.fm.parameterValues.size()==1&&live.fm.hasFeature(FmContract.FM_FEATURE_SET_PARAMETER),"completion cache/feature");
        for(boolean remove:new boolean[]{true,false}){
            Map<String,Object> raw=object("v2-state.json");if(remove)raw.remove("fm");else raw.put("fm",null);
            check(new FmApi.State(Json.stringify(raw),caps(false)).playable(),"absent/null FM leaves DDS");
        }
        for(long flag:new long[]{FmContract.FM_FLAG_MUTED,FmContract.FM_FLAG_BUSY,FmContract.FM_FLAG_FAULT}){
            Map<String,Object> raw=object("v2-live-state.json");nested(raw,"fm").put("flags",3L|flag);FmApi.State s=altered(raw);
            check(!s.playable(),"unsafe flag");check(s.canSelectFm()==(flag==FmContract.FM_FLAG_MUTED),"SELECT gate");
        }
        Map<String,Object> unselected=object("v2-live-state.json");nested(unselected,"fm").put("flags",5L);
        check(!altered(unselected).playable()&&altered(unselected).canSelectFm(),"ready/muted/unselected selectable");
        Map<String,Object> noReady=object("v2-live-state.json");nested(noReady,"fm").put("flags",2L);
        check(!altered(noReady).playable()&&!altered(noReady).canSelectFm(),"READY required");
        for(String identity:new String[]{"session_id","engine_epoch","topology_generation"}){
            Map<String,Object> raw=object("v2-live-state.json");nested(raw,"fm").put(identity,0L);
            check(!altered(raw).playable(),"native identity zero");
        }
        for(long feature:new long[]{FmContract.FM_FEATURE_NOTE_ON,FmContract.FM_FEATURE_NOTE_OFF}){
            Map<String,Object> raw=object("v2-live-state.json");nested(raw,"fm").put("features",1023L&~feature);
            check(!altered(raw).playable(),"note feature declaration");
        }
        for(String gate:new String[]{"switching","recovering"}){
            Map<String,Object> raw=object("v2-live-state.json");nested(raw,"mode").put(gate,true);
            check(!altered(raw).playable()&&!altered(raw).canSelectFm(),"mode "+gate);
        }
        Map<String,Object> unsettled=object("v2-live-state.json");nested(unsettled,"mode").put("requested",0L);
        check(!altered(unsettled).playable()&&!altered(unsettled).canSelectFm(),"stable mode equality");
        Map<String,Object> phase=object("v2-live-state.json");nested(phase,"fm").put("phase",1L);
        check(!altered(phase).playable()&&!altered(phase).canSelectFm(),"phase READY");
        for(String identity:new String[]{"profile_id","backend_id","profile_version","profile_sha256"}){
            Map<String,Object> raw=object("v2-live-state.json");nested(raw,"fm").put(identity,identity.equals("profile_sha256")?"bad":4294967295L);FmApi.State s=altered(raw);
            check(s.fm.profile==null&&!s.playable()&&!s.canSelectFm()&&!s.fm.reason.isEmpty(),"runtime tuple mismatch");
        }
        for(String dimension:new String[]{"voice_count","patch_operator_count"}){
            Map<String,Object> raw=object("v2-live-state.json");Map<String,Object> fm=nested(raw,"fm");fm.put(dimension,Json.number(fm.get(dimension))-1);
            reject(()->altered(raw),"profile dimension "+dimension);
        }
        Map<String,Object> ops=object("v2-live-state.json");Json.list(nested(ops,"fm").get("operators_per_voice")).set(0,3L);
        reject(()->altered(ops),"actual selected voice operator layout");
        Map<String,Object> gens=object("v2-live-state.json");Json.list(nested(gens,"fm").get("voice_generations")).remove(0);
        reject(()->altered(gens),"voice generation dimensions");
        for(String field:new String[]{"flags","features","phase","voice_count"}){
            Map<String,Object> raw=object("v2-live-state.json");nested(raw,"fm").put(field,4294967295L);reject(()->altered(raw),"status budget "+field);
        }
        Map<String,Object> rate=object("v2-live-state.json");nested(nested(rate,"fm"),"source_rate").put("denominator",0L);
        reject(()->altered(rate),"zero rate denominator");
        Map<String,Object> id=object("v2-live-state.json");nested(nested(id,"fm"),"accepted").put("sequence_id",4294967296L);
        reject(()->altered(id),"native identity overflow");
        Map<String,Object> historic=object("v2-live-state.json");nested(nested(historic,"fm"),"completed").put("session_id",7L);
        check(altered(historic).playable(),"old completed command identity preserved");
        for(String field:new String[]{"parameter_id","target_word","value_u32","sequence_id"}){
            Map<String,Object> raw=object("v2-live-state.json");row(nested(raw,"fm"),"parameter_values",0).put(field,field.equals("sequence_id")?0L:4294967295L);
            reject(()->altered(raw),"confirmed cache "+field);
        }
        Map<String,Object> cache=object("v2-live-state.json");Json.list(nested(cache,"fm").get("parameter_values")).add(copy(row(nested(cache,"fm"),"parameter_values",0)));
        reject(()->altered(cache),"duplicate confirmed parameter target");
        for(String field:new String[]{"keys","result_code","token"}){
            Map<String,Object> raw=object("v2-state.json");row(raw,"clients",0).put(field,field.equals("keys")?33554432L:4294967296L);
            reject(()->new FmApi.State(Json.stringify(raw),caps(false)),"owner "+field);
        }
        Map<String,Object> ack=object("v2-live-state.json");Map<String,Object> client=row(ack,"clients",0);
        client.put("token",4294967295L);client.put("processed",9L);client.put("parameter_processed",8L);client.put("stop_processed",7L);
        client.put("keys",1L);client.put("result_sequence",9L);client.put("result_code",5L);client.put("result_context_epoch",9L);
        FmApi.State s=altered(ack);check(s.tokens[0]==4294967295L&&s.processed[0]==9&&s.parameters[0]==8&&s.stops[0]==7
                &&s.keys[0]==1&&s.resultSequences[0]==9&&s.resultCodes[0]==5&&s.resultContexts[0]==9,"independent owner acknowledgements");
    }
    private static final class FixtureTransport implements Transport {
        final Map<String,String> bodies=new LinkedHashMap<>();
        int code=200;
        FixtureTransport() throws IOException{for(String file:new String[]{"v2-profile.json","v2-catalog.json","v2-catalog-page-8.json","v2-presets.json"})bodies.put(path(file),text(file));}
        private String path(String file){
            String suffix=file.equals("v2-profile.json")?"profile":file.equals("v2-presets.json")?"presets&offset=0&limit=8":
                    file.equals("v2-catalog.json")?"parameters&offset=0&limit=8":"parameters&offset=8&limit=8";
            return "/api/v2/fm/catalog?profile="+profileId+"&section="+suffix;
        }
        @Override public Response request(String path,String body) throws IOException {
            if(body!=null||!bodies.containsKey(path))throw new IOException("unexpected catalog GET: "+path);return new Response(code,bodies.get(path));
        }
        void put(String file,Map<String,Object> raw){bodies.put(path(file),Json.stringify(raw));}
        void mutate(String file,String key,Object value){Map<String,Object> raw=Json.object(bodies.get(path(file)));raw.put(key,value);put(file,raw);}
    }
    private static FmApi.Catalog catalog(FixtureTransport t) throws IOException{return FmApi.Catalog.load(t,caps(true),state(true));}
    private static void catalogs() throws Exception {
        FmApi.Catalog c=catalog(new FixtureTransport());
        check(c.profile.id==profileId&&c.profile.backend_id==1&&c.parameters.size()==13&&c.presets.size()==3,"actual generated catalog");
        check(c.parameters.get(4).id==18&&c.parameters.get(4).maximum==31&&c.presets.get(2).id==2,"parameter/preset metadata");
        for(String field:new String[]{"profile_id","backend_id","profile_version","profiles_sha256","key","voice_count","patch_operator_count","parameter_count","preset_count"}){
            FixtureTransport t=new FixtureTransport();t.mutate("v2-profile.json",field,field.equals("key")||field.equals("profiles_sha256")?"bad":4294967295L);
            reject(()->catalog(t),"profile drift "+field);
        }
        for(String field:new String[]{"operator_labels","operators_per_voice"}){
            Map<String,Object> raw=object("v2-profile.json");Json.list(raw.get(field)).set(0,field.equals("operator_labels")?"bad":3L);FixtureTransport t=new FixtureTransport();t.put("v2-profile.json",raw);
            reject(()->catalog(t),"profile layout "+field);
        }
        for(String field:new String[]{"id","name","minimum","maximum","scope_id","unit_id","apply_time_id","value_type_id"}){
            Map<String,Object> raw=object("v2-catalog.json");row(raw,"items",0).put(field,field.equals("name")?"bad":4294967295L);FixtureTransport t=new FixtureTransport();t.put("v2-catalog.json",raw);
            reject(()->catalog(t),"parameter metadata "+field);
        }
        for(String field:new String[]{"profile_id","backend_id","profile_version","profiles_sha256","total","offset","next_offset","section"}){
            FixtureTransport t=new FixtureTransport();t.mutate("v2-catalog-page-8.json",field,field.equals("section")||field.equals("profiles_sha256")?"bad":4294967295L);
            reject(()->catalog(t),"page drift "+field);
        }
        for(Object next:new Object[]{null,0L,7L,9L}){
            FixtureTransport t=new FixtureTransport();t.mutate("v2-catalog.json","next_offset",next);reject(()->catalog(t),"invalid next offset");
        }
        for(int delta:new int[]{-1,0,1}){
            Map<String,Object> raw=object("v2-catalog-page-8.json");
            if(delta<0)Json.list(raw.get("items")).remove(0);else if(delta==0)Json.list(raw.get("items")).set(1,copy(row(raw,"items",0)));
            else Json.list(raw.get("items")).add(copy(row(raw,"items",0)));
            FixtureTransport t=new FixtureTransport();t.put("v2-catalog-page-8.json",raw);reject(()->catalog(t),"missing/duplicate/extra parameter");
        }
        for(String field:new String[]{"id","name","key_on_logical_mask","values_count"}){
            Map<String,Object> raw=object("v2-presets.json");row(raw,"items",0).put(field,field.equals("name")?"bad":4294967295L);FixtureTransport t=new FixtureTransport();t.put("v2-presets.json",raw);
            reject(()->catalog(t),"preset metadata "+field);
        }
        Map<String,Object> duplicate=object("v2-presets.json");Json.list(duplicate.get("items")).set(1,copy(row(duplicate,"items",0)));FixtureTransport t=new FixtureTransport();t.put("v2-presets.json",duplicate);
        reject(()->catalog(t),"duplicate preset");
        FixtureTransport error=new FixtureTransport();error.code=503;reject(()->catalog(error),"catalog HTTP error");
        reject(()->FmApi.Catalog.load(new FixtureTransport(),caps(false),state(false)),"no actual matched runtime");
    }
    private static String encode(FmApi.State s,FmApi.Catalog c,int op,long a,long b,long value){return FmApi.encode(1,1,s.context,op,a,b,value,s,c);}
    private static void commands() throws Exception {
        FmApi.State dds=state(false),fm=state(true);FmApi.Catalog c=catalog(new FixtureTransport());
        equal("4294967295 4294967295 9 2 7 127 0",FmApi.encode(4294967295L,4294967295L,9,2,7,127,0,dds,null),"full token/sequence");
        equal("1 1 9 3 33554431 127 0",encode(dds,null,3,33554431,127,0),"25-bit keys");
        equal("1 1 9 6 200 0 0",encode(dds,null,6,200,0,0),"old KS range");
        equal("1 1 10 7 18 4294901763 31",encode(fm,c,7,18,4294901763L,31),"PATCH_OPERATOR");
        equal("1 1 10 7 1 4294967295 7",encode(fm,c,7,1,4294967295L,7),"PATCH full word");
        equal("1 1 10 8 2 0 0",encode(fm,c,8,2,0,0),"preset");
        equal("1 1 10 5 0 0 0",encode(fm,c,5,0,0,0),"leave FM");
        Map<String,Object> raw=object("v2-live-state.json");nested(raw,"fm").put("flags",5L);nested(raw,"mode").put("requested",0L);nested(raw,"mode").put("applied",0L);FmApi.State ready=altered(raw);
        equal("1 1 10 5 2 1 0",encode(ready,c,5,2,1,0),"SELECT uses backend id");
        if(profileId!=1)reject(()->encode(ready,c,5,2,profileId,0),"directory id as backend");
        equal("1 1 10 3 0 1 0",encode(ready,c,3,0,1,0),"safe zero when unselected");
        equal("1 1 10 4 0 0 0",encode(ready,c,4,0,0,0),"safe STOP");
        raw=object("v2-live-state.json");nested(raw,"fm").put("session_id",0L);FmApi.State reset=altered(raw);
        equal("1 1 10 3 0 1 0",encode(reset,c,3,0,1,0),"safe zero after native reset");
        equal("1 1 10 4 0 0 0",encode(reset,c,4,0,0,0),"safe STOP after native reset");
        reject(()->encode(reset,c,3,1,1,0),"nonzero after reset");
        reject(()->encode(ready,c,3,1,1,0),"nonzero before selected");
        reject(()->encode(ready,c,7,18,4294901760L,31),"parameter before selected");
        reject(()->encode(fm,null,7,18,4294901760L,31),"no catalog");
        reject(()->encode(dds,c,7,18,4294901760L,31),"parameter in DDS");
        reject(()->encode(fm,c,7,18,4294901764L,31),"operator out of range");
        reject(()->encode(fm,c,7,18,0,31),"PATCH_OPERATOR voice must NONE");
        reject(()->encode(fm,c,7,18,4294901760L,32),"parameter range");
        reject(()->encode(fm,c,7,999,4294901760L,1),"unknown parameter");
        reject(()->encode(fm,c,8,3,0,0),"unknown preset");
        for(long word:new long[]{0,-1,4294967296L}){
            reject(()->FmApi.encode(word,1,9,4,0,0,0,dds,null),"token");reject(()->FmApi.encode(1,word,9,4,0,0,0,dds,null),"sequence");
            reject(()->FmApi.encode(1,1,word,4,0,0,0,dds,null),"context");
        }
        reject(()->FmApi.encode(1,1,8,4,0,0,0,dds,null),"stale context");
        long[][] invalid={{1,0,0,0},{2,64,1,0},{2,7,128,0},{2,7,1,1},{3,33554432L,1,0},{3,1,0,0},{4,1,0,0},
                {5,2,2,0},{5,1,1,0},{6,199,0,0},{6,8001,0,0},{8,0,1,0},{7,18,-1,31}};
        for(long[] v:invalid)reject(()->encode(dds,null,(int)v[0],v[1],v[2],v[3]),"bad seven-field command");
        for(long feature:new long[]{FmContract.FM_FEATURE_SET_PARAMETER,FmContract.FM_FEATURE_APPLY_PRESET}){
            Map<String,Object> noFeature=object("v2-live-state.json");nested(noFeature,"fm").put("features",1023L&~feature);FmApi.State s=altered(noFeature);
            reject(()->encode(s,c,feature==FmContract.FM_FEATURE_SET_PARAMETER?7:8,feature==FmContract.FM_FEATURE_SET_PARAMETER?18:0,
                    feature==FmContract.FM_FEATURE_SET_PARAMETER?4294901760L:0,feature==FmContract.FM_FEATURE_SET_PARAMETER?31:0),"undeclared feature");
        }
        String wire=encode(fm,c,7,18,4294901760L,31);check(wire.matches("[0-9]+( [0-9]+){6}")&&wire.length()<=95,"ASCII wire");
    }
    private static void heterogeneous() throws Exception {
        FmApi.State s=state(true);FmApi.Catalog c=catalog(new FixtureTransport());
        check(s.playable()&&s.fm.profile.id==2&&s.fm.backendId==2,"synthetic matched runtime");
        check(c.parameters.size()==1&&c.presets.isEmpty()&&c.parameters.get(0).scope_id==FmContract.FM_SCOPE_VOICE_OPERATOR,"generated synthetic scope");
        equal("1 1 10 7 200 1 63",encode(s,c,7,200,1,63),"voice0 operator1");
        equal("1 1 10 7 200 65539 63",encode(s,c,7,200,65539,63),"voice1 operator3");
        equal("1 1 10 7 200 458752 63",encode(s,c,7,200,458752,63),"voice7 operator0");
        reject(()->encode(s,c,7,200,2,63),"voice0 cannot use backend maximum");
        reject(()->encode(s,c,7,200,131073,63),"voice2 one operator");
        reject(()->encode(s,c,7,200,196608,63),"voice3 zero operators");
        reject(()->encode(s,c,7,200,524288,63),"voice8 outside actual count");
        reject(()->encode(s,c,7,200,4294901760L,63),"VOICE_OPERATOR cannot use voice NONE");
        s.fm.operators[1]=2;
        reject(()->encode(s,c,7,200,65539,63),"encode also validates current selected-voice count");
    }
}
