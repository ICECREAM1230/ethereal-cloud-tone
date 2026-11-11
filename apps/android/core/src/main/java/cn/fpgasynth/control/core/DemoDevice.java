package cn.fpgasynth.control.core;

import java.io.IOException;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.Map;
import java.util.function.LongSupplier;

/** Explicit in-app protocol simulator. Generates no sound and contacts no hardware. */
public class DemoDevice implements Transport {
    public static final String INITIAL="{\"ready\":true,\"revision\":1,\"panic\":0,\"rendering\":false,\"test_tone\":false,\"fpga\":\"unverified\","
            +"\"tokens\":[0,0,0,0],\"processed\":[0,0,0,0],\"parameter_processed\":[0,0,0,0],\"stop_processed\":[0,0,0,0],\"keys\":[0,0,0,0],"
            +"\"engine\":{\"available\":true,\"valid\":true,\"requested\":0,\"applied\":0,\"switching\":false,\"busy\":false,\"recovering\":false,\"fault\":0,\"recoveries\":0,\"timeouts\":0,\"active\":0,\"init\":0,\"release\":0,\"accepted\":0,\"mode_sequence\":0,\"rejects\":0,\"frames\":0,\"deadlines\":0,\"started\":[0,0,0,0]},"
            +"\"ks_decay_ms\":3000,\"osc\":[[0,0,0,0],[3,-12,0,0]],\"voice\":1,\"mix\":0,\"detune\":6,\"spread\":127,\"volume\":207,\"pan\":0,"
            +"\"filter\":[0,512,0,1,64],\"cutoff_offset\":0,\"resonance_offset\":0,\"amp\":[152,32,240,40],\"mod\":[152,32,240,40],\"mod_depth\":2048,\"lfo\":[[2,350,16,0],[2,350,0,0]],\"velocity\":[64,64]}";
    protected final Map<String,Object> state=Json.object(INITIAL);
    private final long[] token=new long[4],seq=new long[4],last=new long[4],lease=new long[4];
    private final boolean[] armed={true,true,true,true};
    private long next=1000;
    private final LongSupplier clock;
    private final Deque<long[]> events=new ArrayDeque<>();
    public DemoDevice(LongSupplier clock){this.clock=clock;}
    private void set(String k,int i,long v){Json.list(state.get(k)).set(i,v);}
    private long get(String k,int i){return Json.number(Json.list(state.get(k)).get(i));}
    private void expire(long now){for(int i=0;i<4;i++){
        if(token[i]!=0&&now-last[i]>=10000)token[i]=0;
        if(get("keys",i)!=0&&now>=lease[i]){set("keys",i,0);armed[i]=false;}
    }}
    public synchronized Map<String,Object> mutableState(){return state;}
    public synchronized void adopt(){
        long now=clock.getAsLong();expire(now);
        while(!events.isEmpty()){
            long[] e=events.removeFirst();int i=(int)e[0],op=(int)e[3];long id=e[2],a=e[4],b=e[5];
            if(token[i]!=e[1])continue;
            set("processed",i,id);
            if(op==3){
                if(a==0)armed[i]=true;
                if(armed[i]&&now<e[6]){set("keys",i,a);lease[i]=e[6];}
            } else if(op==4||op==5){
                for(int c=0;c<4;c++){set("keys",c,0);armed[c]=false;}
                state.put("panic",Json.number(state.get("panic"))+1);state.put("test_tone",false);
                if(op==4)set("stop_processed",i,id);
                else {Map<String,Object> en=Json.map(state.get("engine"));
                    if(a==1&&!Json.bool(en.get("available")))continue;
                    en.put("requested",a);en.put("applied",a);set("parameter_processed",i,id);}
            } else if(op==6){state.put("ks_decay_ms",a);set("parameter_processed",i,id);}
            else if(op==2){applyCC((int)a,(int)b);set("parameter_processed",i,id);}
            state.put("revision",Json.number(state.get("revision"))+1);
        }
    }
    @Override public synchronized Response request(String path,String body) {
        long now=clock.getAsLong();expire(now);
        if(path.equals("/api/state")){adopt();return new Response(200,Json.stringify(state));}
        if(path.equals("/api/session")){
            if(!"".equals(body))return new Response(400,"{}");
            for(int i=0;i<4;i++)if(token[i]==0){token[i]=++next;seq[i]=0;last[i]=now;set("tokens",i,token[i]);
                set("processed",i,0);set("parameter_processed",i,0);set("stop_processed",i,0);set("keys",i,0);armed[i]=true;
                return new Response(201,"{\"token\":"+token[i]+",\"client\":"+i+",\"lease_ms\":1200}");}
            return new Response(503,"{}");
        }
        if(!path.equals("/api/command"))return new Response(404,"{}");
        try{
            if(body==null||!body.matches("[0-9]+ [0-9]+ [0-9]+ [0-9]+ [0-9]+")||body.length()>95)return new Response(400,"{}");
            String[] p=body.split(" ");long[] e=new long[7];
            for(int k=0;k<5;k++){e[k+1]=Long.parseLong(p[k]);if(e[k+1]>Protocol.UINT_MAX)return new Response(400,"{}");}
            Protocol.encode(e[1],e[2],(int)e[3],e[4],e[5]);
            int c=-1;for(int i=0;i<4;i++)if(token[i]==e[1])c=i;
            if(c<0)return new Response(410,"{}");if(e[2]<=seq[c])return new Response(409,"{}");
            seq[c]=e[2];last[c]=now;e[0]=c;e[6]=now+1200;events.add(e);
            return new Response(202,"{\"accepted\":"+e[2]+",\"stage\":\"received\",\"fpga\":\"unverified\"}");
        }catch(Exception e){return new Response(400,"{}");}
    }
    private void applyCC(int cc,int v){
        int engine=((Long)Json.map(state.get("engine")).get("requested")).intValue();
        if(engine==1&&cc!=7&&cc!=10)return;
        if(!LocalPatchState.applyCC(state,cc,v))throw new IllegalArgumentException("unsupported CC");
    }
}
