package cn.fpgasynth.control.core;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** Tests the real direct session at the physical MIDI boundary; no RPC peer exists. */
public final class DirectMidiSessionTest {
    private static int checks,cases,failures;
    private interface Test {void run()throws Exception;}
    private static void check(boolean value,String why){checks++;if(!value)throw new AssertionError(why);}
    private static void eq(long want,long got,String why){check(want==got,why+": expected "+want+", got "+got);}
    private static void test(String name,Test body){
        cases++;try{body.run();System.out.println("PASS "+name);}catch(Throwable e){failures++;System.out.println("FAIL "+name+": "+e);e.printStackTrace(System.out);}
    }
    private static void invalid(Test body)throws Exception{
        try{body.run();}catch(IllegalArgumentException expected){checks++;return;}throw new AssertionError("invalid input accepted");
    }
    private static final class Port implements MidiPort {
        final List<MidiMessage> sent=Collections.synchronizedList(new ArrayList<MidiMessage>());
        volatile int opens,closes,checks,requests,sends;
        volatile boolean failSend,failCheck;
        volatile CountDownLatch entered,finish;
        public Mode mode(){return Mode.BLUETOOTH;}
        public void open(){opens++;}
        public void close(){closes++;}
        public void check()throws IOException{checks++;if(failCheck)throw new IOException("link disconnected");}
        public Response request(String path,String body){requests++;throw new AssertionError("direct MIDI must never request "+path);}
        public void send(List<MidiMessage> messages,long now)throws IOException{
            sends++;
            if(entered!=null){entered.countDown();try{if(!finish.await(3,TimeUnit.SECONDS))throw new IOException("test write blocked");}catch(InterruptedException e){Thread.currentThread().interrupt();throw new IOException(e);}}
            if(failSend)throw new IOException("ambiguous write failure");
            sent.addAll(messages);
        }
        void block(){entered=new CountDownLatch(1);finish=new CountDownLatch(1);}
    }
    private static DirectMidiSession session(Port port){port.open();DirectMidiSession s=new DirectMidiSession(null);s.connect(port);return s;}
    private static void message(Port p,int index,int status,int data1,int data2){
        check(index<p.sent.size(),"MIDI message "+index+" exists");MidiMessage m=p.sent.get(index);
        eq(status,m.status,"status "+index);eq(data1,m.data1,"data1 "+index);eq(data2,m.data2,"data2 "+index);
    }
    private static void readyAndIdle(){
        Port p=new Port();DirectMidiSession s=session(p);
        check(s.view().connected&&s.view().playable&&s.view().editable,"opened port is immediately ready");
        check(s.view().state!=null,"local defaults are available");eq(207,s.view().state.n("volume"),"default is local initial volume");
        check(Boolean.TRUE.equals(s.view().state.raw.get("local")),"local state explicitly marked");
        for(long token:s.view().state.tokens)eq(0,token,"no fabricated token");
        for(long ack:s.view().state.processed)eq(0,ack,"no fabricated owner acknowledgement");
        for(int i=0;i<120;i++)s.tick(i*25L);
        eq(1,p.opens,"session does not reopen port");eq(0,p.requests,"no capability, session or state RPC");eq(0,p.sent.size(),"idle produces no writes");
    }
    private static void fastTap(){
        Port p=new Port();DirectMidiSession s=session(p);
        check(s.keys(1,87,100),"down queued");check(s.keys(0,87,101),"up queued before IO");eq(0,s.view().sentMask,"queued note is not sent yet");
        s.tick(110);eq(1,s.view().sentMask,"successful attack tracked");s.tick(111);eq(0,s.view().sentMask,"successful release tracked");
        eq(2,p.sent.size(),"fast tap retains both transitions");message(p,0,0x90,48,87);message(p,1,0x80,48,0);
    }
    private static void chordDifference(){
        Port p=new Port();DirectMidiSession s=session(p);s.keys(5,76,100);s.tick(100);s.keys(12,51,101);s.tick(101);
        eq(4,p.sent.size(),"held common note is neither stopped nor retriggered");message(p,0,0x90,48,76);message(p,1,0x90,50,76);message(p,2,0x80,48,0);message(p,3,0x90,51,51);
        s.keys(12,100,102);s.tick(102);eq(4,p.sent.size(),"unchanged chord does not retrigger at another velocity");
        s.releaseNotes();s.tick(103);message(p,4,0x80,50,0);message(p,5,0x80,51,0);
    }
    private static void controllerOrder(){
        Port p=new Port();DirectMidiSession s=session(p);
        s.parameter(7,5);s.keys(1,100,100);s.parameter(74,11);s.keys(0,100,101);
        for(int i=0;i<4;i++)s.tick(110+i);
        eq(4,p.sent.size(),"CC and chord transitions remain in one input order");
        message(p,0,0xb0,7,5);message(p,1,0x90,48,100);message(p,2,0xb0,74,11);message(p,3,0x80,48,0);
    }
    private static void localParameters(){
        Port p=new Port();DirectMidiSession s=session(p);DeviceState before=s.view().state;
        s.parameter(7,100);eq(207,s.view().state.n("volume"),"queued value is not labelled sent");check(s.view().ccRequests.isEmpty(),"no fabricated sent value before write");
        s.tick(100);eq(200,s.view().state.n("volume"),"mapped volume uses unchanged legacy units");eq(100,s.view().ccRequests.get(7),"raw CC sent value");
        s.parameter(10,127);s.tick(101);eq(63,s.view().state.n("pan"),"mapped pan");
        s.parameter(1,77);s.tick(102);eq(77,s.view().ccRequests.get(1),"unmapped CC still sent");eq(200,s.view().state.n("volume"),"unmapped CC does not invent a patch field");
        message(p,2,0xb0,1,77);eq(207,before.n("volume"),"old local snapshots do not mutate");
        try{s.view().ccRequests.put(1,2);throw new AssertionError("mutable sent map");}catch(UnsupportedOperationException expected){checks++;}
    }
    private static void localMapping(){
        Port p=new Port();DirectMidiSession s=session(p);
        int[][] inputs={{14,127},{15,0},{20,127},{25,0},{26,127},{27,127},{28,30},{29,127},{30,63},{31,99},{71,127},{74,64},{106,7},{73,0},{75,127},{79,100},{72,0},{102,0},{103,127},{104,127},{105,0},{107,0},{113,127},{76,0},{77,127},{112,127},{110,127},{86,7},{87,120}};
        for(int[] input:inputs){check(s.parameter(input[0],input[1]),"mapped CC queued");s.tick(100);}
        DeviceState v=s.view().state;
        eq(12,v.a("osc",0,1),"octave mapping");eq(-42,v.a("osc",0,2),"signed truncation preserved");eq(3,v.a("osc",0,0),"oscillator wave");eq(0,v.a("osc",0,3),"pulse mapping at zero");
        eq(2,v.n("voice"),"voice mode");eq(31,v.n("detune"),"detune");eq(30,v.n("spread"),"spread");eq(2,v.a("filter",2),"filter type");eq(0,v.a("filter",3),"filter bypass");eq(99,v.a("filter",4),"filter mix");
        eq(5120,v.n("resonance_offset"),"resonance offset");eq(7,v.n("cutoff_offset"),"cutoff coarse/fine composition");
        eq(254,v.a("amp",0),"amp attack");eq(0,v.a("amp",1),"amp decay");eq(200,v.a("amp",2),"amp sustain");eq(254,v.a("amp",3),"amp release");
        eq(254,v.a("mod",0),"mod attack");eq(0,v.a("mod",1),"mod decay");eq(254,v.a("mod",2),"mod sustain");eq(254,v.a("mod",3),"mod release");eq(-16384,v.n("mod_depth"),"mod depth");
        eq(3,v.a("lfo",0,0),"LFO wave");eq(10,v.a("lfo",0,1),"LFO rate");eq(508,v.a("lfo",0,2),"LFO1 depth");eq(4064,v.a("lfo",1,2),"LFO2 destination scaling");eq(3,v.a("lfo",1,3),"LFO2 destination");eq(7,v.a("velocity",0),"velocity amp");eq(120,v.a("velocity",1),"velocity filter");
    }
    private static void modeChannels(){
        Port p=new Port();DirectMidiSession s=session(p);s.keys(1,70,100);s.tick(100);long epoch=s.view().inputEpoch;
        check(s.mode(1),"KS route selected");eq(0,s.view().localMask,"route change clears fingers");check(s.view().inputEpoch>epoch,"old touch epoch invalidated");check(!s.keys(2,71,101),"new route waits for old release");
        s.tick(101);eq(1,s.view().applied,"KS route after old NoteOff");s.keys(2,71,102);s.tick(102);
        check(s.mode(2),"FM MIDI route selected without native FM RPC");s.tick(103);eq(2,s.view().applied,"third route represented separately");eq(0,s.view().state.applied,"legacy local state remains a valid engine value");
        s.keys(4,72,104);s.tick(104);s.releaseNotes();s.tick(105);
        eq(6,p.sent.size(),"mode changes only release and route notes");message(p,0,0x90,48,70);message(p,1,0x80,48,0);message(p,2,0x91,49,71);message(p,3,0x81,49,0);message(p,4,0x92,50,72);message(p,5,0x82,50,0);
        eq(0,p.requests,"no engine-select or program RPC");
    }
    private static void panicChannels(){
        Port p=new Port();DirectMidiSession s=session(p);s.mode(2);s.tick(100);s.keys(1,80,101);s.tick(101);
        check(s.panic(),"stop accepted");s.tick(102);eq(8,p.sent.size(),"known release plus two standard stop CCs per performance channel");message(p,1,0x82,48,0);
        for(int i=0;i<3;i++){message(p,2+i*2,0xb0+i,123,0);message(p,3+i*2,0xb0+i,120,0);}
        eq(0,s.view().sentMask,"panic clears sent notes");eq(0,s.view().localMask,"panic clears input");s.tick(103);eq(8,p.sent.size(),"no repeated stop heartbeat");
    }
    private static void pauseRelease(){
        Port p=new Port();DirectMidiSession s=session(p);s.keys(1,100,100);s.tick(100);s.pause();check(!s.view().playable,"background blocks new playing");
        s.releaseNotes();s.tick(101);message(p,1,0x80,48,0);eq(0,s.view().sentMask,"release executes while paused");
        s.resume();s.tick(102);eq(2,p.sent.size(),"resume never replays old fingers");check(s.keys(1,100,103),"fresh down after resume accepted");s.tick(103);message(p,2,0x90,48,100);
    }
    private static void pauseDropsQueued(){
        Port p=new Port();DirectMidiSession s=session(p);s.keys(1,100,100);s.parameter(7,2);s.pause();s.tick(101);s.resume();s.tick(102);
        eq(0,p.sent.size(),"queued attack and CC discarded by pause");eq(0,s.view().localMask,"old touch absent");eq(0,s.view().queueSize,"old queue absent");
    }
    private static void disconnectRelease(){
        Port p=new Port();DirectMidiSession s=session(p);s.keys(1,100,100);s.tick(100);s.disconnect();check(!s.view().connected,"disconnect is immediate for UI");s.tick(101);message(p,1,0x80,48,0);
        Port fresh=new Port();fresh.open();s.connect(fresh);s.tick(102);eq(0,fresh.sent.size(),"new port has no old notes");check(s.view().playable,"new opened port ready");
    }
    private static void expiredAttack(){
        Port p=new Port();DirectMidiSession s=session(p);s.keys(1,100,100);s.tick(100);s.keys(3,100,110);long epoch=s.view().inputEpoch;s.tick(411);
        eq(2,p.sent.size(),"expired attack releases old note without playing new note");message(p,1,0x80,48,0);eq(0,s.view().localMask,"expired fingers cleared");check(s.view().inputEpoch>epoch,"expiration changes input epoch");
        s.tick(412);eq(2,p.sent.size(),"expired attack cannot replay later");
    }
    private static void freshnessBoundary(){
        Port p=new Port();DirectMidiSession s=session(p);s.keys(1,100,100);s.tick(400);message(p,0,0x90,48,100);s.keys(0,100,401);s.tick(900);message(p,1,0x80,48,0);
    }
    private static void schedulerGap(){
        Port p=new Port();DirectMidiSession s=session(p);s.keys(1,100,100);s.tick(100);s.tick(1100);message(p,1,0x80,48,0);eq(0,s.view().localMask,"one-second scheduling gap clears old fingers");s.tick(1101);eq(2,p.sent.size(),"gap does not trigger replay");
    }
    private static void freshAfterIdle(){
        Port p=new Port();DirectMidiSession s=session(p);s.tick(100);s.keys(1,90,5100);s.tick(5100);
        message(p,0,0x90,48,90);eq(1,s.view().sentMask,"fresh touch after idle is not an old held finger");
    }
    private static void queueBound(){
        Port p=new Port();DirectMidiSession s=session(p);
        for(int i=0;i<32;i++)check(s.keys((i&1)==0?1:0,100,100+i),"transition fits bounded queue");eq(32,s.view().queueSize,"32 transitions retained");
        check(!s.keys(1,100,132),"overflow rejected with safety clear");check(s.view().queueSize<=32,"queue never exceeds limit");eq(0,s.view().localMask,"overflow clears fingers");s.tick(133);eq(0,p.sent.size(),"overflow cannot play discarded attacks");
        check(s.keys(2,90,134),"fresh touch after overflow accepted");s.tick(134);message(p,0,0x90,49,90);
    }
    private static void controllerQueueBound(){
        Port p=new Port();DirectMidiSession s=session(p);for(int i=0;i<32;i++)check(s.parameter(7,i),"controller fits queue");check(!s.parameter(7,32),"controller overflow also bounded");s.tick(100);eq(0,p.sent.size(),"overflow removes unsent controller intent");
    }
    private static void sendFailure(){
        Port p=new Port();DirectMidiSession s=session(p);p.failSend=true;s.keys(1,100,100);s.tick(100);
        check(!s.view().connected&&!s.view().playable,"ambiguous send failure disconnects");eq(1,p.closes,"failed physical port closed");eq(0,s.view().sentMask,"failed send never updates sent mask");eq(0,s.view().localMask,"failure clears fingers");s.tick(101);eq(1,p.sends,"ambiguous NoteOn is never retried");
    }
    private static void backgroundFailure(){
        Port p=new Port();DirectMidiSession s=session(p);s.pause();p.failCheck=true;s.tick(100);check(!s.view().connected,"memory link failure detected in background");eq(1,p.closes,"background failure closes same link");eq(0,p.sends,"link check does not write");
    }
    private static Thread tickThread(DirectMidiSession s,long now,AtomicReference<Throwable> error){
        Thread worker=new Thread(()->{try{s.tick(now);}catch(Throwable e){error.set(e);}},"direct-midi-test-IO");worker.start();return worker;
    }
    private static void finish(Thread worker,Port port,AtomicReference<Throwable> error)throws Exception{
        port.finish.countDown();worker.join(2000);check(!worker.isAlive(),"IO worker finishes");if(error.get()!=null)throw new AssertionError("IO worker failed",error.get());
    }
    private static void nonblockingRelease()throws Exception{
        Port p=new Port();DirectMidiSession s=session(p);p.block();s.keys(1,100,100);AtomicReference<Throwable> error=new AtomicReference<>();Thread worker=tickThread(s,100,error);
        check(p.entered.await(1,TimeUnit.SECONDS),"physical write entered");eq(0,s.view().sentMask,"inflight is not successful yet");long started=System.nanoTime();s.releaseNotes();long elapsed=System.nanoTime()-started;
        check(elapsed<200_000_000L,"UI release is not blocked by GATT write");eq(0,s.view().localMask,"UI release clears local intent immediately");finish(worker,p,error);s.tick(101);message(p,1,0x80,48,0);eq(0,s.view().sentMask,"late successful attack is subsequently released");
    }
    private static void inflightPause()throws Exception{
        Port p=new Port();DirectMidiSession s=session(p);p.block();s.keys(1,100,100);AtomicReference<Throwable> error=new AtomicReference<>();Thread worker=tickThread(s,100,error);
        check(p.entered.await(1,TimeUnit.SECONDS),"physical write entered");s.pause();s.resume();finish(worker,p,error);s.tick(101);message(p,1,0x80,48,0);s.tick(102);eq(2,p.sent.size(),"late completion cannot replay paused input");eq(0,s.view().localMask,"late completion cannot restore local fingers");
    }
    private static void latePortSuccess()throws Exception{
        Port old=new Port();DirectMidiSession s=session(old);old.block();s.keys(1,100,100);AtomicReference<Throwable> error=new AtomicReference<>();Thread worker=tickThread(s,100,error);
        check(old.entered.await(1,TimeUnit.SECONDS),"old write entered");s.disconnect();Port fresh=new Port();fresh.open();s.connect(fresh);finish(worker,old,error);
        check(s.view().connected&&s.view().playable,"old success cannot resurrect or disrupt new generation");eq(0,s.view().sentMask,"old success cannot mark new link sent");eq(0,fresh.closes,"late old result cannot close new link");s.tick(101);eq(0,fresh.sent.size(),"new connection does not replay old attack");check(old.closes>=1,"superseded inflight port is closed");
    }
    private static void latePortFailure()throws Exception{
        Port old=new Port();DirectMidiSession s=session(old);old.block();old.failSend=true;s.keys(1,100,100);AtomicReference<Throwable> error=new AtomicReference<>();Thread worker=tickThread(s,100,error);
        check(old.entered.await(1,TimeUnit.SECONDS),"old write entered");s.disconnect();Port fresh=new Port();fresh.open();s.connect(fresh);finish(worker,old,error);
        check(s.view().playable,"old failure cannot disconnect new link");eq(0,fresh.closes,"old failure closes only old link");check(old.closes>=1,"old failed link closed");s.keys(2,80,101);s.tick(101);message(fresh,0,0x90,49,80);
    }
    private static void inputValidation()throws Exception{
        DirectMidiSession s=new DirectMidiSession(null);check(!s.keys(0,100,0),"disconnected keys rejected");check(!s.parameter(7,1),"disconnected CC rejected");check(!s.mode(1),"disconnected mode rejected");check(!s.panic(),"disconnected stop rejected");
        invalid(()->s.keys(-1,100,0));invalid(()->s.keys(1<<25,100,0));invalid(()->s.keys(1,0,0));invalid(()->s.keys(1,128,0));invalid(()->s.parameter(-1,0));invalid(()->s.parameter(128,0));invalid(()->s.parameter(7,128));invalid(()->s.mode(3));invalid(()->s.velocity(0));invalid(()->s.connect(null));
        Port p=new Port();s.connect(p);s.keys(1<<24,127,100);s.tick(100);message(p,0,0x90,72,127);
    }
    public static void main(String[] args){
        test("opened link ready without RPC or idle writes",DirectMidiSessionTest::readyAndIdle);
        test("fast tap keeps attack and release",DirectMidiSessionTest::fastTap);
        test("chord diff releases before additions",DirectMidiSessionTest::chordDifference);
        test("CC and notes retain input order",DirectMidiSessionTest::controllerOrder);
        test("local defaults and successful raw CC values",DirectMidiSessionTest::localParameters);
        test("legacy local mapping numerical fixtures",DirectMidiSessionTest::localMapping);
        test("three MIDI routes release old channel",DirectMidiSessionTest::modeChannels);
        test("stop sends release and CC123/120 on three channels",DirectMidiSessionTest::panicChannels);
        test("release executes while paused and resume needs new touch",DirectMidiSessionTest::pauseRelease);
        test("pause removes unsent attack and controller",DirectMidiSessionTest::pauseDropsQueued);
        test("disconnect release and clean reconnect",DirectMidiSessionTest::disconnectRelease);
        test("expired attack releases without replay",DirectMidiSessionTest::expiredAttack);
        test("300ms attack boundary and delayed release",DirectMidiSessionTest::freshnessBoundary);
        test("one second scheduler gap clears old fingers",DirectMidiSessionTest::schedulerGap);
        test("fresh touch after idle is eligible",DirectMidiSessionTest::freshAfterIdle);
        test("32 transition bound clears overflow",DirectMidiSessionTest::queueBound);
        test("controller queue shares bound",DirectMidiSessionTest::controllerQueueBound);
        test("ambiguous write closes and never retries",DirectMidiSessionTest::sendFailure);
        test("background link check detects failure without writes",DirectMidiSessionTest::backgroundFailure);
        test("UI release does not wait for physical write",DirectMidiSessionTest::nonblockingRelease);
        test("late completion after pause cannot revive input",DirectMidiSessionTest::inflightPause);
        test("late old success cannot affect new port",DirectMidiSessionTest::latePortSuccess);
        test("late old failure cannot close new port",DirectMidiSessionTest::latePortFailure);
        test("direct MIDI ranges and keyboard top note",DirectMidiSessionTest::inputValidation);
        System.out.println("Direct MIDI: "+checks+" assertions, "+cases+" cases, "+failures+" failures");
        if(failures!=0)throw new AssertionError("Direct MIDI failures: "+failures);
    }
}
