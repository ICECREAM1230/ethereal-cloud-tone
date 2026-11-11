package cn.fpgasynth.control.core;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public final class SessionTest {
    private static int passed;
    private static void check(boolean value,String message){if(!value)throw new AssertionError(message);}
    private static void rejects(Runnable fn){try{fn.run();}catch(IllegalArgumentException e){return;}throw new AssertionError("expected rejection");}
    private static void test(String name,Runnable fn){fn.run();passed++;System.out.println("PASS "+name);}
    private static final class Rig {
        long now;
        final DemoDevice device=new DemoDevice(()->now);
        final List<String> posts=new ArrayList<>();
        final List<Integer> adoptedMasks=new ArrayList<>();
        final List<ControlSession.View> updates=new ArrayList<>();
        final ControlSession client=new ControlSession(updates::add);
        boolean hold,ambiguous,offline;
        int error;
        final Transport transport=(path,body)->{
            if(offline)throw new IOException("network down");
            if(path.equals("/api/command")){
                posts.add(body);
                if(error!=0){int code=error;error=0;return new Transport.Response(code,"{}");}
                Transport.Response r=device.request(path,body);
                if(ambiguous){ambiguous=false;throw new IOException("timeout after acceptance");}
                return r;
            }
            if(path.equals("/api/state")){
                Transport.Response r=hold?new Transport.Response(200,Json.stringify(device.mutableState())):device.request(path,body);
                if(!hold && client.view().client>=0)adoptedMasks.add((int)Json.number(Json.list(device.mutableState().get("keys")).get(client.view().client)));
                return r;
            }
            return device.request(path,body);
        };
        Rig(){client.connect(transport,true);ready();posts.clear();adoptedMasks.clear();}
        void step(){client.tick(now);now+=25;}
        void steps(int n){for(int i=0;i<n;i++)step();}
        void ready(){for(int i=0;i<300&&!client.view().playable;i++)step();check(client.view().playable,"not ready: "+client.view().message);}
        void key(int mask){check(client.keys(mask,100,now),"keys rejected");}
        long[] numbers(String text){String[] v=text.split(" ");long[] a=new long[v.length];for(int i=0;i<v.length;i++)a[i]=Long.parseLong(v[i]);return a;}
        int nonzero(){int n=0;for(String p:posts){long[] a=numbers(p);if(a[2]==3&&a[3]!=0)n++;}return n;}
        Map<String,Object> engine(){return Json.map(device.mutableState().get("engine"));}
    }
    public static void main(String[] args){
        test("strict five uint32 encoding and whitelist",()->{
            check(Protocol.encode(Protocol.UINT_MAX,Protocol.UINT_MAX,3,Protocol.MASK_MAX,127).equals("4294967295 4294967295 3 33554431 127"),"exact body");
            rejects(()->Protocol.encode(0,1,3,0,100));rejects(()->Protocol.encode(1,0,3,0,100));
            rejects(()->Protocol.encode(Protocol.UINT_MAX+1,1,3,0,100));rejects(()->Protocol.encode(1,1,3,-1,100));
            rejects(()->Protocol.encode(1,1,3,0,0));rejects(()->Protocol.encode(1,1,2,64,127));
            rejects(()->Protocol.encode(1,1,2,119,64));rejects(()->Protocol.encode(1,1,6,199,0));
            rejects(()->Protocol.encode(1,1,6,8001,0));rejects(()->Protocol.encode(1,1,4,0,1));
        });
        test("address and bounded malformed JSON",()->{
            check(Protocol.endpoint("192.168.4.1/").equals("http://192.168.4.1"),"origin normalization");
            rejects(()->Protocol.endpoint("http://user:pass@192.168.4.1"));rejects(()->Protocol.endpoint("http://host/api"));
            rejects(()->Protocol.endpoint("http://host/?q=1"));rejects(()->Protocol.endpoint("ftp://host"));
            rejects(()->Json.object("{\"x\":1,\"x\":2}"));rejects(()->Json.object("{\"x\":01}"));
            rejects(()->new DeviceState("{\"ready\":true}"));
            check(Json.number(Json.object("{\"x\":4294967295}").get("x"))==Protocol.UINT_MAX,"uint32 read");
        });
        test("connection reads existing patch without default writes",()->{
            Rig r=new Rig();check(r.client.view().state.n("volume")==207,"retains volume");
            check(r.client.view().sequence==1,"only initial empty heartbeat");
            r.steps(30);for(String p:r.posts)check(r.numbers(p)[2]==3,"no automatic CC/mode writes");
        });
        test("fast press/release waits for owner, 202 alone cannot advance",()->{
            Rig r=new Rig();r.hold=true;r.key(1);r.key(0);r.step();r.steps(6);
            check(r.posts.size()==1 && r.client.view().ownerMask==0,"must await adoption");
            r.hold=false;r.steps(15);
            check(r.adoptedMasks.contains(1),"short NoteOn adopted");
            check(r.client.view().ownerMask==0,"NoteOff adopted");
            long previous=0;for(String p:r.posts){long[] a=r.numbers(p);check(a[1]>previous,"strict shared sequence");previous=a[1];}
        });
        test("250 ms heartbeats preserve the 1.2 s lease",()->{
            Rig r=new Rig();r.key(3);r.steps(100);
            check(r.client.view().ownerMask==3,"held chord survives");long last=-1;
            for(String p:r.posts){long[] a=r.numbers(p);check(a[2]==3,"keyboard only");if(last>=0)check(a[1]>last,"new seq");last=a[1];}
            check(r.nonzero()>=9,"heartbeat renewals");r.client.pause();r.steps(2);r.now+=1300;r.device.request("/api/state",null);
            check(Json.number(Json.list(r.device.mutableState().get("keys")).get(0))==0,"background release");
        });
        test("ambiguous accepted timeout clears intent and never replays old NoteOn",()->{
            Rig r=new Rig();r.ambiguous=true;r.key(1);r.step();
            check(!r.client.view().playable&&r.client.view().localMask==0&&r.client.view().queueSize==0,"local safety");
            r.steps(3);r.client.connect(r.transport,true);r.ready();r.steps(12);
            check(r.nonzero()==1,"no automatic NoteOn replay");check(r.client.view().ownerMask==0,"new session empty");
        });
        test("network failure and lease expiry only release this source",()->{
            Rig r=new Rig();r.key(4);r.steps(3);r.offline=true;r.client.networkLost();r.steps(2);
            check(r.client.view().localMask==0&&!r.client.view().playable,"offline gate");
            r.now+=1201;r.device.request("/api/state",null);
            check(Json.number(Json.list(r.device.mutableState().get("keys")).get(0))==0,"lease fallback");
        });
        test("background/resume sends zero and requires a fresh touch",()->{
            Rig r=new Rig();r.key(1);r.key(3);r.client.pause();r.steps(3);
            check(r.nonzero()==0,"queued notes discarded");r.client.resume();r.ready();r.steps(5);
            check(r.client.view().localMask==0&&r.nonzero()==0,"resume empty");r.key(2);r.steps(3);check(r.client.view().ownerMask==2,"fresh note works");
        });
        test("stale 300 ms nonzero frame is discarded",()->{
            Rig r=new Rig();r.key(1);r.key(0);r.now+=301;r.steps(5);
            check(r.nonzero()==0&&r.client.view().ownerMask==0,"no late note");
        });
        test("long scheduling gap cannot renew an old chord",()->{
            Rig r=new Rig();r.key(1);r.steps(3);int old=r.nonzero();r.now+=1201;r.steps(5);
            check(r.nonzero()==old&&r.client.view().localMask==0,"no late heartbeat replay");
        });
        test("bounded 32 transition queue fails safely",()->{
            Rig r=new Rig();for(int i=0;i<33;i++)r.client.keys(i%2==0?1:0,100,r.now);
            r.steps(4);check(r.client.view().queueSize==0&&r.client.view().localMask==0&&r.nonzero()==0,"queue overflow release");
        });
        test("HTTP errors 400/403/409/410 stop control, 503 sessions explained",()->{
            for(int code:new int[]{400,403,409,410}){
                Rig r=new Rig();r.error=code;r.key(1);r.step();check(!r.client.view().playable&&r.client.view().message.contains(""+code),"error "+code);
            }
            ControlSession c=new ControlSession(null);c.connect((path,body)->new Transport.Response(503,"{}"),false);c.tick(0);
            check(c.view().message.contains("四个会话"),"session-full guidance");
        });
        test("owner adoption timeout cannot keep keys enabled",()->{
            Rig r=new Rig();r.hold=true;r.key(1);r.step();r.steps(38);
            check(!r.client.view().playable&&r.client.view().localMask==0,"adoption timeout safety");
        });
        test("all 38 allowed CCs mapped and can be explicitly adopted",()->{
            Rig r=new Rig();Set<Integer> found=new HashSet<>();for(PatchControls.Control c:PatchControls.all()){
                check(found.add(c.cc),"duplicate CC");check(Protocol.allowed(c.cc),"whitelist");
                check(r.client.parameter(c.cc,64),"explicit edit");r.steps(5);
                check(r.client.view().state.parameters[0]>0,"parameter adopted");c.value(r.client.view().state);c.display(r.client.view().state);
            }
            check(found.size()==38&&found.size()==Protocol.CCS.length,"complete whitelist");
        });
        test("verified physical mapping boundaries and signed depth",()->{
            Rig r=new Rig();r.client.parameter(14,0);r.steps(4);check(r.client.view().state.a("osc",0,1)==-12,"lowest coarse pitch");
            r.client.parameter(14,127);r.steps(4);check(r.client.view().state.a("osc",0,1)==12,"highest coarse pitch");
            r.client.parameter(107,32);r.steps(4);check(r.client.view().state.n("mod_depth")==-4096,"negative four octaves");
            check(PatchControls.read(107,r.client.view().state)==32,"signed inverse");
            r.client.parameter(79,127);r.steps(4);check(r.client.view().state.a("amp",2)==254,"sustain not inverted");
        });
        test("mode switch waits for owner/readback and re-arms empty",()->{
            Rig r=new Rig();r.key(1);r.steps(3);check(r.client.mode(1),"KS available");check(!r.client.view().playable,"switch gates notes");
            r.steps(2);check(!r.client.view().playable,"202 mode not sufficient");r.ready();
            check(r.client.view().state.applied==1&&r.client.view().localMask==0&&r.client.view().ownerMask==0,"mode readback and empty");
            check(!r.client.parameter(74,20),"DDS controls blocked in KS");check(r.client.parameter(7,70),"KS volume supported");r.steps(4);
            check(r.client.decay(200),"min T60");r.steps(4);check(r.client.view().state.decay==200,"min adopted");
            check(r.client.decay(8000),"max T60");r.steps(4);check(r.client.view().state.decay==8000,"max adopted");
        });
        test("missing KS capability keeps compatible DDS usable",()->{
            Rig r=new Rig();r.engine().put("available",false);r.engine().put("valid",false);r.now+=500;r.steps(3);
            check(r.client.view().playable,"DDS legacy exception");check(!r.client.mode(1),"no invented KS");
        });
        test("mode adoption still waits for valid SPI readback",()->{
            Rig r=new Rig();r.client.mode(1);r.steps(3);r.engine().put("valid",false);r.steps(5);
            check(!r.client.view().playable,"owner parameter ack without valid readback cannot play");
            r.engine().put("valid",true);r.ready();check(r.client.view().state.applied==1&&r.client.view().ownerMask==0,"valid readback plus empty re-arm");
        });
        test("parameters wait on parameter counter rather than processed alone",()->{
            Rig r=new Rig();r.client.parameter(7,30);r.step();r.hold=true;
            Json.list(r.device.mutableState().get("processed")).set(0,r.client.view().sequence);
            r.steps(4);check(r.client.view().state.n("volume")==207,"processed alone cannot claim patch adoption");
            r.hold=false;r.steps(4);check(r.client.view().state.n("volume")==60,"parameter counter and patch adopted");
        });
        test("expired background session is not silently re-used or replayed",()->{
            Rig r=new Rig();r.key(1);r.steps(3);int notes=r.nonzero();r.client.pause();r.steps(2);r.now+=10001;
            r.client.resume();r.steps(3);check(!r.client.view().playable&&r.client.view().token==0,"expired token disabled");
            r.client.connect(r.transport,true);r.ready();check(r.client.view().localMask==0&&r.nonzero()==notes,"new session requires fresh notes");
        });
        test("external recovery/mode/panic clears pointers and requires fresh input",()->{
            Rig r=new Rig();r.key(1);r.steps(3);long epoch=r.client.view().inputEpoch;
            r.engine().put("recovering",true);r.engine().put("recoveries",1L);r.now+=500;r.steps(3);
            check(!r.client.view().playable&&r.client.view().localMask==0&&r.client.view().inputEpoch>epoch,"recovery clears local");
            r.engine().put("recovering",false);r.ready();check(r.client.view().localMask==0,"no recovery replay");
        });
        test("sticky fault permits fresh notes only after recovery and valid readback",()->{
            Rig r=new Rig();r.key(1);r.steps(3);long epoch=r.client.view().inputEpoch;
            r.engine().put("fault",2L);r.engine().put("recoveries",1L);
            r.engine().put("recovering",true);r.engine().put("switching",true);r.engine().put("valid",false);
            r.now+=500;r.steps(3);
            check(!r.client.view().playable&&r.client.view().localMask==0&&r.client.view().queueSize==0
                    &&r.client.view().inputEpoch>epoch,"active recovery clears held input");
            check(!r.client.keys(4,100,r.now),"recovery rejects a new touch");int old=r.nonzero();
            r.engine().put("recovering",false);r.engine().put("switching",false);r.now+=500;r.steps(6);
            check(!r.client.view().playable,"invalid current readback still blocks after recovery flags clear");
            r.engine().put("valid",true);r.ready();r.steps(12);
            check(r.client.view().state.fault==2,"historical fault remains available for diagnostics");
            check(r.client.view().localMask==0&&r.client.view().ownerMask==0&&r.nonzero()==old,"old held note never replays");
            r.key(4);r.steps(3);check(r.client.view().ownerMask==4,"fresh touch works with sticky fault");
        });
        test("mode confirmation accepts sticky fault only when current mode is safe",()->{
            Rig r=new Rig();check(r.client.mode(1),"mode request");r.steps(3);
            r.engine().put("fault",2L);r.engine().put("recoveries",1L);
            r.engine().put("recovering",true);r.engine().put("switching",true);r.engine().put("valid",false);r.steps(5);
            check(!r.client.view().playable,"mode cannot confirm during recovery");
            check(r.client.view().state.parameters[0]>0,"owner has adopted mode");
            r.engine().put("recovering",false);r.engine().put("switching",false);r.engine().put("valid",true);
            r.engine().put("applied",0L);r.steps(5);check(!r.client.view().playable,"mismatched applied mode still blocks");
            r.engine().put("applied",1L);r.ready();
            check(r.client.view().state.applied==1&&r.client.view().state.fault==2,"safe readback confirms with fault retained");
            check(r.client.view().localMask==0&&r.client.view().ownerMask==0&&r.nonzero()==0,"mode re-arms empty");
            r.key(8);r.steps(3);check(r.client.view().ownerMask==8,"fresh KS note adopted after mode confirmation");
        });
        test("ordinary releases isolate clients; Stop clears all sources and test tone",()->{
            Rig r=new Rig();Transport.Response s=r.device.request("/api/session","");Map<String,Object> data=Json.object(s.body);
            long other=Json.number(data.get("token"));r.device.request("/api/command",Protocol.encode(other,1,3,1,100));r.device.adopt();
            r.key(1);r.steps(4);r.key(0);r.steps(4);
            check(Json.number(Json.list(r.device.mutableState().get("keys")).get(1))==1,"other client retained");
            r.device.mutableState().put("test_tone",true);check(r.client.panic(),"stop requested");r.steps(2);check(!r.client.view().playable,"stop awaiting adoption");
            r.ready();check(Json.number(Json.list(r.device.mutableState().get("keys")).get(1))==0,"global stop");
            check(!Json.bool(r.device.mutableState().get("test_tone")),"test tone cleared");
        });
        test("stable multi-pointer IDs and duplicate-key release",()->{
            TouchNotes t=new TouchNotes();check(t.move(42,0)==1,"first pointer");check(t.move(7,0)==1,"same key");
            check(t.up(42)==1,"one finger remains");check(t.move(7,24)==(1<<24),"glissando");
            t.move(99,1);check(t.up(7)==2,"pointer index change harmless");t.clear();check(t.mask()==0,"cancel clears all");
        });
        test("uint32 sequence exhaustion never wraps or kills the IO owner",()->{
            Rig r=new Rig();r.key(1);r.steps(3);
            try{java.lang.reflect.Field f=ControlSession.class.getDeclaredField("sequence");f.setAccessible(true);f.setLong(r.client,Protocol.UINT_MAX);}catch(Exception e){throw new AssertionError(e);}
            r.steps(3);check(r.client.view().token==0&&!r.client.view().playable&&r.client.view().localMask==0,"safe sequence boundary");
            r.client.connect(r.transport,true);r.ready();check(r.client.view().sequence==1,"new token restarts sequence");
        });
        System.out.println("RESULT "+passed+" protocol/lifecycle scenarios passed; hardware/audio unverified");
    }
}
