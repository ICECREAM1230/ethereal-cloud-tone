package cn.fpgasynth.control.core;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
/** Deterministic test-only owner model. No SPI, device discovery, or audio. */
public final class RuntimeModel implements Transport {
    public final Map<String,Object> state;
    public final List<long[]> sent=new ArrayList<>();
    public final List<String> paths=new ArrayList<>();
    public final Path fixtures;
    public boolean holdParameters, holdAll, dropOn, rejectParameter;
    public int capsCode=200, sessions;
    public long token=100;
    private final List<long[]> queued=new ArrayList<>();
    public RuntimeModel(Path fixtures)throws IOException {this.fixtures=fixtures;state=Json.object(read("v2-live-state.json"));}
    public String read(String file)throws IOException{return new String(Files.readAllBytes(fixtures.resolve(file)),StandardCharsets.UTF_8);}
    public long context(){return Json.number(state.get("context_epoch"));}
    public Map<String,Object> fm(){return Json.map(state.get("fm"));}
    public Map<String,Object> client(){return Json.map(Json.list(state.get("clients")).get(0));}
    public void externalContext(){state.put("context_epoch",context()+1);client().put("keys",0L);queued.clear();}
    public synchronized Response request(String path,String body)throws IOException{
        paths.add(path);
        if(path.equals("/api/v2/capabilities"))return new Response(capsCode,read("v2-live-capabilities.json"));
        if(path.startsWith("/api/v2/fm/catalog?"))return new Response(200,read(path.contains("section=profile")?"v2-profile.json":path.contains("section=presets")?"v2-presets.json":path.contains("offset=8")?"v2-catalog-page-8.json":"v2-catalog.json"));
        if(path.equals("/api/v2/session")){
            if(!"".equals(body))throw new AssertionError("empty session POST");
            sessions++;token++;Map<String,Object> c=client();for(String k:c.keySet())c.put(k,0L);c.put("token",token);
            return new Response(201,"{\"api_major\":2,\"api_minor\":0,\"token\":"+token+",\"client\":0,\"lease_ms\":1200,\"context_epoch\":"+context()+"}");
        }
        if(path.equals("/api/v2/command")){
            String[] pieces=body.split(" ",-1);if(pieces.length!=7)throw new AssertionError("seven integers");long[] w=new long[7];for(int i=0;i<7;i++)w[i]=Long.parseLong(pieces[i]);
            sent.add(w);
            if(w[0]!=token)return new Response(410,"{\"api_major\":2,\"code\":\"unknown_session\"}");
            if(w[2]!=context())return new Response(409,"{\"api_major\":2,\"code\":\"stale_context\"}");
            client().put("received",w[1]);queued.add(w);
            if(dropOn&&w[3]==3&&w[4]!=0)throw new IOException("ambiguous transport timeout");
            return new Response(202,"{\"api_major\":2,\"accepted\":"+w[1]+",\"stage\":\"received\",\"context_epoch\":"+context()+",\"fpga\":\"unverified\"}");
        }
        if(path.equals("/api/v2/state")){
            if(!holdAll){Iterator<long[]> it=queued.iterator();while(it.hasNext()){
                long[] w=it.next();if(holdParameters&&w[3]!=3)continue;it.remove();Map<String,Object> c=client();
                c.put("result_sequence",w[1]);c.put("result_context_epoch",w[2]);c.put("result_code",rejectParameter&&w[3]==7?5L:0L);
                if(rejectParameter&&w[3]==7)continue;
                if(w[3]==3){c.put("processed",w[1]);c.put("keys",w[4]);}
                else if(w[3]==4)c.put("stop_processed",w[1]);else c.put("parameter_processed",w[1]);
                if(w[3]==5){Map<String,Object> mode=Json.map(state.get("mode"));mode.put("requested",w[4]);mode.put("applied",w[4]);Map<String,Object> legacy=Json.map(state.get("legacy"));legacy.put("ready",true);Map<String,Object> engine=Json.map(legacy.get("engine"));engine.put("requested",Math.min(1L,w[4]));engine.put("applied",Math.min(1L,w[4]));}
                if(w[3]==4||w[3]==5||w[3]==8){state.put("context_epoch",context()+1);c.put("keys",0L);fm().put("engine_epoch",Json.number(fm().get("engine_epoch"))+1);}
            }}
            return new Response(200,Json.stringify(state));
        }
        return new Response(404,"{}");
    }
    public int count(int op,long a){int n=0;for(long[] w:sent)if(w[3]==op&&w[4]==a)n++;return n;}
}
