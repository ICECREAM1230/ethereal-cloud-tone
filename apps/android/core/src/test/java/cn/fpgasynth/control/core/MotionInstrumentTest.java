package cn.fpgasynth.control.core;

/** Observable motion performance and cancellation; no Android or device doubles. */
public final class MotionInstrumentTest {
    private static int checks,cases,failures;
    private static final long EPOCH=17;
    private static void check(boolean value,String why){checks++;if(!value)throw new AssertionError(why);}
    private static void eq(int expected,int actual,String why){check(expected==actual,why+" expected "+expected+" got "+actual);}
    private static void test(String name,Runnable body){
        cases++;try{body.run();System.out.println("PASS "+name);}catch(AssertionError e){failures++;System.out.println("FAIL "+name+": "+e.getMessage());}
    }
    private static MotionInstrument.Frame frame(MotionInstrument m,long now){return m.frame(now,EPOCH,true,127);}
    private static void sample(MotionInstrument m,double roll,double force,long at){m.sample(roll,0,force,at,at);}
    private static MotionInstrument armed(){
        MotionInstrument m=new MotionInstrument();sample(m,0,0,0);
        check(m.calibrate(0),"fresh quiet sample calibrates");
        check(m.arm(EPOCH,true,127,0),"explicit start arms a calibrated quiet source");return m;
    }
    private static long settle(MotionInstrument m,double roll,double force,long from,int samples){
        long now=from;for(int i=0;i<samples;i++){now+=10;sample(m,roll,force,now);}return now;
    }
    private static void sounding(MotionInstrument.Frame f,int note,String why){
        check(f.armed&&f.fresh,why+" must remain armed and fresh");eq(note,f.note,why+" note");
        eq(1<<(note-48),f.mask,why+" mask");check(Integer.bitCount(f.mask)==1,why+" is monophonic");
        check((f.mask&~0x1ffffff)==0,why+" stays in the 25-key wire bitmap");
        check(f.velocity>=1&&f.velocity<=127,why+" velocity is valid");
    }
    private static void stopped(MotionInstrument.Frame f,String why){
        check(!f.armed,why+" cancels armed state");eq(0,f.mask,why+" releases the key");
    }
    private static void initial(){
        MotionInstrument m=new MotionInstrument();MotionInstrument.Frame f=frame(m,0);
        check(!f.fresh&&!f.calibrated&&!f.armed,"initial state requires a sample, calibration and manual start");
        eq(0,f.mask,"initial state cannot send a note");eq(60,f.note,"neutral preview is actual MIDI C4");
        check(f.velocity>=1&&f.velocity<=127,"an idle zero-mask frame can be passed to the session");
        check(f.status!=null&&!f.status.isEmpty(),"initial status is readable");
        sample(m,0,8,1);stopped(frame(m,1),"sensor arrival alone");
    }
    private static void calibration(){
        MotionInstrument m=new MotionInstrument();m.sample(19,-6,1.49,100,100);
        check(m.calibrate(100),"quiet fresh roll becomes the center");MotionInstrument.Frame f=frame(m,100);
        check(f.fresh&&f.calibrated&&!f.armed,"calibration does not start performance");eq(60,f.note,"calibrated angle selects C4");
        check(Math.abs(f.roll)<.000001&&f.pitch==-6,"relative roll is zero and pitch remains observable");
        long now=settle(m,54,0,100,35);eq(72,frame(m,now).note,"+35 relative degrees reaches C5");
        now=settle(m,-16,0,now,40);eq(48,frame(m,now).note,"-35 relative degrees reaches C3");
    }
    private static void calibrationGuards(){
        MotionInstrument m=new MotionInstrument();check(!m.calibrate(0),"missing sample cannot calibrate");
        sample(m,0,1.5,1);check(!m.calibrate(1),"exact low threshold is not quiet enough");
        sample(m,0,0,2);check(!m.calibrate(253),"sample older than 250ms cannot calibrate");
        check(!frame(m,253).calibrated,"failed calibration does not claim success");
        sample(m,12,0,254);check(m.calibrate(254),"new valid quiet data permits calibration");
        check(m.arm(EPOCH,true,127,254),"calibrated source starts");sample(m,12,4,255);
        check(!m.calibrate(255),"calibration while moving is rejected");
        MotionInstrument.Frame f=frame(m,255);stopped(f,"calibration request while playing");
        check(f.calibrated,"failed recalibration retains the prior usable center");
    }
    private static void manualStart(){
        MotionInstrument m=new MotionInstrument();check(!m.arm(EPOCH,true,127,0),"missing calibration cannot start");
        sample(m,0,0,1);check(m.calibrate(1),"prepare center");
        check(!m.arm(EPOCH,false,127,1),"ineligible route cannot start");
        check(!m.arm(EPOCH,true,0,1),"zero velocity ceiling cannot start");
        check(!m.arm(EPOCH,true,128,1),"out-of-range ceiling cannot start");
        sample(m,0,1.5,2);check(!m.arm(EPOCH,true,127,2),"starting requires force strictly below 1.5");
        sample(m,0,0,3);sample(m,0,7,4);stopped(frame(m,4),"calibrated data never starts itself");
        sample(m,0,0,5);check(m.arm(EPOCH,true,127,5),"manual quiet start succeeds");
        eq(0,frame(m,5).mask,"starting alone never emits NoteOn");
    }
    private static void threshold(){
        MotionInstrument m=armed();sample(m,0,3.999,10);eq(0,frame(m,10).mask,"below attack threshold stays silent");
        sample(m,0,4,20);sounding(frame(m,20),60,"exact attack threshold");
    }
    private static void lowGate(){
        MotionInstrument m=armed();sample(m,0,4,10);sample(m,0,1.5,100);sample(m,0,4,310);
        eq(0,frame(m,310).mask,"force equal to low gate does not re-enable a strike");
        sample(m,0,1.49,320);sample(m,0,4,321);sounding(frame(m,321),60,"strictly below low gate permits the next strike");
    }
    private static void sustainedHigh(){
        MotionInstrument m=armed();sample(m,0,8,10);
        for(int now=110;now<=1010;now+=100){sample(m,0,8,now);MotionInstrument.Frame f=frame(m,now);
            check(f.armed,"valid sustained motion stays enabled");if(now>=190)eq(0,f.mask,"held high acceleration cannot repeat");}
    }
    private static void refractory(){
        MotionInstrument m=armed();sample(m,0,4,10);sample(m,0,0,100);sample(m,0,4,309);
        eq(0,frame(m,309).mask,"299ms attack interval is rejected");sample(m,0,4,310);
        eq(0,frame(m,310).mask,"rejected crossing cannot replay when cooldown expires");
        sample(m,0,0,311);sample(m,0,4,312);sounding(frame(m,312),60,"new crossing after 300ms");
        MotionInstrument exact=armed();sample(exact,0,4,10);sample(exact,0,0,100);sample(exact,0,4,310);
        sounding(frame(exact,310),60,"300ms boundary is inclusive");
    }
    private static void timedRelease(){
        MotionInstrument m=armed();sample(m,0,5,10);sounding(frame(m,189),60,"179ms note remains on");
        MotionInstrument.Frame f=frame(m,190);eq(0,f.mask,"180ms timer releases without another sensor callback");
        check(f.armed&&f.fresh,"a normal timer release keeps performance enabled");
    }
    private static void lockedNote(){
        MotionInstrument m=armed();sample(m,0,8,10);long now=settle(m,35,8,10,17);
        sounding(frame(m,now),60,"changing selection during a strike cannot jump its note");
        eq(0,frame(m,190).mask,"locked note is released on time");eq(72,frame(m,190).note,"preview exposes the new selection after release");
        sample(m,35,0,200);sample(m,35,8,310);sounding(frame(m,310),72,"new strike uses the new selection");
    }
    private static void hysteresis(){
        MotionInstrument m=armed();long now=settle(m,2.6,0,0,40);eq(60,frame(m,now).note,"small center-side boundary noise does not switch note");
        now=settle(m,3.6,0,now,40);eq(62,frame(m,now).note,"a deliberate tilt crosses into D4");
        for(int i=0;i<60;i++){now+=10;sample(m,i%2==0?2.4:2.6,0,now);eq(62,frame(m,now).note,"midpoint jitter does not switch back");}
        now=settle(m,1.4,0,now,40);eq(60,frame(m,now).note,"moving decisively back returns to C4");
    }
    private static void smoothing(){
        MotionInstrument m=armed();sample(m,35,0,10);MotionInstrument.Frame f=frame(m,10);
        check(f.roll>0&&f.roll<35,"a single extreme sample is smoothed");check(f.note>60&&f.note<72,"one impulse cannot jump all the way to an endpoint");
        long now=settle(m,0,0,10,40);eq(60,frame(m,now).note,"stable neutral samples settle back to C4");
    }
    private static void scale(){
        int[] angles={-35,-30,-25,-20,-15,-10,-5,0,5,10,15,20,25,30,35};
        int[] notes={48,50,52,53,55,57,59,60,62,64,65,67,69,71,72};
        for(int i=0;i<angles.length;i++){
            MotionInstrument m=armed();long now=settle(m,angles[i],0,0,45);sample(m,angles[i],6,now+1);
            sounding(frame(m,now+1),notes[i],"C-major selection at "+angles[i]+" degrees");
        }
        MotionInstrument m=armed();long now=settle(m,-80,0,0,40);eq(48,frame(m,now).note,"left tilt clamps to C3");
        now=settle(m,80,0,now,40);eq(72,frame(m,now).note,"right tilt clamps to C5");
    }
    private static void rollWrap(){
        MotionInstrument m=new MotionInstrument();sample(m,179,0,0);check(m.calibrate(0),"roll near wrap calibrates");
        long now=settle(m,-179,0,0,35);MotionInstrument.Frame f=frame(m,now);
        check(f.roll>0&&f.roll<3,"crossing +180 to -180 is a small positive relative tilt");eq(60,f.note,"angle wrap does not jump to C3");
    }
    private static void velocity(){
        double[] forces={4,5,8,12,50,Double.MAX_VALUE};int[] ceilings={1,17,64,127};
        for(int ceiling:ceilings){int previous=0;for(double force:forces){
            MotionInstrument m=armed();m.frame(0,EPOCH,true,ceiling);sample(m,0,force,1);MotionInstrument.Frame f=m.frame(1,EPOCH,true,ceiling);
            check(f.mask!=0,"finite attack force triggers");check(f.velocity>=1&&f.velocity<=ceiling,"attack stays under velocity ceiling "+ceiling);
            check(f.velocity>=previous,"stronger attacks never become weaker");previous=f.velocity;
        }}
        MotionInstrument weak=armed();sample(weak,0,4,1);int low=frame(weak,1).velocity;
        MotionInstrument strong=armed();sample(strong,0,12,1);int high=frame(strong,1).velocity;
        check(high>low,"attack force has an audible velocity effect");eq(127,high,"strong attack reaches full MIDI velocity");
        eq(20,strong.frame(2,EPOCH,true,20).velocity,"current lowered ceiling also bounds an active strike");
        stopped(strong.frame(3,EPOCH,true,0),"invalid ceiling");
    }
    private static void freshness(){
        MotionInstrument m=armed();MotionInstrument.Frame f=frame(m,250);check(f.fresh&&f.armed,"250ms age is still fresh");
        f=frame(m,251);check(!f.fresh,"251ms age expires");stopped(f,"sensor silence");
        sample(m,0,0,252);sample(m,0,8,253);f=frame(m,253);check(f.fresh&&f.calibrated,"new data recovers freshness and retains center");
        stopped(f,"data recovery without manual start");sample(m,0,0,254);check(m.arm(EPOCH,true,127,254),"manual start after recovery works");
        sample(m,0,8,255);sounding(frame(m,255),60,"post-recovery manual strike");
    }
    private static void sampleGap(){
        MotionInstrument m=armed();sample(m,0,8,251);MotionInstrument.Frame f=frame(m,251);
        check(f.fresh,"arriving new sample is fresh");stopped(f,"a sensor gap cannot be hidden by the next arriving callback");
    }
    private static void receivedAge(){
        MotionInstrument m=new MotionInstrument();m.sample(0,0,0,0,250);check(m.calibrate(250),"sample received at age 250ms is accepted");
        MotionInstrument old=new MotionInstrument();old.sample(0,0,0,0,251);check(!frame(old,251).fresh,"sample first seen at age 251ms is rejected");
    }
    private static void epoch(){
        MotionInstrument m=armed();sample(m,0,8,1);MotionInstrument.Frame f=m.frame(2,EPOCH+1,true,127);
        stopped(f,"epoch change");check(f.fresh&&f.calibrated,"route change retains sensor center");
        sample(m,0,0,3);sample(m,0,8,4);stopped(m.frame(4,EPOCH+1,true,127),"epoch change needs manual restart");
        sample(m,0,0,301);check(m.arm(EPOCH+1,true,127,301),"explicit start can bind the new epoch");
        sample(m,0,8,302);sounding(m.frame(302,EPOCH+1,true,127),60,"new epoch strike");
    }
    private static void eligibility(){
        MotionInstrument m=armed();sample(m,0,8,1);stopped(m.frame(2,EPOCH,false,127),"loss of performance eligibility");
        sample(m,0,0,3);m.frame(3,EPOCH,true,127);sample(m,0,8,4);stopped(frame(m,4),"eligibility recovery cannot restart itself");
    }
    private static void invalidNumbers(){
        double[][] invalid={{Double.NaN,0,0},{Double.POSITIVE_INFINITY,0,0},{Double.NEGATIVE_INFINITY,0,0},
            {0,Double.NaN,0},{0,Double.POSITIVE_INFINITY,0},{0,Double.NEGATIVE_INFINITY,0},
            {0,0,Double.NaN},{0,0,Double.POSITIVE_INFINITY},{0,0,Double.NEGATIVE_INFINITY},{0,0,-.001}};
        for(double[] v:invalid){MotionInstrument m=armed();sample(m,0,8,1);m.sample(v[0],v[1],v[2],2,2);
            MotionInstrument.Frame f=frame(m,2);stopped(f,"invalid sensor number");check(!f.fresh&&f.calibrated,"invalid data clears freshness and retains center");
            sample(m,0,0,3);sample(m,0,8,4);stopped(frame(m,4),"valid data cannot auto-rearm an invalidated source");
            check(Double.isFinite(f.roll)&&Double.isFinite(f.pitch)&&Double.isFinite(f.force),"invalid numbers never leak into display frame");
        }
    }
    private static void invalidTimestamp(){
        MotionInstrument m=armed();sample(m,0,8,1);m.sample(0,0,0,3,2);MotionInstrument.Frame f=frame(m,2);
        stopped(f,"future sample timestamp");check(!f.fresh,"future timestamp cannot refresh data");
        MotionInstrument old=armed();old.sample(0,0,8,1,252);f=frame(old,252);
        stopped(f,"new but already too old sample");check(!f.fresh,"old arrival is invalid");
        MotionInstrument negative=armed();negative.sample(0,0,8,-1,1);stopped(frame(negative,1),"negative sample timestamp");
    }
    private static void outOfOrder(){
        MotionInstrument m=armed();sample(m,0,8,100);m.sample(35,9,0,99,110);MotionInstrument.Frame f=frame(m,110);
        sounding(f,60,"an ignored older sample cannot replace the current strike");eq(0,(int)f.pitch,"older orientation is not adopted");
        check(f.force==8,"older acceleration is not adopted");m.sample(35,9,0,100,120);f=frame(m,120);
        check(f.force==8&&f.pitch==0,"duplicate timestamp is also ignored");
        f=frame(m,350);check(f.fresh&&f.armed,"freshness measures the last accepted sample");
        f=frame(m,351);check(!f.fresh,"ignored samples cannot extend freshness");stopped(f,"freshness after ignored samples");
    }
    private static void clockRollback(){
        MotionInstrument m=armed();sample(m,0,8,10);MotionInstrument.Frame f=frame(m,9);
        stopped(f,"frame clock rollback");check(!f.fresh&&f.calibrated,"rollback invalidates freshness but retains center");
        sample(m,0,0,11);sample(m,0,8,12);stopped(frame(m,12),"clock recovery cannot restart itself");
        MotionInstrument sampleRollback=armed();sample(sampleRollback,0,8,10);sample(sampleRollback,0,0,9);
        stopped(frame(sampleRollback,10),"sample clock rollback");
        MotionInstrument armRollback=armed();frame(armRollback,10);check(!armRollback.arm(EPOCH,true,127,9),"backward arm clock is rejected");
        MotionInstrument calibrationRollback=armed();frame(calibrationRollback,10);check(!calibrationRollback.calibrate(9),"backward calibration clock is rejected");
        MotionInstrument negative=armed();stopped(frame(negative,-1),"negative monotonic time");
    }
    private static void explicitStop(){
        MotionInstrument m=armed();sample(m,0,8,1);m.stop("已停止");MotionInstrument.Frame f=frame(m,2);
        stopped(f,"explicit stop");check(f.calibrated&&f.fresh,"stop preserves center and current sample");
        check("已停止".equals(f.status),"stop reason is readable");sample(m,0,0,3);sample(m,0,8,4);
        stopped(frame(m,4),"fresh callbacks cannot undo explicit stop");
        sample(m,0,0,5);check(m.arm(EPOCH,true,127,5),"manual restart is allowed while quiet");sample(m,0,8,6);
        eq(0,frame(m,6).mask,"stop and restart cannot bypass the 300ms attack interval");
    }
    private static void reset(){
        MotionInstrument m=armed();sample(m,0,8,10);m.reset("请校准");MotionInstrument.Frame f=frame(m,0);
        stopped(f,"reset");check(!f.calibrated&&!f.fresh,"reset removes both calibration and source freshness");
        check(!m.arm(EPOCH,true,127,0),"reset source cannot start before calibration");
        sample(m,12,0,1);check(m.calibrate(1),"reset allows a new sensor time base and center");
        check(m.arm(EPOCH,true,127,1),"new calibration can start");sample(m,12,8,2);sounding(frame(m,2),60,"new center after reset");
    }
    private static void immutableFrame(){
        MotionInstrument m=armed();sample(m,0,8,1);MotionInstrument.Frame before=frame(m,1);m.stop(null);
        sounding(before,60,"previous immutable frame");stopped(frame(m,2),"new stopped frame");
    }
    public static void main(String[] args){
        test("initially silent",MotionInstrumentTest::initial);
        test("quiet relative calibration",MotionInstrumentTest::calibration);
        test("calibration guards",MotionInstrumentTest::calibrationGuards);
        test("explicit start guards",MotionInstrumentTest::manualStart);
        test("attack threshold",MotionInstrumentTest::threshold);
        test("strict low rearm gate",MotionInstrumentTest::lowGate);
        test("sustained force is one attack",MotionInstrumentTest::sustainedHigh);
        test("300ms attack spacing",MotionInstrumentTest::refractory);
        test("180ms scheduled NoteOff",MotionInstrumentTest::timedRelease);
        test("one strike locks one note",MotionInstrumentTest::lockedNote);
        test("boundary hysteresis",MotionInstrumentTest::hysteresis);
        test("single-sample smoothing",MotionInstrumentTest::smoothing);
        test("15-note scale and 25-key masks",MotionInstrumentTest::scale);
        test("calibration across angle wrap",MotionInstrumentTest::rollWrap);
        test("force velocity and ceiling",MotionInstrumentTest::velocity);
        test("250ms freshness and manual restart",MotionInstrumentTest::freshness);
        test("arrival cannot hide a sample gap",MotionInstrumentTest::sampleGap);
        test("sample age on receipt",MotionInstrumentTest::receivedAge);
        test("epoch cancellation",MotionInstrumentTest::epoch);
        test("eligibility cancellation",MotionInstrumentTest::eligibility);
        test("invalid sensor numbers",MotionInstrumentTest::invalidNumbers);
        test("invalid sample timestamps",MotionInstrumentTest::invalidTimestamp);
        test("old samples do not refresh source",MotionInstrumentTest::outOfOrder);
        test("clock rollback cancellation",MotionInstrumentTest::clockRollback);
        test("manual stop preserves center",MotionInstrumentTest::explicitStop);
        test("reset requires new calibration",MotionInstrumentTest::reset);
        test("frames are value snapshots",MotionInstrumentTest::immutableFrame);
        System.out.println("Motion instrument: "+checks+" assertions, "+cases+" cases, "+failures+" failures");
        if(failures!=0)throw new AssertionError("Motion instrument failures: "+failures);
    }
}
