package cn.fpgasynth.control.core;

import cn.fpgasynth.control.generated.FmContract;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Frozen HTTP v2 boundary; owner acknowledgement is not hardware or audio completion. */
public final class FmApi {
    private static final int CLIENTS=4,CATALOG_PAGE_MAX=8;
    private FmApi() {}

    public static long u32(Object value) {
        long result=Json.number(value);
        if(result<0 || result>Protocol.UINT_MAX)throw new IllegalArgumentException("expected uint32");
        return result;
    }

    public static final class Caps {
        public final long context;
        public final boolean contractMatches;
        public final String incompatibility;
        public final List<FmContract.Profile> profiles;
        public final boolean channelRoutes,subPatchRequired,subPatchSupported;
        private final boolean[] routeModes=new boolean[3];
        private final boolean[] engines=new boolean[(int)FmContract.FM_ENGINE_FM+1];
        public Caps(String json) {
            Map<String,Object> raw=Json.object(json);version(raw,true);
            context=nonzero(raw.get("context_epoch"));
            Map<String,Object> sub=raw.get("sub_patch")==null?null:Json.map(raw.get("sub_patch"));
            if(sub!=null)require(u32(sub.get("version"))==1,"unknown SUB patch version");
            subPatchRequired=sub!=null&&Json.bool(sub.get("required"));subPatchSupported=sub!=null&&Json.bool(sub.get("supported"));
            Map<String,Object> cpu=raw.get("cpu_mode")==null?null:Json.map(raw.get("cpu_mode"));
            if(cpu!=null){require(u32(cpu.get("version"))==1,"unknown CPU route version");Json.bool(cpu.get("route_only"));}
            channelRoutes=cpu!=null&&Json.bool(cpu.get("route_only"))&&Boolean.TRUE.equals(cpu.get("channel_routes"));
            if(cpu!=null&&cpu.containsKey("channel_routes"))Json.bool(cpu.get("channel_routes"));
            if(channelRoutes){
                List<Object> modes=Json.list(cpu.get("modes"));require(!modes.isEmpty()&&modes.size()<=3,"CPU route mode budget");
                for(Object mode:modes){int id=(int)bounded(mode,2,"unknown CPU route");require(!routeModes[id],"duplicate CPU route");routeModes[id]=true;}
            }
            Map<String,Object> limits=Json.map(raw.get("limits"));
            require(u32(limits.get("clients"))==CLIENTS,"unsupported client count");
            require(u32(limits.get("key_count"))==Integer.bitCount(Protocol.MASK_MAX),"unsupported key count");
            require(u32(limits.get("lease_ms"))==FmContract.FM_PERFORMANCE_LEASE_MS,"unsupported lease");
            require(u32(limits.get("max_body_bytes"))==FmContract.FM_HTTP_COMMAND_MAX_BYTES,"unsupported command length");
            require(u32(limits.get("catalog_page_max"))==CATALOG_PAGE_MAX,"unsupported catalog page size");
            List<Object> rows=Json.list(raw.get("engines"));
            require(rows.size()<=engines.length,"too many engines");Set<Long> seen=new HashSet<>();
            for(Object row:rows){
                Map<String,Object> e=Json.map(row);long id=u32(e.get("id"));
                require(id<engines.length&&seen.add(id),"unknown or duplicate engine");
                engines[(int)id]=Json.bool(e.get("available"));
            }
            String mismatch="";
            if(!FmContract.FM_SCHEMA_SHA256.equals(raw.get("schema_sha256")))mismatch="FM schema SHA256 mismatch";
            else if(!FmContract.FM_PROFILES_SHA256.equals(raw.get("profiles_sha256")))mismatch="FM profiles SHA256 mismatch";
            else if(!FmContract.FM_CONTRACT_SHA256.equals(raw.get("contract_sha256")))mismatch="FM contract SHA256 mismatch";
            List<FmContract.Profile> matched=new ArrayList<>();seen.clear();
            Object catalog=raw.get("catalog_profiles");
            if(catalog==null){
                if(mismatch.isEmpty())mismatch="FM profile catalog missing";
            }else{
                rows=Json.list(catalog);require(rows.size()<=FmContract.FM_MAX_BACKEND_PROFILES,"profile catalog budget");
                for(Object row:rows){
                    Map<String,Object> p=Json.map(row);long id=u32(p.get("id"));
                    require(seen.add(id),"duplicate profile id");
                    long backend=u32(p.get("backend_id")),profileVersion=u32(p.get("profile_version"));
                    String key=string(p.get("key"));Json.bool(p.get("runtime_available"));
                    FmContract.Profile generated=FmContract.getProfile(id);
                    if(generated==null||generated.backend_id!=backend||generated.profile_version!=profileVersion||!generated.key.equals(key)){
                        if(mismatch.isEmpty())mismatch="FM catalog profile identity mismatch";
                    }else matched.add(generated);
                }
                if(matched.isEmpty()&&mismatch.isEmpty())mismatch="FM profile catalog empty";
            }
            profiles=Collections.unmodifiableList(matched);contractMatches=mismatch.isEmpty();incompatibility=mismatch;
        }
        public boolean engineAvailable(int id) {
            return id>=0&&id<engines.length&&engines[id]&&(id!=(int)FmContract.FM_ENGINE_FM||contractMatches);
        }
        /** Performance route capability is independent of the native FM editing descriptor. */
        public boolean performanceAvailable(int id){return channelRoutes?id>=0&&id<3&&routeModes[id]:engineAvailable(id);}
    }

    public static final class State {
        public final String json;
        public final long context,requested,applied;
        public final boolean switching,recovering;
        public final DeviceState legacy;
        public final long[] tokens,processed,parameters,stops,keys,resultSequences,resultCodes,resultContexts;
        public final Fm fm;
        public final boolean channelRoutes,cpuReady,inputsReady,subPatchRequired,subPatchReady;
        public final int editorRequested,editorApplied;
        private final int[] midiChannels=new int[CLIENTS];
        private final Caps caps;
        public State(String json,Caps caps) {
            this.json=json;
            require(caps!=null,"capabilities required");this.caps=caps;
            Map<String,Object> raw=Json.object(json);version(raw,true);context=nonzero(raw.get("context_epoch"));
            Map<String,Object> mode=Json.map(raw.get("mode"));
            requested=bounded(mode.get("requested"),FmContract.FM_ENGINE_FM,"unknown requested engine");
            applied=bounded(mode.get("applied"),FmContract.FM_ENGINE_FM,"unknown applied engine");
            switching=Json.bool(mode.get("switching"));recovering=Json.bool(mode.get("recovering"));
            channelRoutes=caps.channelRoutes;
            Map<String,Object> cpu=channelRoutes?Json.map(raw.get("cpu_mode")):null;
            if(channelRoutes){
                require(u32(cpu.get("version"))==1&&Json.bool(cpu.get("route_only"))&&Json.bool(cpu.get("channel_routes")),"CPU route capability/state mismatch");
                boolean valid=Json.bool(cpu.get("valid")),ready=Json.bool(cpu.get("ready")),fault=Json.bool(cpu.get("fault"));
                Json.bool(cpu.get("busy"));Json.bool(cpu.get("pending"));u32(cpu.get("sequence"));
                editorRequested=(int)bounded(cpu.get("desired"),2,"invalid CPU editor request");
                editorApplied=(int)bounded(cpu.get("applied"),3,"invalid CPU editor observation");
                cpuReady=valid&&ready&&!fault&&editorApplied<=2;
            }else{cpuReady=false;editorRequested=(int)requested;editorApplied=(int)applied;}
            List<Object> clients=Json.list(raw.get("clients"));require(clients.size()==CLIENTS,"owner client count");
            tokens=new long[CLIENTS];processed=new long[CLIENTS];parameters=new long[CLIENTS];stops=new long[CLIENTS];
            keys=new long[CLIENTS];resultSequences=new long[CLIENTS];resultCodes=new long[CLIENTS];resultContexts=new long[CLIENTS];
            for(int i=0;i<CLIENTS;i++){
                Map<String,Object> client=Json.map(clients.get(i));tokens[i]=u32(client.get("token"));u32(client.get("received"));
                processed[i]=u32(client.get("processed"));parameters[i]=u32(client.get("parameter_processed"));
                stops[i]=u32(client.get("stop_processed"));keys[i]=bounded(client.get("keys"),Protocol.MASK_MAX,"invalid owner key mask");
                resultSequences[i]=u32(client.get("result_sequence"));resultCodes[i]=resultCode(client.get("result_code"));
                resultContexts[i]=u32(client.get("result_context_epoch"));
                midiChannels[i]=channelRoutes?(int)bounded(client.get("midi_channel"),15,"invalid MIDI channel"):-1;
            }
            legacy=new DeviceState(Json.stringify(Json.map(raw.get("legacy"))));
            Map<String,Object> sub=raw.get("sub_patch")==null?null:Json.map(raw.get("sub_patch"));
            if(sub!=null)require(u32(sub.get("version"))==1,"unknown SUB patch state version");
            boolean required=sub!=null&&Json.bool(sub.get("required"));
            boolean supported=sub!=null&&Json.bool(sub.get("supported")),valid=sub!=null&&Json.bool(sub.get("valid"));
            require(!caps.subPatchRequired||required,"missing required SUB patch observation");
            subPatchRequired=caps.subPatchRequired||required;subPatchReady=supported&&valid&&(!caps.subPatchRequired||caps.subPatchSupported);
            if(channelRoutes){
                Map<String,Object> inputs=Json.map(legacy.raw.get("inputs"));require(u32(inputs.get("version"))==1,"unknown input readiness version");
                String[] fields={"available","initialized","enabled","capabilities_known","ready","stop_pending"};boolean[] bits=new boolean[fields.length];
                for(int i=0;i<fields.length;i++)bits[i]=Json.bool(inputs.get(fields[i]));
                for(String field:new String[]{"fault","stop_requested","stop_completed","stop_fault"})u32(inputs.get(field));
                inputsReady=bits[0]&&bits[1]&&bits[2]&&bits[3]&&bits[4]&&!bits[5];
            }else inputsReady=true;
            fm=new Fm(raw.get("fm")==null?null:Json.map(raw.get("fm")),caps);
        }
        private boolean stable(){return requested==applied&&!switching&&!recovering;}
        public boolean playable() {
            if(!stable()||!caps.engineAvailable((int)applied))return false;
            if(applied==FmContract.FM_ENGINE_FM){
                return fm.ready()&&(fm.flags&FmContract.FM_FLAG_SELECTED)!=0
                        &&(fm.flags&(FmContract.FM_FLAG_MUTED|FmContract.FM_FLAG_BUSY|FmContract.FM_FLAG_FAULT))==0
                        &&fm.session!=0&&fm.epoch!=0&&fm.topology!=0
                        &&fm.hasFeature(FmContract.FM_FEATURE_NOTE_ON|FmContract.FM_FEATURE_NOTE_OFF);
            }
            return legacy.playable()&&legacy.requested==requested&&legacy.applied==applied;
        }
        public boolean canSelectFm() {
            return stable()&&caps.engineAvailable((int)FmContract.FM_ENGINE_FM)&&fm.ready()
                    &&(fm.flags&(FmContract.FM_FLAG_BUSY|FmContract.FM_FLAG_FAULT))==0;
        }
        public int midiChannel(int client){return channelRoutes&&client>=0&&client<CLIENTS?midiChannels[client]:-1;}
        public int performanceEngine(int client){return channelRoutes?midiChannel(client):(int)applied;}
        public boolean playableFor(int client){return channelRoutes?cpuReady&&legacy.ready&&inputsReady&&caps.performanceAvailable(performanceEngine(client)):playable();}
        public boolean canSelectPerformance(int engine){return channelRoutes?cpuReady&&legacy.ready&&caps.performanceAvailable(engine):stable()&&caps.engineAvailable(engine)&&(engine!=2||canSelectFm());}
        /** Frozen SUB_PATCH1 guard; unaffected controls remain available. */
        public boolean ccAvailable(int cc){
            switch(cc){case 7:case 71:case 72:case 73:case 74:case 75:case 76:case 77:case 79:
                case 87:case 102:case 103:case 104:case 105:case 106:case 107:case 109:case 110:case 111:case 112:case 113:
                    return !subPatchRequired||subPatchReady;
                default:return true;}
        }
    }

    public static final class Fm {
        public final FmContract.Profile profile;
        public final boolean available,valid;
        public final long phase,backendId,profileId,profileVersion,session,epoch,topology,flags,features;
        public final int voiceCount,patchCount;
        public final int[] operators;
        public final long[] voiceGenerations;
        public final List<Map<String,Object>> parameterValues;
        public final String reason;
        private Fm(Map<String,Object> raw,Caps caps) {
            if(raw==null){
                available=false;valid=false;phase=FmContract.FM_PHASE_UNAVAILABLE;
                backendId=0;profileId=0;profileVersion=0;session=0;epoch=0;topology=0;flags=0;features=0;
                voiceCount=0;patchCount=0;operators=new int[0];voiceGenerations=new long[0];
                profile=null;parameterValues=Collections.emptyList();reason="capability_missing";return;
            }
            available=optionalBool(raw.get("available"));valid=optionalBool(raw.get("valid"));
            require(!valid||available,"valid FM without available capability");
            phase=bounded(optionalU32(raw.get("phase")),FmContract.FM_PHASE_BLOCKED,"unknown FM phase");
            backendId=optionalU32(raw.get("backend_id"));profileId=optionalU32(raw.get("profile_id"));
            profileVersion=optionalU32(raw.get("profile_version"));session=optionalU32(raw.get("session_id"));
            epoch=optionalU32(raw.get("engine_epoch"));topology=optionalU32(raw.get("topology_generation"));
            voiceCount=(int)bounded(optionalU32(raw.get("voice_count")),FmContract.FM_MAX_DESCRIPTOR_VOICES,"FM voice budget");
            patchCount=(int)bounded(optionalU32(raw.get("patch_operator_count")),FmContract.FM_MAX_OPERATORS_PER_VOICE,"FM patch operator budget");
            flags=optionalU32(raw.get("flags"));features=optionalU32(raw.get("features"));
            long allowedFlags=FmContract.FM_FLAG_READY|FmContract.FM_FLAG_SELECTED|FmContract.FM_FLAG_MUTED
                    |FmContract.FM_FLAG_BUSY|FmContract.FM_FLAG_FAULT;
            long allowedFeatures=FmContract.FM_FEATURE_NOTE_ON|FmContract.FM_FEATURE_NOTE_OFF|FmContract.FM_FEATURE_SET_PITCH
                    |FmContract.FM_FEATURE_SET_GAIN_PAN|FmContract.FM_FEATURE_SET_PARAMETER|FmContract.FM_FEATURE_APPLY_PRESET
                    |FmContract.FM_FEATURE_RELEASE_ALL|FmContract.FM_FEATURE_PRIORITY_PANIC|FmContract.FM_FEATURE_NEXT_NOTE_PARAMETERS
                    |FmContract.FM_FEATURE_NATIVE_STEREO_PAN;
            require((flags&~allowedFlags)==0,"reserved FM flags");require((features&~allowedFeatures)==0,"reserved FM features");
            long[] counts=array(raw.get("operators_per_voice"),voiceCount);
            operators=new int[voiceCount];for(int i=0;i<counts.length;i++)
                operators[i]=(int)bounded(counts[i],FmContract.FM_MAX_OPERATORS_PER_VOICE,"FM operator budget");
            voiceGenerations=array(raw.get("voice_generations"),voiceCount);
            FmContract.Profile matched=null;
            if(caps.contractMatches){
                for(FmContract.Profile candidate:caps.profiles){
                    if(candidate.backend_id==backendId&&candidate.profile_version==profileVersion
                            &&candidate.profile_sha256.equals(raw.get("profile_sha256"))){
                        require(matched==null,"ambiguous runtime profile tuple");matched=candidate;
                    }
                }
            }
            if(matched!=null&&matched.id!=profileId)matched=null;
            profile=matched;
            if(available&&valid){
                require(voiceCount>0,"valid FM has no voices");
                require(raw.get("flags")!=null&&raw.get("features")!=null,"valid FM status fields missing");
                if(profile!=null)layout(profile,voiceCount,operators,patchCount);
            }
            rate(raw.get("source_rate"),available&&valid);rate(raw.get("output_rate"),available&&valid);
            identity(raw.get("accepted"),false);identity(raw.get("completed"),false);identity(raw.get("result"),true);
            if(raw.get("panic")!=null){
                Map<String,Object> panic=Json.map(raw.get("panic"));
                identity(panic.get("accepted"),false);identity(panic.get("completed"),false);identity(panic.get("result"),true);
            }
            String why=raw.get("reason")==null?"":string(raw.get("reason"));
            if(!caps.contractMatches)why=caps.incompatibility;
            else if(profile==null&&available&&valid)why="FM backend/profile identity mismatch";
            else if(!available||!valid){if(why.isEmpty())why="capability_missing";}
            reason=why;
            List<Map<String,Object>> confirmed=new ArrayList<>();Set<String> seen=new HashSet<>();
            Object values=raw.get("parameter_values");
            require(!(available&&valid&&values==null),"valid FM parameter cache missing");
            if(values!=null){
                List<Object> rows=Json.list(values);require(rows.size()<=FmContract.FM_MAX_PARAMETERS,"FM parameter cache budget");
                for(Object row:rows){
                    Map<String,Object> entry=Json.map(row);long id=u32(entry.get("parameter_id")),target=u32(entry.get("target_word"));
                    long value=u32(entry.get("value_u32"));nonzero(entry.get("sequence_id"));
                    require(seen.add(id+"/"+target),"duplicate confirmed parameter target");
                    if(profile!=null)require(parameterValid(profile,voiceCount,operators,patchCount,id,target,value),"invalid confirmed FM parameter");
                    confirmed.add(Collections.unmodifiableMap(new LinkedHashMap<>(entry)));
                }
            }
            parameterValues=Collections.unmodifiableList(confirmed);
        }
        private boolean ready(){
            return profile!=null&&available&&valid&&phase==FmContract.FM_PHASE_READY&&(flags&FmContract.FM_FLAG_READY)!=0;
        }
        public boolean hasFeature(long feature){
            return feature>0&&feature<=Protocol.UINT_MAX&&(features&feature)==feature;
        }
    }

    public static final class Catalog {
        public final FmContract.Profile profile;
        public final List<FmContract.Parameter> parameters;
        public final List<FmContract.Preset> presets;
        private Catalog(FmContract.Profile profile,List<FmContract.Parameter> parameters,List<FmContract.Preset> presets){
            this.profile=profile;this.parameters=Collections.unmodifiableList(new ArrayList<>(parameters));
            this.presets=Collections.unmodifiableList(new ArrayList<>(presets));
        }
        public static Catalog load(Transport transport,Caps caps,State state) throws IOException {
            try{
                require(transport!=null&&caps!=null&&state!=null,"catalog inputs required");
                FmContract.Profile profile=state.fm.profile;
                require(caps.contractMatches&&profile!=null,"matched runtime profile required");
                boolean indexed=false;for(FmContract.Profile p:caps.profiles)if(p.id==profile.id)indexed=true;
                require(indexed,"runtime profile is not in capabilities");
                String path="/api/v2/fm/catalog?profile="+profile.id+"&section=profile";
                Map<String,Object> header=get(transport,path);catalogIdentity(header,profile);
                require(profile.key.equals(string(header.get("key"))),"profile key drift");
                int voices=(int)bounded(header.get("voice_count"),FmContract.FM_MAX_DESCRIPTOR_VOICES,"catalog voice budget");
                int patch=(int)bounded(header.get("patch_operator_count"),FmContract.FM_MAX_OPERATORS_PER_VOICE,"catalog patch budget");
                long[] values=array(header.get("operators_per_voice"),voices);int[] operators=new int[voices];
                for(int i=0;i<voices;i++)operators[i]=(int)bounded(values[i],FmContract.FM_MAX_OPERATORS_PER_VOICE,"catalog operator budget");
                layout(profile,voices,operators,patch);
                List<Object> labels=Json.list(header.get("operator_labels"));int labelCount=0;
                while(profile.operatorLabel(labelCount)!=null){require(labelCount<FmContract.FM_MAX_OPERATORS_PER_VOICE,"label budget");labelCount++;}
                require(labels.size()==labelCount,"operator label count");
                for(int i=0;i<labels.size();i++)require(profile.operatorLabel(i).equals(string(labels.get(i))),"operator label drift");
                require(bounded(header.get("parameter_count"),FmContract.FM_MAX_PARAMETERS,"parameter budget")==profile.parameter_count,"parameter count drift");
                require(bounded(header.get("preset_count"),FmContract.FM_MAX_PRESETS,"preset budget")==profile.preset_count,"preset count drift");
                List<FmContract.Parameter> parameters=new ArrayList<>();Set<Long> seen=new HashSet<>();
                for(Map<String,Object> entry:pages(transport,profile,"parameters",profile.parameter_count,FmContract.FM_MAX_PARAMETERS)){
                    long id=u32(entry.get("id"));require(seen.add(id),"duplicate parameter id");
                    FmContract.Parameter p=FmContract.getParameter(profile.id,id);require(p!=null,"unknown parameter id");
                    require(p.name.equals(string(entry.get("name")))&&p.minimum==Json.number(entry.get("minimum"))
                            &&p.maximum==Json.number(entry.get("maximum"))&&p.scope_id==u32(entry.get("scope_id"))
                            &&p.unit_id==u32(entry.get("unit_id"))&&p.apply_time_id==u32(entry.get("apply_time_id"))
                            &&p.value_type_id==u32(entry.get("value_type_id")),"parameter metadata drift");
                    parameters.add(p);
                }
                List<FmContract.Preset> presets=new ArrayList<>();seen.clear();
                for(Map<String,Object> entry:pages(transport,profile,"presets",profile.preset_count,FmContract.FM_MAX_PRESETS)){
                    long id=u32(entry.get("id"));require(seen.add(id),"duplicate preset id");
                    FmContract.Preset p=FmContract.getPreset(profile.id,id);require(p!=null,"unknown preset id");
                    require(p.name.equals(string(entry.get("name")))&&p.key_on_logical_mask==u32(entry.get("key_on_logical_mask"))
                            &&p.values_count==u32(entry.get("values_count")),"preset metadata drift");presets.add(p);
                }
                return new Catalog(profile,parameters,presets);
            }catch(IllegalArgumentException malformed){
                throw new IOException("invalid FM catalog: "+malformed.getMessage(),malformed);
            }
        }
        private static Map<String,Object> get(Transport transport,String path) throws IOException{
            Transport.Response response=transport.request(path,null);
            if(response==null||response.code!=200)throw new IOException("FM catalog HTTP "+(response==null?"no response":response.code));
            return Json.object(response.body);
        }
        private static void catalogIdentity(Map<String,Object> raw,FmContract.Profile profile){
            version(raw,false);
            require(FmContract.FM_PROFILES_SHA256.equals(raw.get("profiles_sha256"))&&u32(raw.get("profile_id"))==profile.id
                    &&u32(raw.get("backend_id"))==profile.backend_id&&u32(raw.get("profile_version"))==profile.profile_version,"catalog identity drift");
            Json.bool(raw.get("runtime_available"));
        }
        private static List<Map<String,Object>> pages(Transport transport,FmContract.Profile profile,String section,
                                                      long total,long maximum) throws IOException{
            require(total<=maximum,"catalog resource budget");List<Map<String,Object>> result=new ArrayList<>();long offset=0;
            do{
                String path="/api/v2/fm/catalog?profile="+profile.id+"&section="+section+"&offset="+offset+"&limit="+CATALOG_PAGE_MAX;
                Map<String,Object> raw=get(transport,path);catalogIdentity(raw,profile);
                require(section.equals(string(raw.get("section"))),"catalog section drift");
                require(u32(raw.get("offset"))==offset&&bounded(raw.get("total"),maximum,"catalog total budget")==total,"catalog page bounds drift");
                List<Object> items=Json.list(raw.get("items"));
                require(items.size()==Math.min(CATALOG_PAGE_MAX,total-offset),"catalog page item count");
                for(Object entry:items)result.add(Json.map(entry));
                offset+=items.size();require(raw.containsKey("next_offset"),"next_offset missing");
                if(offset==total)require(raw.get("next_offset")==null,"catalog final next_offset");
                else require(nonzero(raw.get("next_offset"))==offset,"catalog next_offset drift");
            }while(offset<total);
            require(result.size()==total,"catalog incomplete");return result;
        }
    }

    public static Catalog load(Transport transport,Caps caps,State state) throws IOException {
        return Catalog.load(transport,caps,state);
    }

    public static String encode(long token,long sequence,long context,int op,long a,long b,long c,
                                State state,Catalog catalog) {
        return encode(token,sequence,context,op,a,b,c,state,catalog,-1);
    }
    public static String encode(long token,long sequence,long context,int op,long a,long b,long c,
                                State state,Catalog catalog,int client) {
        require(token>0&&token<=Protocol.UINT_MAX&&sequence>0&&sequence<=Protocol.UINT_MAX
                &&context>0&&context<=Protocol.UINT_MAX,"token/sequence/context must be nonzero uint32");
        require(a>=0&&a<=Protocol.UINT_MAX&&b>=0&&b<=Protocol.UINT_MAX&&c>=0&&c<=Protocol.UINT_MAX,"command values must be uint32");
        require(state!=null&&context==state.context,"current HTTP context required");
        switch(op){
            case (int)FmContract.FM_HTTP_OPCODE_CC:
                require(c==0&&state.playable()&&state.ccAvailable((int)a),"CC unavailable");
                Protocol.encode(token,sequence,op,a,b);break;
            case (int)FmContract.FM_HTTP_OPCODE_KEYS_UPDATE:
                require(c==0,"reserved KEYS field");Protocol.encode(token,sequence,op,a,b);
                require(a==0||state.playableFor(client),"engine not ready for nonzero keys");break;
            case (int)FmContract.FM_HTTP_OPCODE_STOP:
                require(c==0,"reserved STOP field");Protocol.encode(token,sequence,op,a,b);break;
            case (int)FmContract.FM_HTTP_OPCODE_ENGINE_SELECT:
                require(c==0&&(state.channelRoutes?state.cpuReady:state.stable()),"engine selection during transition");
                if(state.channelRoutes){require(a<=2&&b==0&&state.canSelectPerformance((int)a),"CPU performance route unavailable");}
                else if(a==FmContract.FM_ENGINE_FM){
                    require(state.canSelectFm()&&b!=0&&b==state.fm.profile.backend_id,"FM backend unavailable");
                }else{
                    Protocol.encode(token,sequence,op,a,b);
                    require(state.caps.engineAvailable((int)a),"engine unavailable");
                }
                break;
            case (int)FmContract.FM_HTTP_OPCODE_KS_DECAY:
                require(c==0&&state.caps.engineAvailable((int)FmContract.FM_ENGINE_KS),"KS unavailable");
                Protocol.encode(token,sequence,op,a,b);break;
            case (int)FmContract.FM_HTTP_OPCODE_FM_PARAMETER:
                requireFmCatalog(state,catalog);
                require(state.fm.hasFeature(FmContract.FM_FEATURE_SET_PARAMETER),"FM parameters unsupported");
                require(parameterValid(catalog.profile,state.fm.voiceCount,state.fm.operators,state.fm.patchCount,a,b,c),"invalid FM parameter");
                break;
            case (int)FmContract.FM_HTTP_OPCODE_FM_PRESET:
                requireFmCatalog(state,catalog);
                require(state.fm.hasFeature(FmContract.FM_FEATURE_APPLY_PRESET),"FM presets unsupported");
                require(b==0&&c==0&&FmContract.getPreset(catalog.profile.id,a)!=null,"invalid FM preset");break;
            default:throw new IllegalArgumentException("unsupported HTTP opcode");
        }
        String body=token+" "+sequence+" "+context+" "+op+" "+a+" "+b+" "+c;
        require(body.length()<=FmContract.FM_HTTP_COMMAND_MAX_BYTES,"HTTP command length");return body;
    }

    private static void require(boolean condition,String message){
        if(!condition)throw new IllegalArgumentException(message);
    }
    private static void requireFmCatalog(State state,Catalog catalog){
        require(state.applied==FmContract.FM_ENGINE_FM&&state.playable()&&catalog!=null,"FM not playable or catalog missing");
        require(catalog.profile==state.fm.profile,"FM catalog/profile mismatch");
    }
    private static long nonzero(Object value){long n=u32(value);require(n!=0,"expected nonzero uint32");return n;}
    private static String string(Object value){
        if(!(value instanceof String))throw new IllegalArgumentException("expected string");return (String)value;
    }
    private static void version(Map<String,Object> raw,boolean minorRequired){
        require(u32(raw.get("api_major"))==FmContract.FM_HTTP_API_MAJOR,"unsupported HTTP API major");
        if(minorRequired||raw.containsKey("api_minor"))
            require(u32(raw.get("api_minor"))==FmContract.FM_HTTP_API_MINOR,"unsupported HTTP API minor");
    }
    private static long bounded(Object value,long maximum,String message){
        long number=u32(value);require(number<=maximum,message);return number;
    }
    private static long optionalU32(Object value){return value==null?0:u32(value);}
    private static boolean optionalBool(Object value){return value!=null&&Json.bool(value);}
    private static long resultCode(Object value){return bounded(value,FmContract.FM_RESULT_STALE_VOICE,"unknown owner result");}
    private static long[] array(Object value,int count){
        if(value==null){require(count==0,"missing FM voice array");return new long[0];}
        List<Object> values=Json.list(value);require(values.size()==count,"FM voice array length");
        long[] result=new long[count];for(int i=0;i<count;i++)result[i]=u32(values.get(i));return result;
    }
    private static void layout(FmContract.Profile profile,int count,int[] operators,int patchCount){
        require(profile.voice_count==count&&profile.patch_operator_count==patchCount,"FM profile dimensions mismatch");
        for(int i=0;i<count;i++)require(FmContract.profileOperatorCount(profile.id,i)==operators[i],"FM profile operator layout mismatch");
    }
    private static void rate(Object value,boolean required){
        if(value==null){require(!required,"FM rate missing");return;}
        Map<String,Object> rate=Json.map(value);u32(rate.get("numerator"));nonzero(rate.get("denominator"));
    }
    private static void identity(Object value,boolean result){
        if(value==null)return;Map<String,Object> identity=Json.map(value);
        u32(identity.get("session_id"));u32(identity.get("engine_epoch"));u32(identity.get("sequence_id"));
        if(result)resultCode(identity.get("code"));
    }
    private static boolean parameterValid(FmContract.Profile profile,int voiceCount,int[] operators,int patchCount,
                                          long id,long target,long value){
        FmContract.Parameter parameter=FmContract.getParameter(profile.id,id);if(parameter==null)return false;
        long voice=FmContract.targetVoice(target);long selected=voice<voiceCount?operators[(int)voice]:0;
        return FmContract.validateParameter(profile.id,id,parameter.scope_id,target,value)
                &&FmContract.validateTarget(parameter.scope_id,target,voiceCount,selected,patchCount);
    }
}
