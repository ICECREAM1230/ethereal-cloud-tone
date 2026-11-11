package cn.fpgasynth.control.core;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.net.*;
import com.sun.net.httpserver.HttpServer;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicLong;

/** Literal wire fixtures and a separate peer exercise the real App session and ports. */
public final class ControlPortsTest {
    private static int count;
    private static void check(boolean b,String why){count++;if(!b)throw new AssertionError(why);}
    private interface IO {void run()throws Exception;}
    private static void rejects(IO action,String why)throws Exception{
        boolean rejected=false;try{action.run();}catch(IOException|IllegalArgumentException e){rejected=true;}
        check(rejected,why);
    }
    private static byte[] bytes(String s){return s.getBytes(StandardCharsets.UTF_8);}
    private static void frames()throws Exception{
        rejects(()->new BleControlProfile("1-1-1-1-1",BleControlProfile.REQUEST,BleControlProfile.RESPONSE),"abbreviated UUID cannot silently normalize");
        rejects(()->new BleControlProfile(BleControlProfile.SERVICE,BleControlProfile.REQUEST,BleControlProfile.REQUEST),"receive and transmit characteristics must differ");
        rejects(()->new BleControlProfile("03b80e5a-ede8-4b33-a751-6ce34ec4c700",BleControlProfile.REQUEST,BleControlProfile.RESPONSE),"private control port cannot target standard MIDI service");
        byte[] literal={83,1,1,3,1,2,3,4,0,0,0,2,123,125};
        List<byte[]> encoded=BleControlFrame.encode(1,0x01020304L,bytes("{}"),20);
        check(encoded.size()==1&&Arrays.equals(encoded.get(0),literal),"request header and big-endian ID match independent fixture");
        byte[] first={83,1,2,1,0,0,0,9,0,0,0,11,104,101,108,108,111,32};
        byte[] last={83,1,2,2,0,0,0,9,0,6,0,11,119,111,114,108,100};
        BleControlFrame.Assembler a=new BleControlFrame.Assembler(2,9);
        check(a.accept(first)==null,"partial response withheld");
        check(Arrays.equals(a.accept(last),bytes("hello world")),"literal fragmented response reassembled");
        BleControlFrame.Assembler order=new BleControlFrame.Assembler(2,9);
        rejects(()->order.accept(last),"last fragment cannot skip first");
        BleControlFrame.Assembler duplicate=new BleControlFrame.Assembler(2,9);duplicate.accept(first);
        rejects(()->duplicate.accept(first),"duplicate first fragment rejected");
        byte[] wrong=last.clone();wrong[9]=5;
        BleControlFrame.Assembler offset=new BleControlFrame.Assembler(2,9);offset.accept(first);
        rejects(()->offset.accept(wrong),"overlapping offset rejected");
        byte[] total=last.clone();total[11]=12;
        BleControlFrame.Assembler length=new BleControlFrame.Assembler(2,9);length.accept(first);
        rejects(()->length.accept(total),"total length cannot change mid-message");
        byte[] stale=first.clone();stale[7]=8;
        BleControlFrame.Assembler fresh=new BleControlFrame.Assembler(2,9);
        check(fresh.accept(stale)==null&&fresh.accept(first)==null,"old request ID ignored without poisoning new assembly");
        check(Arrays.equals(fresh.accept(last),bytes("hello world")),"new response survives stale packet");
        byte[] flags=first.clone();flags[3]=5;
        rejects(()->new BleControlFrame.Assembler(2,9).accept(flags),"unknown flags rejected");
        rejects(()->BleControlFrame.encode(1,0,bytes("x"),20),"zero ID rejected");
        rejects(()->BleControlFrame.encode(1,1,new byte[65536],20),"oversized message rejected");
        rejects(()->BleControlFrame.encode(1,1,bytes("x"),12),"packet needs header and payload");
        List<byte[]> edge=BleControlFrame.encode(1,2,new byte[65535],514);
        check(edge.size()==131&&edge.get(edge.size()-1).length==287,"maximum payload is bounded and fully fragmented");
    }
    // Peer framing is deliberately independent of BleControlFrame's serializer/parser.
    private static class Peer implements BleControlTransport.Link {
        BleControlTransport.Receiver receiver;
        final ByteArrayOutputStream input=new ByteArrayOutputStream();
        int opens,closes;volatile int writes;boolean silent,malformed,wrongOffset,failWrite;
        String responseBody="{\"ready\":true}",lastPath,lastBody;
        Transport backend;long currentId;int total;
        final List<Long> ids=new ArrayList<>();
        public void setReceiver(BleControlTransport.Receiver r){receiver=r;}
        public void open(){opens++;}
        public int packetLimit(){return 20;}
        public void close(){closes++;}
        private static int u16(byte[]p,int at){return (p[at]&255)*256+(p[at+1]&255);}
        public void write(byte[] p)throws IOException{
            writes++;if(failWrite)throw new IOException("write rejected");
            check(p.length<=20&&p[0]==83&&p[1]==1&&p[2]==1,"App sends only candidate request frames within ATT limit");
            long id=0;for(int i=4;i<8;i++)id=(id<<8)|(p[i]&255);
            if((p[3]&1)!=0){input.reset();currentId=id;total=u16(p,10);ids.add(id);}
            check(currentId==id&&u16(p,8)==input.size(),"request fragments remain ordered in one transaction");
            input.write(p,12,p.length-12);
            if((p[3]&2)==0)return;
            check(input.size()==total,"request total equals actual UTF-8 bytes");
            Map<String,Object> m=Json.object(new String(input.toByteArray(),StandardCharsets.UTF_8));
            check(m.size()==2&&m.get("path") instanceof String,"request exposes path/body envelope only");
            lastPath=(String)m.get("path");lastBody=(String)m.get("body");
            if(silent)return;
            Transport.Response result=backend==null?new Transport.Response(200,responseBody):backend.request(lastPath,lastBody);
            Map<String,Object> envelope=new LinkedHashMap<>();envelope.put("code",result.code);envelope.put("body",result.body);
            byte[] data=malformed?new byte[]{(byte)0xc0,(byte)0xaf}:bytes(Json.stringify(envelope));
            reply(id-1,bytes("{\"code\":200,\"body\":\"stale\"}"),false);
            reply(id,data,wrongOffset);
        }
        void reply(long id,byte[] data,boolean corrupt){
            for(int at=0;at<data.length;at+=8){int n=Math.min(8,data.length-at);byte[] p=new byte[12+n];
                p[0]=83;p[1]=1;p[2]=2;p[3]=(byte)((at==0?1:0)|(at+n==data.length?2:0));
                for(int i=0;i<4;i++)p[4+i]=(byte)(id>>(24-i*8));
                int off=corrupt&&at>0?at-1:at;p[8]=(byte)(off>>8);p[9]=(byte)off;
                p[10]=(byte)(data.length>>8);p[11]=(byte)data.length;System.arraycopy(data,at,p,12,n);receiver.received(p);
            }
        }
    }
    private static void rpc()throws Exception{
        Peer peer=new Peer();BleControlTransport port=new BleControlTransport(peer,100);
        Transport.Response response=port.request("/api/v2/state",null);
        check(response.code==200&&response.body.equals("{\"ready\":true}"),"fragmented peer response reaches caller, old ID ignored");
        check(peer.lastPath.equals("/api/v2/state")&&peer.lastBody==null,"GET represented by null body");
        peer.responseBody="弦音\n\"已收到\"";
        String body="1 2 3 4 5\n\"测试\"";
        response=port.request("/api/v2/command",body);
        check(peer.lastBody.equals(body)&&response.body.equals("弦音\n\"已收到\""),"Unicode, quoting and control characters preserved across wire");
        check(peer.opens==1&&peer.ids.equals(Arrays.asList(1L,2L)),"one link and unique sequential IDs per port");
        rejects(()->port.request("https://elsewhere/api/state",null),"absolute endpoint injection rejected");
        rejects(()->port.request("/api/../state",null),"path traversal rejected");
        port.close();int sent=peer.writes;
        rejects(()->port.request("/api/state",null),"closed port cannot reopen or send");
        check(peer.writes==sent&&peer.closes==1,"close idempotently releases physical link");
        for(int type=0;type<4;type++){
            Peer bad=new Peer();bad.silent=type==0;bad.malformed=type==1;bad.wrongOffset=type==2;bad.failWrite=type==3;
            BleControlTransport p=new BleControlTransport(bad,35);
            rejects(()->p.request("/api/state",null),"timeout/malformed UTF-8/offset/write failure reaches caller");
            int writes=bad.writes;rejects(()->p.request("/api/state",null),"failed port cannot replay next request");
            check(bad.closes==1&&bad.writes==writes,"failed port closed, no automatic retry");
        }
        Peer late=new Peer(){@Override public void write(byte[] packet)throws IOException{
            if((packet[3]&2)!=0)try{Thread.sleep(80);}catch(InterruptedException e){Thread.currentThread().interrupt();throw new IOException(e);}
            super.write(packet);
        }};
        BleControlTransport expired=new BleControlTransport(late,40);
        rejects(()->expired.request("/api/state",null),"response delivered inside final write after total deadline must not succeed");
        int lateWrites=late.writes;rejects(()->expired.request("/api/state",null),"expired final write cannot replay");
        check(late.closes==1&&late.writes==lateWrites,"total deadline closes late port without retry");
        Peer silent=new Peer();silent.silent=true;BleControlTransport waiting=new BleControlTransport(silent,2000);
        Thread request=new Thread(()->{try{waiting.request("/api/state",null);throw new AssertionError("closed wait returned success");}catch(IOException expected){}});
        request.start();long deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(1);
        while(silent.writes==0&&System.nanoTime()<deadline)Thread.yield();waiting.close();request.join(1000);
        check(!request.isAlive(),"close wakes a blocked response waiter");
    }
    private static class DevicePort implements ControlPort {
        final Mode mode;final DemoDevice device;final List<String> events;final String name;
        boolean closed;int commands;
        DevicePort(Mode m,String n,AtomicLong clock,List<String> e){mode=m;name=n;device=new DemoDevice(clock::get);events=e;}
        public Mode mode(){return mode;}
        public Response request(String path,String body)throws IOException{
            check(!closed,"closed physical port never receives requests");
            events.add(name+":"+path+":"+body);if(path.equals("/api/command"))commands++;
            return device.request(path,body);
        }
        public void close(){closed=true;events.add(name+":CLOSE");}
    }
    private static void tick(ControlPorts ports,AtomicLong clock,int n){for(int i=0;i<n;i++)ports.tick(clock.addAndGet(25));}
    private static void modes()throws Exception{
        AtomicLong clock=new AtomicLong();List<String> events=new ArrayList<>();AppController session=new AppController(null);ControlPorts ports=new ControlPorts(session);
        DevicePort wifi=new DevicePort(ControlPort.Mode.WIFI,"wifi",clock,events);ports.connect(wifi,clock.get());tick(ports,clock,12);
        check(session.view().playable,"existing Wi-Fi protocol discovers v1 and confirms empty keys");
        rejects(()->ports.connect(wifi,clock.get()),"same active port cannot be reattached");
        check(!wifi.closed&&session.view().playable,"invalid reattachment must not close the active connection");
        session.keys(1,100,clock.get());tick(ports,clock,4);check(session.view().ownerMask==1,"held note acknowledged before switching");
        Transport obsolete=ports.endpoint();ports.select(ControlPort.Mode.BLUETOOTH,clock.get());
        check(!session.view().playable&&session.view().localMask==0&&wifi.closed&&!ports.isOpen(),"mode selection clears intent and closes old port without auto-connect");
        int close=events.indexOf("wifi:CLOSE");String release=events.get(close-1);
        check(release.startsWith("wifi:/api/command:")&&release.endsWith(" 3 0 100"),"old empty-key command precedes close");
        int oldEvents=events.size();rejects(()->obsolete.request("/api/state",null),"obsolete lease cannot write after switch");
        tick(ports,clock,12);check(events.size()==oldEvents,"selecting Bluetooth does not poll Wi-Fi or implicitly open Bluetooth");
        DevicePort wrong=new DevicePort(ControlPort.Mode.WIFI,"wrong",clock,events);
        rejects(()->ports.connect(wrong,clock.get()),"cannot attach a port from the other mode");check(wrong.closed,"rejected port closed");
        Peer ble=new Peer();ble.backend=new DemoDevice(clock::get);BleControlTransport bluetooth=new BleControlTransport(ble,100);
        ports.connect(bluetooth,clock.get());tick(ports,clock,12);
        check(session.view().playable&&ble.opens==1,"same AppController works through actual BLE codec and peer");
        check(wifi.commands>0&&events.stream().skip(close+1L).noneMatch(e->e.startsWith("wifi:")),"Bluetooth session never falls through to Wi-Fi");
        session.keys(2,99,clock.get());tick(ports,clock,4);ports.disconnect(clock.get());int bleWrites=ble.writes;
        tick(ports,clock,12);check(ble.closes==1&&ble.writes==bleWrites&&!session.view().playable,"disconnect releases and blocks all future traffic");
        ports.select(ControlPort.Mode.WIFI,clock.get());ports.connectDemo(new DemoDevice(clock::get),clock.get());tick(ports,clock,12);
        check(session.view().demo&&session.view().playable&&!ports.isOpen(),"local demo uses neither physical port");
        ports.disconnect(clock.get());
        List<String> bleEvents=new ArrayList<>();
        Peer healthy=new Peer(){
            @Override public void write(byte[] packet)throws IOException{super.write(packet);if((packet[3]&2)!=0)bleEvents.add(lastPath+":"+lastBody);}
            @Override public void close(){super.close();bleEvents.add("CLOSE");}
        };
        healthy.backend=new DemoDevice(clock::get);ports.select(ControlPort.Mode.BLUETOOTH,clock.get());
        ports.connect(new BleControlTransport(healthy,100),clock.get());tick(ports,clock,12);
        session.keys(1,100,clock.get());tick(ports,clock,4);check(session.view().ownerMask==1,"BLE note held before background");
        Runnable oldAbort=ports.abortHandle();session.pause();ports.disconnect(clock.get());
        check(bleEvents.get(bleEvents.size()-2).endsWith(" 3 0 100")&&bleEvents.get(bleEvents.size()-1).equals("CLOSE"),"healthy BLE background exit sends empty keys before closing the link");
        Peer blocked=new Peer();blocked.silent=true;ports.connect(new BleControlTransport(blocked,2000),clock.get());
        Runnable abort=ports.abortHandle();Thread owner=new Thread(()->ports.tick(clock.get()));owner.setDaemon(true);owner.start();
        long end=System.nanoTime()+TimeUnit.SECONDS.toNanos(1);while(blocked.writes==0&&System.nanoTime()<end)Thread.yield();
        check(blocked.writes>0,"IO owner entered a blocked BLE request");abort.run();owner.join(1000);
        check(!owner.isAlive()&&blocked.closes==1,"out-of-band abort wakes a blocked IO owner");ports.disconnect(clock.get());
        DevicePort replacement=new DevicePort(ControlPort.Mode.BLUETOOTH,"replacement",clock,events);ports.connect(replacement,clock.get());
        oldAbort.run();abort.run();tick(ports,clock,12);
        check(!replacement.closed&&session.view().playable,"delayed abort captures the old instance and cannot close a new connection");
        ports.disconnect(clock.get());
    }
    private static void http()throws Exception{
        List<String> seen=Collections.synchronizedList(new ArrayList<>());HttpServer server=HttpServer.create(new InetSocketAddress("127.0.0.1",0),0);
        server.createContext("/api/echo",exchange->{
            ByteArrayOutputStream b=new ByteArrayOutputStream();byte[] buf=new byte[128];int n;
            while((n=exchange.getRequestBody().read(buf))!=-1)b.write(buf,0,n);
            seen.add(exchange.getRequestMethod()+":"+exchange.getRequestHeaders().getFirst("X-FPGA-Control")+":"+new String(b.toByteArray(),StandardCharsets.US_ASCII));
            byte[] reply=bytes("{\"accepted\":2}");exchange.sendResponseHeaders(202,reply.length);exchange.getResponseBody().write(reply);exchange.close();
        });
        server.createContext("/api/redirect",exchange->{exchange.getResponseHeaders().add("Location","/api/echo");exchange.sendResponseHeaders(302,-1);exchange.close();});
        server.createContext("/api/large",exchange->{byte[] large=new byte[65537];exchange.sendResponseHeaders(200,large.length);try{exchange.getResponseBody().write(large);}finally{exchange.close();}});
        server.start();
        try{
            String origin="http://127.0.0.1:"+server.getAddress().getPort();
            WifiControlPort port=new WifiControlPort(origin,url->(HttpURLConnection)url.openConnection());
            Transport.Response r=port.request("/api/echo","1 2 3 4 5");
            check(r.code==202&&r.body.equals("{\"accepted\":2}"),"Wi-Fi returns actual localhost status/body");
            check(seen.equals(Arrays.asList("POST:1:1 2 3 4 5")),"Wi-Fi sends exact original ASCII and required control header");
            port.request("/api/echo",null);check(seen.get(1).equals("GET:null:"),"null body remains query without write header");
            check(port.request("/api/redirect",null).code==302&&seen.size()==2,"Wi-Fi never follows a redirect to another endpoint");
            rejects(()->port.request("/api/large",null),"oversized Wi-Fi response rejected");
            rejects(()->port.request("/api/echo","弦"),"Wi-Fi ASCII ABI never silently substitutes Unicode");
            port.close();rejects(()->port.request("/api/echo",null),"closed Wi-Fi port cannot reconnect");
            check(seen.size()==2,"rejected requests never reach localhost peer");
        }finally{server.stop(0);}
    }
    public static void main(String[] args)throws Exception{frames();rpc();modes();http();System.out.println("Control ports: "+count+" assertions PASS");}
}
