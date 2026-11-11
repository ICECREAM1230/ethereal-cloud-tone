package cn.fpgasynth.control.core;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** Literal BLE-MIDI fixtures; the injected link replaces only physical radio I/O. */
public final class BleMidiPortTest {
    private static int count;
    private interface IO {void run()throws Exception;}
    private static void check(boolean value,String why){count++;if(!value)throw new AssertionError(why);}
    private static IOException rejects(IO action,String why)throws Exception{
        try{action.run();}catch(IOException e){count++;return e;}
        throw new AssertionError(why);
    }
    private static void packet(byte[] actual,int... expected){
        byte[] literal=new byte[expected.length];
        for(int i=0;i<expected.length;i++)literal[i]=(byte)expected[i];
        check(Arrays.equals(actual,literal),"literal BLE-MIDI packet: "+Arrays.toString(actual));
    }
    private static List<MidiMessage> one(){return Collections.singletonList(MidiMessage.noteOn(1,60,100));}
    private static List<MidiMessage> five(){return Arrays.asList(
            MidiMessage.noteOn(1,60,100),MidiMessage.noteOn(1,64,127),
            MidiMessage.controlChange(16,7,99),MidiMessage.pitchBend(2,8192),MidiMessage.noteOff(3,60));}
    private static void wireFixtures()throws Exception{
        List<byte[]> packets=BleMidiCodec.encode(five(),255,20);
        check(packets.size()==2,"20-byte ATT payload splits five complete MIDI messages");
        packet(packets.get(0),0x81,0xff,0x90,60,100,0xff,0x90,64,127,
                0xff,0xbf,7,99,0xff,0xe1,0,64);
        packet(packets.get(1),0x81,0xff,0x82,60,0);
        List<byte[]> minimum=BleMidiCodec.encode(five(),0,5);
        check(minimum.size()==5,"minimum packet carries one complete three-byte message");
        packet(minimum.get(0),0x80,0x80,0x90,60,100);
        packet(minimum.get(4),0x80,0x80,0x82,60,0);
        List<byte[]> exact=BleMidiCodec.encode(five().subList(0,2),0,9);
        check(exact.size()==1,"exact capacity does not add an empty tail");
        packet(exact.get(0),0x80,0x80,0x90,60,100,0x80,0x90,64,127);
        List<byte[]> spare=BleMidiCodec.encode(five().subList(0,2),0,8);
        check(spare.size()==2&&spare.get(0).length==5&&spare.get(1).length==5,
                "packet space never splits a MIDI message or suppresses repeated status");
        check(BleMidiCodec.encode(Collections.emptyList(),0,20).isEmpty(),"idle encoding has no header-only packet");
    }
    private static void timestamps()throws Exception{
        packet(BleMidiCodec.encode(one(),0,20).get(0),0x80,0x80,0x90,60,100);
        packet(BleMidiCodec.encode(one(),127,20).get(0),0x80,0xff,0x90,60,100);
        packet(BleMidiCodec.encode(one(),128,20).get(0),0x81,0x80,0x90,60,100);
        packet(BleMidiCodec.encode(one(),8191,20).get(0),0xbf,0xff,0x90,60,100);
        packet(BleMidiCodec.encode(one(),8192,20).get(0),0x80,0x80,0x90,60,100);
        packet(BleMidiCodec.encode(one(),8193,20).get(0),0x80,0x81,0x90,60,100);
        packet(BleMidiCodec.encode(one(),Long.MAX_VALUE,20).get(0),0xbf,0xff,0x90,60,100);
    }
    private static void limits()throws Exception{
        rejects(()->BleMidiCodec.encode(one(),0,4),"packet limit below one message rejected");
        rejects(()->BleMidiCodec.encode(one(),0,515),"packet limit above supported ATT maximum rejected");
        rejects(()->BleMidiCodec.encode(null,0,20),"null message batch rejected");
        rejects(()->BleMidiCodec.encode(Arrays.asList(MidiMessage.noteOff(1,60),null),0,20),
                "null message rejected before any packet is returned");
        List<MidiMessage> many=new ArrayList<>();
        for(int i=0;i<128;i++)many.add(MidiMessage.controlChange(16,127,127));
        List<byte[]> full=BleMidiCodec.encode(many,8191,514);
        check(full.size()==1&&full.get(0).length==513,"largest supported ATT payload holds 128 full messages");
        byte[] p=full.get(0);
        check((p[0]&255)==0xbf&&(p[1]&255)==0xff&&(p[2]&255)==0xbf&&
                (p[509]&255)==0xff&&(p[510]&255)==0xbf&&(p[511]&255)==127&&(p[512]&255)==127,
                "maximum-sized packet retains first and final timestamp/status/data");
        many.add(MidiMessage.noteOff(1,60));
        List<byte[]> tail=BleMidiCodec.encode(many,8192,514);
        check(tail.size()==2&&tail.get(0).length==513,"one excess message starts a new complete packet");
        packet(tail.get(1),0x80,0x80,0x80,60,0);
    }
    private static class Link implements BleControlTransport.Link {
        BleControlTransport.Receiver receiver;
        final List<byte[]> packets=new ArrayList<>();
        int opens,closes,writes,limit=20,failAt;
        IOException openFailure;
        Runnable afterOpen,afterWrite;
        public void setReceiver(BleControlTransport.Receiver value){receiver=value;}
        public void open()throws IOException{opens++;if(openFailure!=null)throw openFailure;if(afterOpen!=null)afterOpen.run();}
        public int packetLimit(){return limit;}
        public void write(byte[] bytes)throws IOException{
            writes++;if(writes==failAt)throw new IOException("physical write failed");
            packets.add(bytes.clone());if(afterWrite!=null)afterWrite.run();
        }
        public void close(){closes++;}
    }
    private static void directOnly()throws Exception{
        Link link=new Link();BleMidiPort port=new BleMidiPort(link);
        check(link.opens==0&&link.writes==0&&link.closes==0,"construction performs no physical I/O");
        check(port.mode()==ControlPort.Mode.BLUETOOTH,"direct MIDI selects Bluetooth control route");
        port.open();port.open();port.check();
        check(link.opens==1&&link.writes==0,"open is physical only, idempotent and sends no protocol request");
        port.send(five(),255);
        check(link.writes==2,"send writes the ordered standard MIDI packets once");
        packet(link.packets.get(0),0x81,0xff,0x90,60,100,0xff,0x90,64,127,
                0xff,0xbf,7,99,0xff,0xe1,0,64);
        packet(link.packets.get(1),0x81,0xff,0x82,60,0);
        port.send(Collections.emptyList(),300);
        for(int i=0;i<100;i++)port.check();
        link.receiver.received(new byte[]{83,1,2,3});link.receiver.received(null);port.check();
        rejects(()->port.request("/api/v2/state",null),"direct MIDI rejects RPC instead of silently negotiating");
        check(link.writes==2&&link.opens==1,"idle checks, ignored input and rejected RPC produce no outgoing data");
        port.close();port.close();
        rejects(port::check,"closed port is not healthy");
        rejects(port::open,"closed port cannot reconnect itself");
        rejects(()->port.send(one(),0),"closed port cannot send or replay MIDI");
        check(link.closes==1&&link.writes==2&&link.opens==1,"close releases only this link exactly once");
        Link unopened=new Link();BleMidiPort fresh=new BleMidiPort(unopened);
        rejects(()->fresh.send(one(),0),"send requires explicit physical open");
        check(unopened.opens==0&&unopened.writes==0,"send never opens a link implicitly");fresh.close();
    }
    private static void failures()throws Exception{
        Link brokenOpen=new Link();brokenOpen.openFailure=new IOException("physical open failed");
        BleMidiPort opening=new BleMidiPort(brokenOpen);
        check(rejects(opening::open,"open failure reaches caller")==brokenOpen.openFailure,"physical open failure preserved");
        rejects(opening::open,"failed opening cannot retry");
        check(brokenOpen.opens==1&&brokenOpen.closes==1&&brokenOpen.writes==0,"failed open releases link without a request");
        Link brokenWrite=new Link();brokenWrite.limit=5;brokenWrite.failAt=2;BleMidiPort sending=new BleMidiPort(brokenWrite);
        sending.open();IOException error=rejects(()->sending.send(five(),0),"partial write failure reaches caller");
        check(rejects(sending::check,"failed port check reports original failure")==error,"first write failure is retained");
        rejects(()->sending.send(five(),1),"failed batch cannot replay");sending.close();
        check(brokenWrite.writes==2&&brokenWrite.packets.size()==1&&brokenWrite.closes==1,
                "partial failure stops remaining messages, closes link, and never retries");
        Link disconnected=new Link();BleMidiPort idle=new BleMidiPort(disconnected);idle.open();
        IOException disconnect=new IOException("asynchronous disconnect");disconnected.receiver.failed(disconnect);
        check(rejects(idle::check,"idle disconnect is visible without a status query")==disconnect,"disconnect reason preserved");
        rejects(()->idle.send(one(),0),"disconnected port cannot write");idle.close();
        check(disconnected.closes==1&&disconnected.writes==0,"asynchronous disconnect releases its own link immediately");
        Link lateOpen=new Link();lateOpen.afterOpen=()->lateOpen.receiver.failed(new IOException("disconnect during open"));
        BleMidiPort duringOpen=new BleMidiPort(lateOpen);
        rejects(duringOpen::open,"late open success cannot hide a disconnect");
        check(lateOpen.closes==1&&lateOpen.writes==0,"open callback failure cannot revive the port");
        Link lateWrite=new Link();lateWrite.afterWrite=()->lateWrite.receiver.failed(new IOException("disconnect during write"));
        BleMidiPort duringWrite=new BleMidiPort(lateWrite);duringWrite.open();
        rejects(()->duringWrite.send(five(),0),"final write checks asynchronous failure before reporting success");
        check(lateWrite.writes==1&&lateWrite.closes==1,"disconnect during a write stops later packets");
        Link badLimit=new Link();badLimit.limit=4;BleMidiPort invalid=new BleMidiPort(badLimit);invalid.open();
        rejects(()->invalid.send(one(),0),"bad physical packet limit cannot write a truncated message");
        check(badLimit.writes==0&&badLimit.closes==1,"invalid packet limit closes the unusable physical link");
    }
    private static void cancellation()throws Exception{
        final CountDownLatch started=new CountDownLatch(1),released=new CountDownLatch(1);
        Link blocked=new Link(){
            @Override public void write(byte[] packet)throws IOException{
                writes++;started.countDown();
                try{if(!released.await(2,TimeUnit.SECONDS))throw new IOException("test write timed out");}
                catch(InterruptedException e){Thread.currentThread().interrupt();throw new IOException(e);}
            }
            @Override public void close(){super.close();released.countDown();}
        };
        BleMidiPort port=new BleMidiPort(blocked);port.open();AtomicReference<Throwable> result=new AtomicReference<>();
        Thread sender=new Thread(()->{try{port.send(one(),0);}catch(Throwable e){result.set(e);}},"test-midi-write");
        sender.start();check(started.await(1,TimeUnit.SECONDS),"physical write entered");port.close();sender.join(1000);
        check(!sender.isAlive()&&result.get() instanceof IOException,"close cancels in-flight write without waiting on send lock");
        check(blocked.writes==1&&blocked.closes==1,"cancellation does not replay or close twice");
    }
    public static void main(String[] args)throws Exception{
        wireFixtures();timestamps();limits();directOnly();failures();cancellation();
        System.out.println("BleMidiPortTest PASS ("+count+" checks)");
    }
}
