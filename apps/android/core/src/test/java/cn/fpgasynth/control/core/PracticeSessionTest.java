package cn.fpgasynth.control.core;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** Real timing model, deterministic monotonic milliseconds, no Android or MIDI doubles. */
public final class PracticeSessionTest {
    private static int checks;
    private static void check(boolean value,String why){checks++;if(!value)throw new AssertionError(why);}
    private static void eq(long want,long got,String why){check(want==got,why+" want "+want+" got "+got);}
    private static void phase(PracticeSession.Phase want,PracticeSession.Scene got,String why){check(want==got.phase,why+" got "+got.phase);}
    private static void bad(Runnable operation,String why){boolean rejected=false;try{operation.run();}catch(IllegalArgumentException expected){rejected=true;}check(rejected,why);}
    private static void blocked(Runnable operation,String why){boolean rejected=false;try{operation.run();}catch(IllegalStateException expected){rejected=true;}check(rejected,why);}
    private static int bit(int midi){return 1<<(midi-48);}
    private static PracticeSong sequence(int... midi){
        List<PracticeSong.Note> notes=new ArrayList<>();
        for(int i=0;i<midi.length;i++)notes.add(new PracticeSong.Note(midi[i],i*480,480));
        return new PracticeSong("test","测试练习",120,4,notes);
    }
    private static PracticeSession ready(PracticeSong song){PracticeSession p=new PracticeSession();p.select(song);return p;}
    private static void songs(){
        List<PracticeSong> all=PracticeSong.builtIns();check(all.size()>=3,"three original practice patterns are bundled");
        Set<String> ids=new HashSet<>();boolean eighth=false,rest=false,dotted=false,longNote=false;
        for(PracticeSong song:all){
            check(ids.add(song.id),"song IDs are unique");check(song.title.length()>0,"visible song title");
            check(song.defaultBpm>=40&&song.defaultBpm<=180,"valid default tempo");
            eq(0,song.totalTicks%(480*song.beatsPerBar),"built-in songs contain complete bars");
            int position=0;
            for(PracticeSong.Note note:song.notes){
                eq(position,note.startTick,"events have an explicit contiguous time axis");position+=note.durationTicks;
                check(note.midi==-1||(note.midi>=48&&note.midi<=72),"hardware surface note range");
                if(note.midi>=0)check(Arrays.asList(0,2,4,5,7,9,11).contains(note.midi%12),"bundled exercise uses C major");
                eighth|=note.durationTicks==240;rest|=note.midi==-1;dotted|=note.durationTicks==720;longNote|=note.durationTicks>=960;
                check(!PracticeSong.degree(note.midi).isEmpty(),"each event has a numbered notation label");
            }
            eq(position,song.totalTicks,"total ticks are derived from all events");
            boolean immutable=false;try{song.notes.clear();}catch(UnsupportedOperationException expected){immutable=true;}
            check(immutable,"song does not expose mutable note list");
        }
        check(eighth&&rest&&dotted&&longNote,"eighth, rest, dotted and long-note exercises available");
        boolean immutable=false;try{all.clear();}catch(UnsupportedOperationException expected){immutable=true;}
        check(immutable,"built-in catalog is immutable");
        check("1".equals(PracticeSong.degree(60)),"C4 is the unmarked reference tonic");
        check(!PracticeSong.degree(48).equals(PracticeSong.degree(60)),"low octave has its own dot");
        check(!PracticeSong.degree(72).equals(PracticeSong.degree(60)),"high octave has its own dot");
        check("0".equals(PracticeSong.degree(-1)),"rest is numbered zero");
        List<PracticeSong.Note> source=new ArrayList<>();source.add(new PracticeSong.Note(48,0,480));
        PracticeSong copied=new PracticeSong("copy","复制输入",90,4,source);source.clear();eq(1,copied.notes.size(),"constructor makes a defensive list copy");
    }
    private static void validation(){
        bad(()->new PracticeSong.Note(47,0,480),"note below range rejected");
        bad(()->new PracticeSong.Note(73,0,480),"note above range rejected");
        bad(()->new PracticeSong.Note(-2,0,480),"only minus one is a rest");
        bad(()->new PracticeSong.Note(48,-1,480),"negative onset rejected");
        bad(()->new PracticeSong.Note(48,0,0),"empty duration rejected");
        bad(()->new PracticeSong.Note(48,Integer.MAX_VALUE,1),"overflowing event endpoint rejected");
        List<PracticeSong.Note> one=Arrays.asList(new PracticeSong.Note(48,0,480));
        bad(()->new PracticeSong(null,"x",80,4,one),"missing ID rejected");
        bad(()->new PracticeSong("x","",80,4,one),"empty title rejected");
        bad(()->new PracticeSong("x","x",39,4,one),"low song tempo rejected");
        bad(()->new PracticeSong("x","x",181,4,one),"high song tempo rejected");
        bad(()->new PracticeSong("x","x",80,0,one),"invalid meter rejected");
        bad(()->new PracticeSong("x","x",80,4,new ArrayList<PracticeSong.Note>()),"empty exercise rejected");
        bad(()->new PracticeSong("x","x",80,4,Arrays.asList(new PracticeSong.Note(48,480,480))),"implicit leading gap rejected");
        bad(()->new PracticeSong("x","x",80,4,Arrays.asList(one.get(0),new PracticeSong.Note(50,240,480))),"overlap rejected");
        bad(()->new PracticeSong("x","x",80,4,Arrays.asList(one.get(0),new PracticeSong.Note(50,960,480))),"implicit rest gap rejected");
        bad(()->PracticeSong.degree(47),"invalid notation input rejected");
        PracticeSession p=ready(sequence(48,50));
        bad(()->p.select(null),"missing selection rejected");bad(()->p.tempo(39),"low UI tempo rejected");bad(()->p.tempo(181),"high UI tempo rejected");
        bad(()->p.start(-1),"negative clock start rejected");bad(()->p.input(-1,0),"negative mask rejected");bad(()->p.input(1<<25,0),"out-of-range mask rejected");
        p.tempo(40);eq(40,p.bpm(),"lower tempo inclusive");p.tempo(180);eq(180,p.bpm(),"upper tempo inclusive");
        p.start(0);blocked(()->p.tempo(120),"tempo cannot jump during count-in");p.pause(1);blocked(()->p.tempo(120),"paused timeline cannot silently change scale");
        p.stop();p.tempo(120);eq(120,p.bpm(),"stop permits a new tempo");
        p.start(2);p.select(sequence(52));phase(PracticeSession.Phase.STOPPED,p.scene(3),"selection safely stops old exercise");eq(120,p.bpm(),"selection adopts song default tempo");
    }
    private static void timing(){
        PracticeSession p=ready(sequence(48,50,52,53));phase(PracticeSession.Phase.STOPPED,p.scene(0),"initially stopped");
        eq(0,p.scene(0).guideMask,"stopped has no target");eq(0,p.scene(0).nextMask,"stopped has no pending target");
        p.start(100);PracticeSession.Scene s=p.scene(100);phase(PracticeSession.Phase.COUNT_IN,s,"one bar count-in");
        eq(-2000,s.positionMs,"negative position identifies count-in");eq(1,s.beat,"count-in starts on beat one");eq(bit(48),s.nextMask,"count-in identifies first upcoming note");eq(0,s.guideMask,"count-in does not light a sounding target");check(s.running,"count-in ticks only while active");
        s=p.scene(1599);eq(3,s.beat,"count-in third beat");check(s.beatFraction>.99,"beat fraction uses actual clock");
        s=p.scene(1600);eq(4,s.beat,"count-in fourth beat");s=p.scene(2099);phase(PracticeSession.Phase.COUNT_IN,s,"no premature start");
        s=p.scene(2100);phase(PracticeSession.Phase.PLAYING,s,"song starts at exact first onset");eq(0,s.positionMs,"first note position is zero");eq(bit(48),s.guideMask,"first target");eq(bit(50),s.nextMask,"second note preview");eq(0,s.currentIndex,"current event index");
        eq(bit(48),p.scene(2599).guideMask,"first target held for complete duration");s=p.scene(2600);eq(bit(50),s.guideMask,"event boundary moves once");eq(2,s.beat,"song beat advances");
        s=p.scene(4100);phase(PracticeSession.Phase.FINISHED,s,"end clears all prompts");eq(0,s.guideMask,"finished target clear");eq(0,s.nextMask,"finished preview clear");check(!s.running,"finished stops scheduling");eq(4,s.misses,"unplayed onsets counted once");
        eq(4,p.scene(10000).misses,"finished state never duplicates misses");
        for(int bpm:new int[]{40,60,73,120,179,180}){
            PracticeSession q=ready(sequence(48,50,52,53));q.tempo(bpm);long base=3_000_000_000_000L;q.start(base);
            for(int i=0;i<4;i++){
                long onset=(240000L+60000L*i+bpm-1)/bpm;
                PracticeSession.Scene before=q.scene(base+onset-1);
                if(i==0)phase(PracticeSession.Phase.COUNT_IN,before,"fractional count-in rounds at the clock boundary");
                else eq(bit(new int[]{48,50,52,53}[i-1]),before.guideMask,"no early note at fractional boundary");
                PracticeSession.Scene at=q.scene(base+onset);eq(bit(new int[]{48,50,52,53}[i]),at.guideMask,"absolute timeline at BPM "+bpm+" onset "+i);eq(i+1,at.beat,"beat and note agree without cumulative drift");
            }
            long end=(480000L+bpm-1)/bpm;phase(PracticeSession.Phase.FINISHED,q.scene(base+end),"fractional complete duration at BPM "+bpm);
        }
    }
    private static void restsAndFrames(){
        PracticeSong song=new PracticeSong("rests","休止与长音",120,4,Arrays.asList(new PracticeSong.Note(48,0,480),new PracticeSong.Note(-1,480,480),new PracticeSong.Note(52,960,960)));
        PracticeSession p=ready(song);p.start(0);PracticeSession.Scene s=p.scene(2000);eq(bit(52),s.nextMask,"preview skips rest to next playable pitch");
        s=p.scene(2500);eq(1,s.currentIndex,"rest retains score event index");eq(0,s.guideMask,"rest never lights a note");eq(bit(52),s.nextMask,"rest shows next actual pitch");
        s=p.scene(3700);eq(2,s.currentIndex,"late frame jumps to current long note");eq(bit(52),s.guideMask,"late frame does not replay skipped first target");eq(0,s.nextMask,"last target has no future note");eq(2,s.misses,"rests do not count as missed notes");
        eq(2,p.scene(3999).misses,"one miss per sounded event");phase(PracticeSession.Phase.FINISHED,p.scene(Long.MAX_VALUE),"huge forward clock safely finishes without multiplication overflow");
    }
    private static void matching(){
        PracticeSession p=ready(sequence(48,48,50,52));p.start(0);p.input(bit(48),2000);PracticeSession.Scene s=p.scene(2000);eq(1,s.hits,"on-time first note");eq(0,s.lastErrorMs,"zero timing error");
        p.input(bit(48),2050);eq(1,p.scene(2050).hits,"held mask is not another onset");p.input(0,2250);p.input(bit(48),2500);eq(2,p.scene(2500).hits,"release and same-pitch repluck matches next event");
        p.input(bit(48),2600);eq(2,p.scene(2600).hits,"same event cannot be counted twice");p.input(0,2601);p.input(bit(49),2920);eq(1,p.scene(2920).wrong,"wrong pitch is counted independently");
        p.input(0,2921);p.input(bit(50),3000);eq(3,p.scene(3000).hits,"wrong pitch does not consume correct future onset");p.input(0,3001);p.input(bit(52),3200);eq(2,p.scene(3200).wrong,"too-early pitch is not an accepted hit");
        p.input(0,3300);p.input(bit(52),3680);s=p.scene(3680);eq(4,s.hits,"positive 180ms window inclusive");eq(180,s.lastErrorMs,"late timing error retains sign");eq(0,s.misses,"hit at inclusive edge is not missed");
        phase(PracticeSession.Phase.FINISHED,p.scene(4000),"graded exercise completes");
        PracticeSession early=ready(sequence(48));early.start(0);early.input(bit(48),1820);s=early.scene(1820);eq(1,s.hits,"negative 180ms window inclusive");eq(-180,s.lastErrorMs,"early timing error retains sign");
        PracticeSession pre=ready(sequence(48));pre.start(0);pre.input(bit(48),1000);eq(0,pre.scene(1000).wrong,"early count-in contacts are not scored wrong");pre.input(bit(48),2000);eq(0,pre.scene(2000).hits,"count-in held note does not become a new onset");
        PracticeSession late=ready(sequence(48));late.start(0);eq(0,late.scene(2180).misses,"inclusive scoring window has not expired");eq(1,late.scene(2181).misses,"window closes at 181ms");late.input(bit(48),2182);s=late.scene(2182);eq(0,s.hits,"late pitch cannot resurrect missed event");eq(1,s.wrong,"late new contact reports unmatched pitch");eq(1,s.misses,"late contact does not duplicate missed event");
        PracticeSession chord=ready(sequence(48));chord.start(0);chord.input(bit(48)|bit(50),2000);s=chord.scene(2000);eq(1,s.hits,"correct pitch in multi-touch input is retained");eq(1,s.wrong,"extra simultaneous pitch is not silently ignored");
    }
    private static void pauseAndStop(){
        PracticeSession p=ready(sequence(48,50,52,53));p.start(0);p.pause(1000);PracticeSession.Scene s=p.scene(100000);phase(PracticeSession.Phase.PAUSED,s,"paused count-in does not expire");eq(-1000,s.positionMs,"pause freezes negative timeline");eq(0,s.guideMask,"paused target clear");eq(0,s.nextMask,"paused upcoming prompt clear");check(!s.running,"paused needs no clock callback");eq(0,s.misses,"paused time does not create misses");
        p.resume(100000);s=p.scene(101000);phase(PracticeSession.Phase.PLAYING,s,"resume continues remaining count-in");eq(0,s.positionMs,"resume has no wall-clock jump");p.input(bit(48),101000);p.pause(101250);s=p.scene(102000);eq(250,s.positionMs,"playing pause preserves fractional note position");eq(1,s.hits,"pause preserves scores");
        p.input(bit(50),102001);eq(1,p.scene(102001).hits,"paused touches are not graded");p.resume(200000);eq(bit(48),p.scene(200249).guideMask,"resume preserves remainder of long-held target");eq(bit(50),p.scene(200250).guideMask,"resumed note boundary is shifted by paused duration");
        p.stop();s=p.scene(200251);phase(PracticeSession.Phase.STOPPED,s,"explicit stop");eq(0,s.guideMask,"stop clears target");eq(0,s.nextMask,"stop clears all future prompt");eq(0,s.hits,"stop resets scores");check(s.lastErrorMs==null,"stop clears stale timing result");
        p.resume(200252);phase(PracticeSession.Phase.STOPPED,p.scene(200252),"resume cannot restart a stopped exercise");p.start(200253);phase(PracticeSession.Phase.COUNT_IN,p.scene(200253),"restart is explicit and starts with count-in");
    }
    private static void shortFinalWindow(){
        PracticeSong song=new PracticeSong("short-final","短末音",180,4,Arrays.asList(new PracticeSong.Note(48,0,240)));
        PracticeSession p=ready(song);p.start(0);PracticeSession.Scene s=p.scene(1500);
        eq(0,s.guideMask,"short final note ends visually on its own duration");eq(0,s.nextMask,"scoring tail must not preview the first note again");
        check(s.running,"short final onset retains the promised late-input window");
        p.input(bit(48),1513);s=p.scene(1513);eq(1,s.hits,"last eighth accepts +179.7ms hit");eq(180,s.lastErrorMs,"fractional final-note timing rounded for display");
        phase(PracticeSession.Phase.FINISHED,p.scene(1514),"practice finishes when final scoring window expires");eq(0,p.scene(1514).misses,"late but valid final note is not missed");
        PracticeSession missed=ready(song);missed.start(0);s=missed.scene(1514);phase(PracticeSession.Phase.FINISHED,s,"short unplayed final note reaches completed state");eq(1,s.misses,"short final note cannot disappear without a hit or miss");
        PracticeSession repeat=ready(sequence(48));repeat.start(0);repeat.input(bit(48),2000);repeat.input(0,2001);repeat.input(bit(48),2002);s=repeat.scene(2002);
        eq(1,s.hits,"repressed pitch cannot match one score event twice");eq(1,s.wrong,"unmatched same-pitch repress is reported");
    }
    private static void clockSafety(){
        PracticeSession p=ready(sequence(48,50));p.start(1000);eq(bit(48),p.scene(3000).guideMask,"valid target before clock discontinuity");PracticeSession.Scene s=p.scene(2999);phase(PracticeSession.Phase.STOPPED,s,"clock rollback stops schedule");eq(0,s.guideMask,"rollback removes target");eq(0,s.nextMask,"rollback removes upcoming target");
        p.input(bit(48),4000);phase(PracticeSession.Phase.STOPPED,p.scene(4000),"old schedule cannot restart after rollback");eq(0,p.scene(5000).hits,"stopped input is not graded");p.start(6000);phase(PracticeSession.Phase.COUNT_IN,p.scene(6000),"new explicit start recovers");
        s=p.scene(-1);phase(PracticeSession.Phase.STOPPED,s,"negative sample clock fails closed");eq(0,s.guideMask,"negative clock clears target");
        p.start(Long.MAX_VALUE-4000);eq(bit(48),p.scene(Long.MAX_VALUE-2000).guideMask,"long uptime uses subtraction rather than absolute multiplication");phase(PracticeSession.Phase.FINISHED,p.scene(Long.MAX_VALUE),"near maximum uptime finishes without overflow");
    }
    private static void heldAcrossPause(){
        PracticeSession p=ready(sequence(48,48));p.tempo(60);p.start(0);p.input(1,4000);eq(1,p.scene(4000).hits,"first physical onset");p.pause(4500);p.resume(5000);p.input(1,5500);eq(1,p.scene(5500).hits,"unchanged held pitch after resume is not a new onset");p.input(0,5501);p.input(1,5502);eq(2,p.scene(5502).hits,"physical release and repress can hit second note");
        PracticeSession existing=ready(sequence(48));existing.start(0);existing.baseline(1);existing.input(1,2000);eq(0,existing.scene(2000).hits,"already-held key before manual start is not an onset");existing.input(0,2001);existing.input(1,2002);eq(1,existing.scene(2002).hits,"new contact after baseline release is graded");
        PracticeSession paused=ready(sequence(48));paused.start(0);paused.pause(1000);paused.input(1,1500);paused.resume(2000);paused.input(1,3000);eq(0,paused.scene(3000).hits,"contact pressed during pause remains held across resume");
    }
    public static void main(String[] args){songs();validation();timing();restsAndFrames();matching();pauseAndStop();shortFinalWindow();clockSafety();heldAcrossPause();System.out.println("Practice session: "+checks+" assertions PASS (no Android or transport classes)");}
}
