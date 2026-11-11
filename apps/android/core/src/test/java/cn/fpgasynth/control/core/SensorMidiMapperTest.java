package cn.fpgasynth.control.core;

import java.util.ArrayList;
import java.util.List;

/** Standard MIDI boundary behavior using real motion frames and an observable target. */
public final class SensorMidiMapperTest {
    private static final long EPOCH=17,START=1000;
    private static int checks,cases,failures;
    private static void check(boolean value,String why){checks++;if(!value)throw new AssertionError(why);}
    private static void eq(int expected,int actual,String why){check(expected==actual,why+" expected "+expected+" got "+actual);}
    private static void test(String name,Runnable body){cases++;try{body.run();System.out.println("PASS "+name);}catch(AssertionError e){failures++;System.out.println("FAIL "+name+": "+e.getMessage());}}
    private static void rejects(Runnable operation,Class<? extends RuntimeException> kind,String why){
        boolean rejected=false;try{operation.run();}catch(RuntimeException e){rejected=kind.isInstance(e);}check(rejected,why);
    }
    private static void bytes(MidiMessage m,int a,int b,int c,String why){
        byte[] wire=m.bytes();eq(3,wire.length,why+" length");eq(a,wire[0]&255,why+" status");eq(b,wire[1]&255,why+" data1");eq(c,wire[2]&255,why+" data2");
        eq(a,m.status,why+" public status");eq(b,m.data1,why+" public data1");eq(c,m.data2,why+" public data2");
    }
    private static void message(List<MidiMessage> events,int index,int status,int a,int b,String why){
        check(index<events.size(),why+" message exists");MidiMessage m=events.get(index);eq(status,m.status,why+" status");eq(a,m.data1,why+" data1");eq(b,m.data2,why+" data2");
    }
    private static MotionInstrument motion(double roll,double pitch,boolean sounding,long now){
        MotionInstrument m=new MotionInstrument();long first=now-100;
        m.sample(0,0,0,first,first);check(m.calibrate(first),"real fixture calibration");check(m.arm(EPOCH,true,127,first),"real fixture arm");
        for(long at=first+1;at<now;at++)m.sample(roll,pitch,0,at,at);
        m.sample(roll,pitch,sounding?8:0,now,now);return m;
    }
    private static MotionInstrument.Frame frame(double roll,double pitch,boolean sounding,long now){return motion(roll,pitch,sounding,now).frame(now,EPOCH,true,127);}
    private static SensorMidiMapper mapper(int roll,int pitch){SensorMidiMapper m=new SensorMidiMapper();m.configure(roll,pitch);m.calibrate(0);m.arm(EPOCH,START);return m;}

    private static void standardBytes(){
        bytes(MidiMessage.noteOn(1,60,100),0x90,0x3c,0x64,"NoteOn");
        bytes(MidiMessage.noteOff(1,60),0x80,0x3c,0,"NoteOff");
        bytes(MidiMessage.controlChange(1,74,64),0xb0,0x4a,0x40,"CC74");
        bytes(MidiMessage.pitchBend(1,8192),0xe0,0,0x40,"pitch center");
        bytes(MidiMessage.pitchBend(16,16383),0xef,127,127,"pitch maximum");
        bytes(MidiMessage.pitchBend(1,0),0xe0,0,0,"pitch minimum");
        MidiMessage m=MidiMessage.noteOn(1,60,100);byte[] changed=m.bytes();changed[0]=0;changed[1]=0;
        bytes(m,0x90,60,100,"bytes are defensive copies");
        check(m.description()!=null&&!m.description().isEmpty(),"message has readable short description");
        for(int channel=1;channel<=16;channel++){
            MidiMessage[] values={MidiMessage.noteOn(channel,127,127),MidiMessage.noteOff(channel,0),MidiMessage.controlChange(channel,127,127),MidiMessage.pitchBend(channel,16383)};
            for(MidiMessage value:values){eq(channel,value.channel(),"channel round-trip");check((value.status&15)==channel-1,"channel status nibble");check(value.data1>=0&&value.data1<128&&value.data2>=0&&value.data2<128,"data bytes stay seven-bit");}
        }
    }
    private static void ranges(){
        for(int channel:new int[]{0,17,-1,128}){
            rejects(()->MidiMessage.noteOn(channel,60,1),IllegalArgumentException.class,"NoteOn channel rejected");
            rejects(()->MidiMessage.noteOff(channel,60),IllegalArgumentException.class,"NoteOff channel rejected");
            rejects(()->MidiMessage.controlChange(channel,74,64),IllegalArgumentException.class,"CC channel rejected");
            rejects(()->MidiMessage.pitchBend(channel,8192),IllegalArgumentException.class,"bend channel rejected");
        }
        for(int n:new int[]{-1,128}){
            rejects(()->MidiMessage.noteOn(1,n,1),IllegalArgumentException.class,"note outside seven-bit rejected");
            rejects(()->MidiMessage.noteOff(1,n),IllegalArgumentException.class,"off note outside seven-bit rejected");
            rejects(()->MidiMessage.controlChange(1,n,1),IllegalArgumentException.class,"CC number outside seven-bit rejected");
            rejects(()->MidiMessage.controlChange(1,74,n),IllegalArgumentException.class,"CC value outside seven-bit rejected");
        }
        for(int n:new int[]{-1,0,128})rejects(()->MidiMessage.noteOn(1,60,n),IllegalArgumentException.class,"attack velocity must be 1..127");
        for(int n:new int[]{-1,16384})rejects(()->MidiMessage.pitchBend(1,n),IllegalArgumentException.class,"pitch bend must be fourteen-bit");
        bytes(MidiMessage.noteOn(1,0,1),0x90,0,1,"legal lower note and attack");
        bytes(MidiMessage.controlChange(16,0,0),0xbf,0,0,"legal lower controller values");
    }
    private static void defaultNotes(){
        SensorMidiMapper m=new SensorMidiMapper();check(m.update(frame(0,0,true,1000),EPOCH,true,1000).isEmpty(),"mapper never arms itself");
        m.arm(EPOCH,1000);check(m.update(frame(0,0,false,1000),EPOCH,true,1000).isEmpty(),"defaults do not write any CC");
        List<MidiMessage> out=m.update(frame(35,0,true,1001),EPOCH,true,1001);eq(1,out.size(),"default attack emits only a note");message(out,0,0x90,72,64,"roll controls scale note");
        check(m.update(frame(35,0,true,1002),EPOCH,true,1002).isEmpty(),"held note is not retriggered");
        out=m.update(frame(35,0,false,1003),EPOCH,true,1003);eq(1,out.size(),"zero mask releases once");message(out,0,0x80,72,0,"release uses prior actual note");
        check(m.stop().isEmpty(),"stop after release is idempotent");
    }
    private static void configureGuards(){
        int[] rolls={SensorMidiMapper.TILT_NOTES,SensorMidiMapper.TILT_OFF,7,10,74};int[] pitches={-1,7,10,74};
        for(int roll:rolls)for(int pitch:pitches){SensorMidiMapper m=new SensorMidiMapper();if(roll>0&&roll==pitch)rejects(()->m.configure(roll,pitch),IllegalArgumentException.class,"two axes cannot share one CC");else m.configure(roll,pitch);}
        for(int bad:new int[]{-2,1,6,11,71,127,128}){
            SensorMidiMapper m=new SensorMidiMapper();rejects(()->m.configure(bad,-1),IllegalArgumentException.class,"unsupported roll mapping rejected");
            rejects(()->m.configure(0,bad),IllegalArgumentException.class,"unsupported pitch mapping rejected");
        }
        SensorMidiMapper m=mapper(0,-1);rejects(()->m.configure(7,-1),IllegalStateException.class,"armed quiet mapper requires stop before configure");
        m.update(frame(0,0,true,1001),EPOCH,true,1001);rejects(()->m.configure(7,-1),IllegalStateException.class,"active note cannot be silently discarded by configure");
        rejects(()->m.arm(EPOCH+1,1002),IllegalStateException.class,"arm cannot discard an active NoteOff");
        List<MidiMessage> off=m.stop();message(off,0,0x80,60,0,"failed configuration keeps the old note releasable");
        m.configure(7,-1);m.arm(EPOCH+1,1003);eq(2,m.update(frame(35,0,true,1003),EPOCH+1,true,1003).size(),"stopped mapper accepts new configuration");
    }
    private static void fixedNoteAndOrder(){
        SensorMidiMapper m=mapper(74,10);m.calibrate(20);
        List<MidiMessage> out=m.update(frame(35,20,true,1001),EPOCH,true,1001);eq(3,out.size(),"two CCs and attack");
        message(out,0,0xb0,74,127,"roll CC precedes note");message(out,1,0xb0,10,64,"pitch CC precedes note");message(out,2,0x90,60,64,"CC roll mapping fixes strike at C4");
        out=m.update(frame(-35,-15,false,1051),EPOCH,true,1051);eq(3,out.size(),"release plus two changed controllers");
        message(out,0,0x80,60,0,"NoteOff is before controller writes");message(out,1,0xb0,74,0,"roll lower bound");message(out,2,0xb0,10,0,"relative pitch lower bound");
        SensorMidiMapper off=mapper(SensorMidiMapper.TILT_OFF,-1);out=off.update(frame(-35,0,true,1001),EPOCH,true,1001);
        eq(1,out.size(),"disabled roll mapping emits no CC");message(out,0,0x90,60,64,"disabled roll mapping still uses fixed C4 for force strike");
    }
    private static void noteChange(){
        SensorMidiMapper m=mapper(0,7);List<MidiMessage> first=m.update(frame(0,0,true,1001),EPOCH,true,1001);
        message(first,0,0xb0,7,64,"initial current controller");message(first,1,0x90,60,64,"initial attack");
        List<MidiMessage> next=m.update(frame(35,35,true,1051),EPOCH,true,1051);eq(3,next.size(),"note transition includes off, controller and on");
        message(next,0,0x80,60,0,"old note off first");message(next,1,0xb0,7,127,"new controller second");message(next,2,0x90,72,64,"new note on last");
    }
    private static void ccBounds(){
        for(int cc:new int[]{7,10,74})for(int angle:new int[]{-80,-35,0,35,80}){
            SensorMidiMapper m=mapper(cc,-1);List<MidiMessage> out=m.update(frame(angle,0,false,1001),EPOCH,true,1001);
            int expected=angle<=-35?0:angle>=35?127:64;eq(1,out.size(),"roll only maps one configured controller");message(out,0,0xb0,cc,expected,"roll boundary/midpoint");
            SensorMidiMapper pitch=mapper(0,cc);out=pitch.update(frame(0,angle,false,1001),EPOCH,true,1001);
            eq(1,out.size(),"pitch only maps one configured controller");message(out,0,0xb0,cc,expected,"pitch boundary/midpoint");
        }
    }
    private static void pitchCalibration(){
        SensorMidiMapper m=mapper(0,74);m.calibrate(179);
        List<MidiMessage> out=m.update(frame(0,-179,false,1001),EPOCH,true,1001);message(out,0,0xb0,74,67,"pitch wrap is positive two degrees");
        out=m.update(frame(0,179,false,1051),EPOCH,true,1051);message(out,0,0xb0,74,64,"pitch calibration centers the original raw angle");
        for(double bad:new double[]{Double.NaN,Double.POSITIVE_INFINITY,Double.NEGATIVE_INFINITY})rejects(()->m.calibrate(bad),IllegalArgumentException.class,"invalid calibration rejected");
        out=m.update(frame(0,144,false,1101),EPOCH,true,1101);message(out,0,0xb0,74,0,"failed calibration preserves previous center");
    }
    private static void throttling(){
        SensorMidiMapper m=mapper(74,-1);List<MidiMessage> out=m.update(frame(0,0,false,1000),EPOCH,true,1000);message(out,0,0xb0,74,64,"first current CC");
        check(m.update(frame(35,0,false,1010),EPOCH,true,1010).isEmpty(),"10ms controller change is coalesced");
        check(m.update(frame(-35,0,false,1020),EPOCH,true,1020).isEmpty(),"20ms change remains coalesced");
        out=m.update(frame(35,0,true,1021),EPOCH,true,1021);eq(1,out.size(),"NoteOn is not delayed by CC throttle");message(out,0,0x90,60,64,"unthrottled attack");
        out=m.update(frame(-35,0,false,1022),EPOCH,true,1022);eq(1,out.size(),"NoteOff is not delayed by CC throttle");message(out,0,0x80,60,0,"unthrottled release");
        check(m.update(frame(35,0,false,1049),EPOCH,true,1049).isEmpty(),"49ms is still throttled");
        out=m.update(frame(-35,0,false,1050),EPOCH,true,1050);eq(1,out.size(),"50ms boundary sends one current value");message(out,0,0xb0,74,0,"latest current value wins over older pending values");
        check(m.update(frame(-35,0,false,1100),EPOCH,true,1100).isEmpty(),"identical CC is deduplicated after throttle");
        out=m.update(frame(0,0,false,1101),EPOCH,true,1101);message(out,0,0xb0,74,64,"deduplication does not advance the send clock");
    }
    private static void independentControllers(){
        SensorMidiMapper m=mapper(7,10);m.update(frame(0,0,false,1000),EPOCH,true,1000);
        List<MidiMessage> out=m.update(frame(0,35,false,1050),EPOCH,true,1050);eq(1,out.size(),"pitch changes alone");message(out,0,0xb0,10,127,"pitch independent send time");
        out=m.update(frame(35,-35,false,1051),EPOCH,true,1051);eq(1,out.size(),"roll can send while pitch is throttled");message(out,0,0xb0,7,127,"roll has independent 50ms throttle");
        out=m.update(frame(35,-35,false,1100),EPOCH,true,1100);eq(1,out.size(),"pitch's deferred current value eventually sends");message(out,0,0xb0,10,0,"latest pitch value");
    }
    private static void stopClearsControllers(){
        SensorMidiMapper m=mapper(74,-1);m.update(frame(0,0,false,1000),EPOCH,true,1000);
        check(m.update(frame(35,0,false,1020),EPOCH,true,1020).isEmpty(),"prepare an unsent controller change");
        check(m.stop().isEmpty(),"quiet stop needs no NoteOff");check(m.update(frame(35,0,true,1100),EPOCH,true,1100).isEmpty(),"stopped mapper emits neither CC nor NoteOn");
        m.arm(EPOCH,1101);List<MidiMessage> out=m.update(frame(0,0,false,1101),EPOCH,true,1101);
        eq(1,out.size(),"manual restart publishes only the current configured value");message(out,0,0xb0,74,64,"unsent old 127 is never replayed");
    }
    private static void cancellation(){
        for(int kind=0;kind<6;kind++){
            SensorMidiMapper m=mapper(74,-1);List<MidiMessage> on=m.update(frame(0,0,true,1001),EPOCH,true,1001);eq(2,on.size(),"prepare active note and CC");
            MotionInstrument.Frame f=frame(35,35,true,1051);long epoch=EPOCH,now=1051;boolean eligible=true;
            if(kind==0)epoch++;
            if(kind==1)eligible=false;
            if(kind==2){MotionInstrument source=motion(35,35,true,1000);f=source.frame(1251,EPOCH,true,127);}
            if(kind==3){MotionInstrument source=motion(35,35,true,1051);source.stop("已停止");f=source.frame(1051,EPOCH,true,127);}
            if(kind==4)f=null;
            if(kind==5)now=1000;
            List<MidiMessage> out=m.update(f,epoch,eligible,now);eq(1,out.size(),"cancellation emits only needed NoteOff");message(out,0,0x80,60,0,"cancellation release");
            check(m.update(frame(0,0,true,1100),EPOCH,true,1100).isEmpty(),"valid state cannot auto-restart after cancellation "+kind);
            check(m.stop().isEmpty(),"cancellation stop is idempotent");m.arm(EPOCH,1101);out=m.update(frame(0,0,true,1101),EPOCH,true,1101);
            eq(2,out.size(),"manual rearm restores current CC and attack");
        }
        SensorMidiMapper negative=mapper(0,-1);negative.update(frame(0,0,true,1001),EPOCH,true,1001);
        message(negative.update(frame(0,0,true,1002),EPOCH,true,-1),0,0x80,60,0,"negative clock also stops");
    }
    private static void immutableLists(){
        SensorMidiMapper m=mapper(0,-1);List<MidiMessage> on=m.update(frame(0,0,true,1001),EPOCH,true,1001);
        rejects(()->on.add(MidiMessage.noteOff(1,60)),UnsupportedOperationException.class,"update list is immutable");
        List<MidiMessage> off=m.stop();rejects(()->off.clear(),UnsupportedOperationException.class,"stop list is immutable");
        eq(1,on.size(),"later stop cannot mutate the previous output");check(m.stop().isEmpty(),"stop releases once");
    }
    private static final class RecordingTarget implements MidiControlAdapter.Target {
        final List<String> calls=new ArrayList<>();boolean acceptNotes=true,acceptCc=true;
        public boolean notes(int mask,int velocity,long now){calls.add("N:"+mask+":"+velocity+":"+now);return acceptNotes;}
        public boolean cc(int cc,int value){calls.add("C:"+cc+":"+value);return acceptCc;}
    }
    private static void call(RecordingTarget target,int index,String wanted,String why){check(index<target.calls.size(),why+" target called");check(wanted.equals(target.calls.get(index)),why+" expected "+wanted+" got "+target.calls.get(index));}
    private static void adapterNotes(){
        RecordingTarget t=new RecordingTarget();MidiOutput a=new MidiControlAdapter(t);
        check(a.send(MidiMessage.noteOn(1,48,100),1000),"adapter accepts lower C3");call(t,0,"N:1:100:1000","lower bit conversion");
        check(a.send(MidiMessage.noteOn(1,72,40),1001),"adapter accepts upper C5");call(t,1,"N:16777217:40:1001","upper bit plus held lower note");
        check(a.send(MidiMessage.noteOff(1,48),1002),"adapter accepts C3 release");call(t,2,"N:16777216:40:1002","release retains other note and valid accepted velocity");
        check(a.send(MidiMessage.noteOff(1,72),1003),"adapter accepts final release");call(t,3,"N:0:40:1003","zero mask with legal velocity");
    }
    private static void adapterRejects(){
        RecordingTarget t=new RecordingTarget();MidiControlAdapter a=new MidiControlAdapter(t);
        for(int channel:new int[]{2,16}){
            check(!a.send(MidiMessage.noteOn(channel,60,100),1000),"other NoteOn channel rejected");check(!a.send(MidiMessage.noteOff(channel,60),1000),"other NoteOff channel rejected");
            check(!a.send(MidiMessage.controlChange(channel,74,64),1000),"other CC channel rejected");
        }
        for(int note:new int[]{0,47,73,127}){check(!a.send(MidiMessage.noteOn(1,note,100),1000),"unsupported note attack rejected");check(!a.send(MidiMessage.noteOff(1,note),1000),"unsupported note release rejected");}
        for(int cc:new int[]{0,1,6,11,71,127})check(!a.send(MidiMessage.controlChange(1,cc,64),1000),"unsupported controller rejected");
        check(!a.send(MidiMessage.pitchBend(1,8192),1000),"adapter cannot represent pitch bend");check(!a.send(null,1000),"null message rejected");eq(0,t.calls.size(),"unsupported data never invokes target");
        for(int cc:new int[]{7,10,74}){check(a.send(MidiMessage.controlChange(1,cc,64),1001),"supported controller forwarded");}
        call(t,0,"C:7:64","volume");call(t,1,"C:10:64","pan");call(t,2,"C:74:64","filter");
    }
    private static void adapterAcceptance(){
        RecordingTarget t=new RecordingTarget();MidiControlAdapter a=new MidiControlAdapter(t);t.acceptNotes=false;
        check(!a.send(MidiMessage.noteOn(1,60,127),1000),"target rejection propagates");t.acceptNotes=true;
        check(a.send(MidiMessage.noteOn(1,62,30),1001),"next valid attack accepted");call(t,1,"N:16384:30:1001","rejected attack did not advance mask");
        t.acceptNotes=false;check(!a.send(MidiMessage.noteOff(1,62),1002),"release rejection propagates");check(!a.send(MidiMessage.noteOn(1,60,127),1003),"second rejected attack");
        t.acceptNotes=true;check(a.send(MidiMessage.noteOff(1,60),1004),"unheld-note release accepted");call(t,4,"N:16384:30:1004","rejected release and velocity did not advance shadow");
        t.acceptCc=false;check(!a.send(MidiMessage.controlChange(1,7,99),1005),"controller target rejection propagates");
    }
    private static void adapterReset(){
        RecordingTarget t=new RecordingTarget();MidiControlAdapter a=new MidiControlAdapter(t);a.send(MidiMessage.noteOn(1,60,90),1000);
        int before=t.calls.size();a.reset();eq(before,t.calls.size(),"reset does not make an unauthorized release call");
        a.send(MidiMessage.noteOn(1,48,50),1001);call(t,1,"N:1:50:1001","reset discards old shadow mask after caller release");
    }
    public static void main(String[] args){
        test("standard MIDI bytes and channels",SensorMidiMapperTest::standardBytes);
        test("seven/fourteen-bit input boundaries",SensorMidiMapperTest::ranges);
        test("default notes without CC writes",SensorMidiMapperTest::defaultNotes);
        test("mapping configuration and active guards",SensorMidiMapperTest::configureGuards);
        test("fixed C4 and event order",SensorMidiMapperTest::fixedNoteAndOrder);
        test("changed note releases before controllers/on",SensorMidiMapperTest::noteChange);
        test("tilt CC bounds and midpoint",SensorMidiMapperTest::ccBounds);
        test("pitch calibration and angle wrap",SensorMidiMapperTest::pitchCalibration);
        test("CC deduplication and latest-value throttle",SensorMidiMapperTest::throttling);
        test("independent per-controller throttles",SensorMidiMapperTest::independentControllers);
        test("stop discards unsent controller values",SensorMidiMapperTest::stopClearsControllers);
        test("generation/eligibility/freshness/clock stop",SensorMidiMapperTest::cancellation);
        test("immutable outputs and idempotent stop",SensorMidiMapperTest::immutableLists);
        test("legacy adapter 25-key conversion",SensorMidiMapperTest::adapterNotes);
        test("legacy adapter supported subset",SensorMidiMapperTest::adapterRejects);
        test("legacy adapter advances only after acceptance",SensorMidiMapperTest::adapterAcceptance);
        test("legacy adapter explicit shadow reset",SensorMidiMapperTest::adapterReset);
        System.out.println("Sensor MIDI: "+checks+" assertions, "+cases+" cases, "+failures+" failures");
        if(failures!=0)throw new AssertionError("Sensor MIDI failures: "+failures);
    }
}
