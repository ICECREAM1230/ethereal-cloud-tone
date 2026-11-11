package cn.fpgasynth.control.core;

/** Observable control gestures and honest feedback; no device or graphics runtime required. */
public final class ConsoleInteractionTest {
    private static int checks;
    private static void check(boolean value,String why){checks++;if(!value)throw new AssertionError(why);}
    private static void eq(Integer expected,Integer actual,String why){check(expected==null?actual==null:expected.equals(actual),why+" got "+actual);}
    private static void gestures(){
        ControlGesture g=new ControlGesture();g.begin(64,3,0);
        eq(null,g.end(3,2,true),"touching without changing must not send defaults");
        g.begin(64,3,10);eq(65,g.move(65,3,10,true),"first actual change is immediate");
        eq(null,g.move(68,3,20,true),"fast intermediate movement coalesced");
        eq(null,g.move(72,3,59,true),"50ms throttle measured from actual send");
        eq(75,g.move(75,3,60,true),"latest value after 50ms emitted");
        eq(null,g.move(79,3,70,true),"trailing movement coalesced");
        eq(79,g.end(3,72,true),"release always commits trailing value before throttle expires");
        eq(null,g.end(3,73,true),"release is idempotent");
        g.begin(64,3,100);eq(64,g.move(64,3,101,true),"explicit change callback can request current device value");
        eq(null,g.move(64,3,200,true),"same requested value not spammed");eq(null,g.end(3,201,true),"already-sent final value not duplicated");
        g.begin(70,3,300);g.move(80,3,301,true);eq(null,g.move(90,4,400,true),"new input generation cancels old gesture");
        eq(null,g.end(4,500,true),"old gesture cannot replay on new epoch");
        g.begin(70,4,600);g.move(80,4,601,true);eq(null,g.end(4,610,false),"loss of edit permission discards trailing value");
        g.begin(70,4,700);g.move(80,4,701,true);g.cancel();eq(null,g.end(4,800,true),"cancel does not commit a stale tail");
        g.begin(70,4,900);g.move(80,4,901,true);eq(null,g.move(90,4,899,true),"backward time invalidates gesture");
        boolean bad=false;try{g.begin(128,1,0);}catch(IllegalArgumentException expected){bad=true;}check(bad,"invalid MIDI control values rejected");
    }
    private static void feedback(){
        PerformanceFeedback f=new PerformanceFeedback();
        f.update(1,true,false,0,0,100,0);PerformanceFeedback.Scene idle=f.snapshot(0);
        check(!idle.audioAvailable&&idle.localCount==0&&idle.adoptedCount==0,"connected idle has no invented audio signal");
        f.update(1,true,false,17,1,96,10);PerformanceFeedback.Scene s=f.snapshot(10);
        check(s.localCount==2&&s.adoptedCount==1&&s.pendingOn==16&&s.pendingOff==0,"local and owner adoption remain separate");
        check(s.localNotes.equals("C3  E3")&&s.adoptedNotes.equals("C3")&&s.velocity==96,"note names and actual controller velocity");
        f.update(1,true,false,0,17,96,20);s=f.snapshot(20);check(s.pendingOff==17&&s.pendingOn==0,"release can wait for old owner acknowledgement");
        f.update(2,false,false,0,17,96,30);s=f.snapshot(30);check(s.adoptedCount==0&&s.pendingOff==0,"disabled or stale state never lights adopted notes");
        check(!f.audio(1,30,.2,.2,.4,.4),"old route cannot inject audio telemetry");
        f.update(3,true,false,1,1,100,40);check(f.audio(3,40,.2,.3,.5,.6),"explicit valid device frame accepted");
        s=f.snapshot(340);check(s.audioAvailable&&s.leftRms==.2&&s.rightPeak==.6,"fresh frame exposes actual supplied values");
        check(!f.snapshot(341).audioAvailable,"audio frame expires after 300ms instead of silently holding activity");
        check(!f.audio(3,39,.2,.2,.5,.5),"out-of-order audio frame rejected");
        check(!f.audio(3,400,Double.NaN,.2,.5,.5),"NaN cannot drive a meter");
        check(!f.audio(3,400,.6,.2,.5,.5),"RMS cannot exceed peak");
        check(!f.audio(3,400,.2,.2,1.1,.5),"normalized peaks bounded");
        f.update(4,true,true,1,1,127,500);check(!f.audio(4,500,.2,.2,.5,.5),"local demo cannot claim physical audio telemetry");
        check(!f.snapshot(500).audioAvailable&&f.snapshot(500).simulation,"demo remains labeled and has no fake VU");
        f.update(5,true,false,0,0,64,600);check(!f.snapshot(600).audioAvailable,"changing route clears prior audio frame");
        check(PerformanceFeedback.notes(1<<24).equals("C5"),"highest supported note labeled correctly");
        boolean bad=false;try{f.update(5,true,false,1<<25,0,100,600);}catch(IllegalArgumentException expected){bad=true;}check(bad,"feedback masks cannot extend the wire keyboard");
    }
    private static void envelope(){
        float[] shortA=EnvelopeShape.widths(254,120,120),longA=EnvelopeShape.widths(0,120,120);
        check(longA[0]>shortA[0],"lower raw attack rate must illustrate a longer attack");
        check(EnvelopeShape.widths(120,0,120)[1]>EnvelopeShape.widths(120,254,120)[1],"lower decay rate must illustrate a longer decay");
        check(EnvelopeShape.widths(120,120,0)[3]>EnvelopeShape.widths(120,120,254)[3],"lower release rate must illustrate a longer release");
        float total=0;for(float n:longA){check(n>0&&n<1,"relative widths remain within the diagram");total+=n;}check(Math.abs(total-1)<.00001,"relative diagram spans one normalized axis");
    }
    public static void main(String[] args){gestures();feedback();envelope();System.out.println("Console interaction: "+checks+" assertions PASS");}
}
