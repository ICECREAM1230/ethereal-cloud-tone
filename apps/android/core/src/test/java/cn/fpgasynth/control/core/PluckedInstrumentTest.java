package cn.fpgasynth.control.core;

import java.util.ArrayList;
import java.util.List;

/** Deterministic time and observed output transitions; no radio or Android mocks. */
public final class PluckedInstrumentTest {
    private static int checks;
    private static void check(boolean value,String why){checks++;if(!value)throw new AssertionError(why);}
    private static void eq(long want,long got,String why){check(want==got,why+" want "+want+" got "+got);}
    private static final class Capture implements PluckedInstrument.Output {
        final List<Integer> masks=new ArrayList<>();
        final List<Long> times=new ArrayList<>();
        boolean accepted=true;
        @Override public boolean keys(int mask,long now){masks.add(mask);times.add(now);return accepted;}
    }
    private static int bit(int note){return 1<<(note-48);}
    private static PluckedInstrument ready(Capture c){PluckedInstrument p=new PluckedInstrument(c);p.state(true,9);return p;}
    private static void mapping(){
        int[][] notes={{67,64,60,55,52,48},{72,69,64,60,57,52},{69,65,62,57,53,50},
                       {69,65,60,57,53,48},{71,67,62,59,55,50},{71,67,64,59,55,52}};
        String[][] names={{"G4","E4","C4","G3","E3","C3"},{"C5","A4","E4","C4","A3","E3"},
                {"A4","F4","D4","A3","F3","D3"},{"A4","F4","C4","A3","F3","C3"},
                {"B4","G4","D4","B3","G3","D3"},{"B4","G4","E4","B3","G3","E3"}};
        String[] chords={"C","Am","Dm","F","G","Em"};
        for(int col=0;col<6;col++){
            check(chords[col].equals(PluckedInstrument.chord(col)),"accepted chord heading "+col);
            for(int row=0;row<6;row++){
                eq(notes[col][row],PluckedInstrument.note(col,row),"accepted high-to-low voicing");
                check(names[col][row].equals(PluckedInstrument.name(col,row)),"exact displayed note name");
            }
        }
    }
    private static void pulses(){
        Capture c=new Capture();PluckedInstrument p=ready(c);
        p.tick(0);eq(0,c.masks.size(),"idle never sends traffic");
        p.pluck(0,0,10,10);eq(bit(67),p.mask(),"manual note enters exact MIDI mask");
        eq(610,p.nextAt(),"bounded 600ms gate schedules its release");
        p.tick(609);eq(1,c.masks.size(),"no idle duplicate writes");
        p.pluck(0,0,609,609);eq(3,c.masks.size(),"same-note repluck emits distinct off then on");
        eq(0,c.masks.get(1),"repluck removes only its note first");
        eq(bit(67),c.masks.get(2),"repluck starts a fresh note");
        p.pluck(0,1,610,610);eq(bit(67)|bit(64),p.mask(),"independent ringing gates coexist");
        p.pluck(0,0,611,611);eq(bit(64),c.masks.get(4),"repluck preserves another ringing note");
        p.tick(1210);eq(bit(67),p.mask(),"individual gate expiry preserves later repluck");
        p.tick(1211);eq(0,p.mask(),"last ringing gate expires");
        eq(-1,p.nextAt(),"idle needs no further callback");
    }
    private static void strums(){
        for(boolean up:new boolean[]{false,true}){
            Capture c=new Capture();PluckedInstrument p=ready(c);p.interval(40);
            p.pluck(4,2,0,0);p.strum(up,10);
            eq(0,c.masks.get(1),"chord head cancels old own note before sweep");
            int cumulative=0;
            for(int i=0;i<6;i++){
                int row=up?5-i:i;cumulative|=bit(PluckedInstrument.note(4,row));
                p.tick(10+40*i);eq(cumulative,p.mask(),"up/down follows selected column order "+i);
                eq(10+40*i,c.times.get(c.times.size()-1),"each sweep note is sent at its actual due time");
            }
            p.tick(810);eq(0,p.mask(),"all sweep gates expire");
        }
        Capture c=new Capture();PluckedInstrument p=ready(c);p.interval(160);p.strum(false,0);
        p.pluck(1,2,20,20);int count=c.masks.size();p.tick(200);
        eq(count,c.masks.size(),"manual cancels only unrun sweep notes");
        eq(bit(67)|bit(64),p.mask(),"manual preserves valid ringing sweep note");
        boolean low=false,high=false;
        try{p.interval(19);}catch(IllegalArgumentException expected){low=true;}
        try{p.interval(161);}catch(IllegalArgumentException expected){high=true;}
        check(low&&high,"20..160ms spacing bounds enforced");
    }
    private static void cancellation(){
        Capture c=new Capture();PluckedInstrument p=ready(c);p.strum(false,0);int sent=c.masks.size();
        p.clear();p.tick(500);eq(sent,c.masks.size(),"clear has no callback or late replay");eq(0,p.mask(),"clear removes local intent");
        p.strum(false,600);sent=c.masks.size();p.state(true,10);p.tick(700);
        eq(sent,c.masks.size(),"epoch change discards queued sweep callback-free");eq(0,p.mask(),"epoch change clears active intent");
        p.strum(false,800);sent=c.masks.size();p.state(false,10);p.tick(900);p.pluck(1,0,900,900);
        eq(sent,c.masks.size(),"disabled model cannot produce or replay notes");
        p.state(true,10);p.pluck(1,0,901,901);check(p.mask()!=0,"fresh re-enable accepts a fresh action");
        p.release(902);eq(0,p.mask(),"autonomous release clears own source");eq(0,c.masks.get(c.masks.size()-1),"autonomous release sends zero");
    }
    private static void lateness(){
        Capture c=new Capture();PluckedInstrument p=ready(c);p.interval(40);p.strum(false,0);
        p.tick(400);eq(1,c.masks.size(),"300ms overdue starts are discarded without catch-up burst");
        p.tick(600);eq(0,p.mask(),"discarded sweep cannot prevent gate release");
        p.pluck(2,0,0,601);eq(0,p.mask(),"old MotionEvent sample cannot become delayed NoteOn");
        p.pluck(2,0,900,700);eq(0,p.mask(),"future event rejected");
        p.pluck(2,0,701,701);check(p.mask()!=0,"fresh sample accepted after stale input");
        p.tick(1802);eq(0,p.mask(),"one second UI gap releases old intent");eq(-1,p.nextAt(),"gap has no replayable sweep");
        p.strum(false,1900);p.tick(1899);eq(0,p.mask(),"backward clock discards local intent");
        p.tick(2200);eq(0,p.mask(),"backward clock cannot replay pending starts");
    }
    private static void failures(){
        Capture c=new Capture();PluckedInstrument p=ready(c);c.accepted=false;p.strum(false,0);
        eq(0,p.mask(),"failed write immediately clears local intent");eq(-1,p.nextAt(),"failed write stops every scheduled callback");
        p.tick(100);p.pluck(1,1,101,101);p.state(true,9);p.strum(true,102);
        eq(1,c.masks.size(),"failure never retries in the same input generation");
        c.accepted=true;p.state(true,10);p.pluck(1,1,103,103);eq(bit(69),p.mask(),"new generation accepts a new explicit action");
        c.accepted=false;p.pluck(1,1,104,104);eq(0,p.mask(),"failed repluck-off cannot proceed to on");
        int count=c.masks.size();p.tick(1000);eq(count,c.masks.size(),"failure during off has no stale continuation");
    }
    private static void interruption(){
        final PluckedInstrument[] holder=new PluckedInstrument[1];final List<Integer> masks=new ArrayList<>();
        holder[0]=new PluckedInstrument(new PluckedInstrument.Output(){
            @Override public boolean keys(int mask,long now){masks.add(mask);if(mask==0)holder[0].clear();return true;}
        });
        PluckedInstrument p=holder[0];p.state(true,1);p.pluck(0,0,0,0);p.strum(false,10);
        eq(2,masks.size(),"clear during a successful release callback stops the rest of that action");
        eq(0,p.mask(),"reentrant central clear cannot be followed by an old NoteOn");
        eq(-1,p.nextAt(),"reentrant central clear leaves no scheduled sweep");
    }
    public static void main(String[] args){mapping();pulses();strums();cancellation();lateness();failures();interruption();System.out.println("Plucked instrument: "+checks+" assertions PASS");}
}
