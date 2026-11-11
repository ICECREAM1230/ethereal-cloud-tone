package cn.fpgasynth.control.core;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

/** Actual C serializer shapes; explicit host owner observations, never device/audio receipts. */
public final class Ae350RoutingTest {
    private static int assertions,cases;
    private static Path fixtures;
    private static void check(boolean value,String why){assertions++;if(!value)throw new AssertionError(why);}
    private static void test(String name,Runnable run){run.run();cases++;System.out.println("PASS "+name);}
    private static void rejects(Runnable run){try{run.run();}catch(IllegalArgumentException expected){assertions++;return;}throw new AssertionError("expected invalid extension rejection");}
    private static String read(String name){try{return new String(Files.readAllBytes(fixtures.resolve(name)),StandardCharsets.UTF_8);}catch(IOException e){throw new IllegalStateException(e);}}
    private static final class Owner implements Transport {
        final Map<String,Object> caps=Json.object(read("capabilities.json"));
        final Map<String,Object> state=Json.object(read(Files.exists(fixtures.resolve("state-initial-client-ks.json"))?"state-initial-client-ks.json":"state-1.json"));
        final List<long[]> commands=new ArrayList<>(),pending=new ArrayList<>();
        int sessions;long token=1000;final int index=2;
        boolean hold,wrongResultContext,wrongResultSequence,skipChannel,dropNote;
        Owner(){for(Object row:Json.list(state.get("clients"))){Map<String,Object> c=Json.map(row);c.put("token",0L);c.put("keys",0L);}}
        Map<String,Object> client(){return Json.map(Json.list(state.get("clients")).get(index));}
        Map<String,Object> cpu(){return Json.map(state.get("cpu_mode"));}
        Map<String,Object> mode(){return Json.map(state.get("mode"));}
        long context(){return Json.number(state.get("context_epoch"));}
        int route(){return (int)Json.number(client().get("midi_channel"));}
        int count(int op){int n=0;for(long[] w:commands)if(w[3]==op)n++;return n;}
        public Response request(String path,String body)throws IOException{
            if(path.equals("/api/v2/capabilities"))return new Response(200,Json.stringify(caps));
            if(path.equals("/api/v2/session")){
                check("".equals(body),"session remains empty POST");sessions++;token++;
                for(String key:new ArrayList<>(client().keySet()))client().put(key,0L);
                client().put("token",token);client().put("midi_channel",1L);pending.clear();
                return new Response(201,"{\"api_major\":2,\"api_minor\":0,\"token\":"+token+",\"client\":2,\"lease_ms\":1200,\"context_epoch\":"+context()+"}");
            }
            if(path.equals("/api/v2/command")){
                String[] words=body.split(" ");check(words.length==7,"seven integer command");long[] w=new long[7];for(int i=0;i<7;i++)w[i]=Long.parseLong(words[i]);
                check(w[0]==token&&w[2]==context(),"current session/context");check(commands.isEmpty()||commands.get(commands.size()-1)[1]<w[1],"strict monotonic sequence");
                if(w[3]==5)check(w[5]==0&&w[6]==0,"CPU channel selection has backend/reserved zero");
                commands.add(w);pending.add(w);client().put("received",w[1]);
                if(dropNote&&w[3]==3&&w[4]!=0){dropNote=false;throw new IOException("accepted NoteOn reply lost");}
                return new Response(202,"{\"api_major\":2,\"accepted\":"+w[1]+",\"context_epoch\":"+context()+",\"stage\":\"received\",\"fpga\":\"unverified\"}");
            }
            if(path.equals("/api/v2/state")){
                if(!hold){for(long[] w:pending){
                    client().put("result_sequence",wrongResultSequence?w[1]+1:w[1]);client().put("result_context_epoch",wrongResultContext?context()+1:context());client().put("result_code",0L);
                    if(w[3]==3){client().put("processed",w[1]);client().put("keys",w[4]);}
                    else{client().put(w[3]==4?"stop_processed":"parameter_processed",w[1]);if(w[3]==5&&!skipChannel)client().put("midi_channel",w[4]);}
                }pending.clear();}
                return new Response(200,Json.stringify(state));
            }
            throw new IOException("unsupported test request "+path);
        }
    }
    private static final class Rig {
        final Owner owner=new Owner();final V2Session session=new V2Session(null);long now;
        Rig(){this(false);}
        Rig(boolean ble){session.connect(ble?new BleControlTransport(new BlePeer(owner)):owner,new FmApi.Caps(Json.stringify(owner.caps)));steps(8);check(session.view().playable,"initial AE350 empty-key handshake");}
        void step(){session.tick(now);now+=25;}
        void steps(int n){for(int i=0;i<n;i++)step();}
        void key(int mask){check(session.keys(mask,100,now),"key accepted");steps(4);}
    }
    /** Protocol loopback only; physical GATT/MTU is not exercised here. */
    private static final class BlePeer implements BleControlTransport.Link {
        final Transport target;BleControlTransport.Receiver receiver;BleControlFrame.Assembler input;long id;
        BlePeer(Transport target){this.target=target;}
        public void setReceiver(BleControlTransport.Receiver r){receiver=r;}
        public void open(){}
        public void close(){}
        public int packetLimit(){return 20;}
        public void write(byte[] packet)throws IOException{
            long next=0;for(int i=4;i<8;i++)next=(next<<8)|(packet[i]&255);
            if(next!=id){id=next;input=new BleControlFrame.Assembler(BleControlFrame.REQUEST,id);}
            byte[] complete=input.accept(packet);if(complete==null)return;
            Map<String,Object> request=Json.object(new String(complete,StandardCharsets.UTF_8));
            Transport.Response result=target.request((String)request.get("path"),(String)request.get("body"));
            Map<String,Object> envelope=new LinkedHashMap<>();envelope.put("code",result.code);envelope.put("body",result.body);
            for(byte[] reply:BleControlFrame.encode(BleControlFrame.RESPONSE,id,Json.stringify(envelope).getBytes(StandardCharsets.UTF_8),20))receiver.received(reply);
        }
    }
    public static void main(String[] args){
        fixtures=Paths.get(args[0]);
        test("adopted KS route completes while global editor stays SUB",()->{
            Rig r=new Rig();long token=r.session.view().token;check(r.session.mode(1,0),"KS selection queued");r.steps(20);
            check(r.owner.route()==1&&Json.number(r.owner.mode().get("applied"))==0,"distinct route and editor");
            check(r.session.view().playable&&r.session.view().canSwitch,"adopted route must leave switching gate");
            check(r.session.view().token==token&&r.owner.sessions==1,"route adoption retains session and channel");
        });
        test("all three routes selectable without native FM parameter capability",()->{
            Rig r=new Rig();check(!r.session.view().caps.engineAvailable(2),"legacy native FM remains unavailable");check(r.session.view().fmSelectable,"CPU route FM selection independent");
            for(int engine:new int[]{0,2,1,2}){check(r.session.mode(engine,0),"route selection "+engine);r.steps(12);check(r.owner.route()==engine&&r.session.view().playable,"route adopted "+engine);r.key(1);r.key(0);}
            check(!r.session.view().fmEditable&&!r.session.fmParameter(0,0,0,0)&&!r.session.fmPreset(0),"operator/preset APIs not fabricated");check(r.owner.sessions==1,"no route reset to default KS");
        });
        test("202 alone cannot confirm route",()->{
            Rig r=new Rig();check(r.session.mode(0,0),"select SUB");r.steps(2);r.owner.hold=true;r.steps(10);check(!r.session.view().playable,"wait for actual owner adoption");r.owner.hold=false;r.steps(8);check(r.owner.route()==0&&r.session.view().playable,"adopt after observation");
        });
        test("parameter ack without matching client channel cannot confirm",()->{
            Rig r=new Rig();r.owner.skipChannel=true;check(r.session.mode(0,0),"select SUB");r.steps(25);check(!r.session.view().playable,"wrong route keeps gated");
        });
        test("wrong result context cannot confirm selected route",()->{
            Rig r=new Rig();r.owner.wrongResultContext=true;check(r.session.mode(0,0),"select SUB");r.steps(25);check(!r.session.view().playable,"wrong result identity remains pending");
        });
        test("later unrelated result cannot confirm selected route",()->{
            Rig r=new Rig();r.owner.wrongResultSequence=true;check(r.session.mode(0,0),"select SUB");r.steps(25);check(!r.session.view().playable,"wrong result sequence remains pending");
        });
        test("CPU fault clears held notes and prevents new NoteOn",()->{
            Rig r=new Rig();r.key(1);r.owner.cpu().put("fault",true);r.steps(15);check(!r.session.view().playable&&r.session.view().localMask==0,"fault gates and clears");check(!r.session.keys(1,100,r.now),"no NoteOn during fault");
        });
        test("raw input STOP fence clears held notes even when CPU remains ready",()->{
            Rig r=new Rig();r.key(1);Map<String,Object> inputs=Json.map(Json.map(r.owner.state.get("legacy")).get("inputs"));
            inputs.put("stop_pending",true);inputs.put("ready",false);r.steps(15);
            check(!r.session.view().playable&&r.session.view().localMask==0,"input STOP fence clears and gates performance");
            inputs.put("stop_pending",false);inputs.put("ready",true);r.steps(15);check(r.session.view().localMask==0,"input recovery never replays old notes");
        });
        test("CPU readiness loss clears held input without replay",()->{
            Rig r=new Rig();r.key(1);r.owner.cpu().put("ready",false);r.steps(15);check(!r.session.view().playable&&r.session.view().localMask==0,"unready CPU gated");r.owner.cpu().put("ready",true);r.steps(15);check(r.session.view().localMask==0,"recovery never replays old note");
        });
        test("other client's channel changes do not disrupt this source",()->{
            Rig r=new Rig();r.key(1);Json.map(Json.list(r.owner.state.get("clients")).get(0)).put("midi_channel",2L);r.steps(20);check(r.session.view().playable&&r.session.view().localMask==1,"use active client index2");
        });
        test("unexpected own route change releases old input",()->{
            Rig r=new Rig();r.key(1);r.owner.client().put("midi_channel",2L);r.steps(15);check(r.session.view().localMask==0,"external own-route change clears held mask");
        });
        test("global editor changes never impersonate the performance route",()->{
            Rig r=new Rig();r.key(1);r.owner.mode().put("switching",true);r.owner.mode().put("requested",2L);r.steps(20);check(r.session.view().playable&&r.owner.route()==1&&r.session.view().localMask==1,"parallel CPU route survives editor-only change");check(!r.session.view().editable,"unstable editor controls remain gated");
        });
        test("route change releases old keys before selecting and preserves empty state",()->{
            Rig r=new Rig();r.key(1);int begin=r.owner.commands.size();check(r.session.mode(2,0),"select FM");r.steps(15);int off=-1,select=-1;for(int i=begin;i<r.owner.commands.size();i++){long[] w=r.owner.commands.get(i);if(w[3]==3&&w[4]==0&&off<0)off=i;if(w[3]==5)select=i;}
            check(off>=0&&select>off,"empty key adopted before select");check(r.session.view().localMask==0&&r.session.view().ownerMask==0,"no old note after select");
        });
        test("250ms heartbeat and pause safety retained",()->{
            Rig r=new Rig();check(r.session.mode(2,0),"FM route");r.steps(12);r.key(3);int before=r.owner.count(3);r.steps(100);check(r.owner.count(3)-before>=9&&r.session.view().ownerMask==3,"lease heartbeat continues");r.session.pause();r.steps(4);check(r.session.view().localMask==0&&!r.session.view().playable,"background cleared");
        });
        test("missing or invalid declared MIDI channel is rejected",()->{
            Owner o=new Owner();Map<String,Object> c=o.client();FmApi.Caps caps=new FmApi.Caps(Json.stringify(o.caps));c.remove("midi_channel");rejects(()->new FmApi.State(Json.stringify(o.state),caps));c.put("midi_channel",16L);rejects(()->new FmApi.State(Json.stringify(o.state),caps));
        });
        test("SUB_PATCH1 failure gates affected CC without disabling notes or other controls",()->{
            Rig r=new Rig();Json.map(r.owner.state.get("sub_patch")).put("valid",false);r.steps(15);
            check(r.session.view().playable,"invalid patch observation does not disconnect performance");
            check(!r.session.parameter(7,90),"CC7 blocked locally when native SUB patch is unready");
            check(r.session.parameter(10,64),"unaffected pan CC remains usable");r.steps(8);check(r.session.view().playable,"no avoidable 503 disconnect");
        });
        test("same AE350 route semantics through fragmented private BLE RPC",()->{
            Rig r=new Rig(true);check(r.session.mode(2,0),"BLE select FM");r.steps(16);check(r.session.view().playable&&r.owner.route()==2,"BLE route adopted");r.key(1);r.key(0);check(r.session.view().ownerMask==0&&r.owner.sessions==1,"BLE NoteOff and session retained");
        });
        test("App view exposes performance and editor independently",()->{
            Owner owner=new Owner();AppController app=new AppController(null);app.connect(owner,false);long clock=0;for(int i=0;i<12;i++){app.tick(clock);clock+=25;}
            AppController.View initial=app.view();check(initial.channelRoutes&&initial.performanceApplied==1&&initial.midiChannel==1&&initial.applied==0,"initial performance KS/channel2 versus global SUB");
            check(app.mode(2),"App FM route bypasses unavailable native catalog only for routing");for(int i=0;i<16;i++){app.tick(clock);clock+=25;}
            AppController.View after=app.view();check(after.playable&&after.performanceApplied==2&&after.midiChannel==2&&after.applied==0,"FM route/channel3 and global SUB stay independent");check(!after.fmEditable&&after.catalog==null,"native FM editing remains blocked");
        });
        System.out.println("AE350 routing: "+cases+" cases / "+assertions+" assertions PASS; actual C shapes with host owner observations, no hardware/audio");
    }
}
