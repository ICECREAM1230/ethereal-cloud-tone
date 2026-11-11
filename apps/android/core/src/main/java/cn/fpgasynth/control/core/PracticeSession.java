package cn.fpgasynth.control.core;

/**
 * A visual practice clock and local onset evaluator. No MIDI, transport, audio,
 * scheduled callbacks or output interfaces exist in this module.
 */
public final class PracticeSession {
    public enum Phase {STOPPED,COUNT_IN,PLAYING,PAUSED,FINISHED}
    private static final int MASK=(1<<25)-1,WINDOW_MS=180;
    // 60,000 ms / PPQ480 is integral, so fractional BPM boundaries never drift.
    private static final long TICK_SCALE=60000/PracticeSong.PPQ;
    private PracticeSong song;
    private int bpm,held,hits,misses,wrong;
    private Phase phase=Phase.STOPPED;
    private long segmentAt,elapsedBase,lastNow=-1;
    private boolean[] hit,missed;
    private Integer lastErrorMs;
    private String lastResult="等待开始";

    public static final class Scene {
        public final Phase phase;
        public final int guideMask,nextMask,currentIndex,beat,bar,hits,misses,wrong;
        public final double beatFraction;
        public final long positionMs;
        public final Integer lastErrorMs;
        public final String lastResult;
        public final boolean running;
        private Scene(PracticeSession source,long scaled){
            phase=source.phase;running=phase==Phase.COUNT_IN||phase==Phase.PLAYING;
            hits=source.hits;misses=source.misses;wrong=source.wrong;
            lastErrorMs=source.lastErrorMs;lastResult=source.lastResult;
            positionMs=phase==Phase.STOPPED?0:Math.floorDiv(scaled,source.bpm);
            int current=-1;
            if(phase!=Phase.STOPPED&&scaled>=0&&scaled<source.endScaled()){
                for(int i=0;i<source.song.notes.size();i++){
                    PracticeSong.Note note=source.song.notes.get(i);
                    if(scaled>=note.startTick*TICK_SCALE&&scaled<(note.startTick+(long)note.durationTicks)*TICK_SCALE){current=i;break;}
                }
            }
            currentIndex=current;
            guideMask=phase==Phase.PLAYING&&current>=0?mask(source.song.notes.get(current).midi):0;
            int upcoming=0;
            if(running&&scaled<source.endScaled()){
                for(int i=current+1;i<source.song.notes.size();i++){
                    int midi=source.song.notes.get(i).midi;if(midi>=0){upcoming=mask(midi);break;}
                }
            }
            nextMask=upcoming;
            long display=phase==Phase.STOPPED?0:Math.min(scaled,source.endScaled()-1);
            if(display<0){
                long countPosition=display+source.countScaled();
                beat=(int)(Math.max(0,countPosition)/60000)%source.song.beatsPerBar+1;bar=1;
                beatFraction=Math.floorMod(countPosition,60000)/60000.0;
            }else{
                long ordinal=display/60000;
                beat=(int)(ordinal%source.song.beatsPerBar)+1;bar=(int)(ordinal/source.song.beatsPerBar)+1;
                beatFraction=display%60000/60000.0;
            }
        }
    }

    public PracticeSession(){select(PracticeSong.builtIns().get(0));}
    public synchronized void select(PracticeSong song){
        if(song==null)throw new IllegalArgumentException("Missing practice song");
        this.song=song;bpm=song.defaultBpm;hit=new boolean[song.notes.size()];missed=new boolean[song.notes.size()];stop();
    }
    public synchronized PracticeSong song(){return song;}
    public synchronized int bpm(){return bpm;}
    /** Tempo changes require stopping; a paused timeline keeps its original scale. */
    public synchronized void tempo(int bpm){
        if(bpm<40||bpm>180)throw new IllegalArgumentException("Practice tempo must be 40..180 BPM");
        if(phase!=Phase.STOPPED&&phase!=Phase.FINISHED)throw new IllegalStateException("Stop practice before changing tempo");
        this.bpm=bpm;reset("等待开始");
    }
    public synchronized void start(long now){
        if(now<0)throw new IllegalArgumentException("Invalid monotonic clock");
        reset("预备拍");segmentAt=now;lastNow=now;phase=Phase.COUNT_IN;
    }
    public synchronized void pause(long now){
        if(!advance(now))return;
        if(phase==Phase.COUNT_IN||phase==Phase.PLAYING){elapsedBase=elapsed(now);phase=Phase.PAUSED;lastResult="已暂停";}
    }
    public synchronized void resume(long now){
        if(!advance(now)||phase!=Phase.PAUSED)return;
        segmentAt=now;phase=scaled(now)<0?Phase.COUNT_IN:Phase.PLAYING;lastResult="继续练习";
    }
    public synchronized void stop(){reset("已停止");lastNow=-1;}
    /** Synchronize actual held pitches at a manual start/resume without grading them. */
    public synchronized void baseline(int mask){if(mask<0||(mask&~MASK)!=0)throw new IllegalArgumentException("Invalid practice input mask");held=mask;}

    /** Grade only new local pitch contacts; a held pitch never becomes another onset. */
    public synchronized void input(int mask,long now){
        if(mask<0||(mask&~MASK)!=0)throw new IllegalArgumentException("Invalid practice input mask");
        if(!advance(now))return;
        if(phase!=Phase.COUNT_IN&&phase!=Phase.PLAYING){held=mask;return;}
        int rising=mask&~held;held=mask;long position=scaled(now),window=WINDOW_MS*(long)bpm;
        if(position<-window)return; // Do not grade exploratory contacts early in the count-in.
        for(int offset=0;offset<25;offset++)if((rising&(1<<offset))!=0){
            int chosen=-1;long nearest=Long.MAX_VALUE;
            for(int i=0;i<song.notes.size();i++){
                PracticeSong.Note note=song.notes.get(i);
                if(note.midi!=offset+48||hit[i]||missed[i])continue;
                long distance=Math.abs(position-note.startTick*TICK_SCALE);
                if(distance<=window&&distance<nearest){chosen=i;nearest=distance;}
            }
            if(chosen>=0){
                hit[chosen]=true;hits++;long delta=position-song.notes.get(chosen).startTick*TICK_SCALE;
                lastErrorMs=(int)Math.round(delta/(double)bpm);
                lastResult=lastErrorMs<0?"提前":lastErrorMs>0?"稍迟":"节奏合拍";
            }else{wrong++;lastErrorMs=null;lastResult="音高或拍点不符";}
        }
    }

    public synchronized Scene scene(long now){advance(now);return new Scene(this,phase==Phase.STOPPED?0:scaled(now));}

    private static int mask(int midi){return midi<0?0:1<<(midi-48);}
    private long countScaled(){return 60000L*song.beatsPerBar;}
    private long endScaled(){return song.totalTicks*TICK_SCALE;}
    // A short final note must not truncate its promised inclusive +180ms window.
    // This tail only grades input; its visual target still ends at endScaled().
    private long finishScaled(){
        for(int i=song.notes.size()-1;i>=0;i--){
            PracticeSong.Note note=song.notes.get(i);
            if(note.midi>=0)return Math.max(endScaled(),note.startTick*TICK_SCALE+WINDOW_MS*(long)bpm+1);
        }
        return endScaled();
    }
    private long maxElapsed(){return (countScaled()+finishScaled()+bpm-1)/bpm;}
    private long elapsed(long now){
        if(phase==Phase.PAUSED)return elapsedBase;
        long cap=maxElapsed(),delta=now-segmentAt;
        if(delta>=cap||elapsedBase>=cap-delta)return cap;
        return elapsedBase+delta;
    }
    private long scaled(long now){return Math.min(finishScaled(),elapsed(now)*bpm-countScaled());}
    private void reset(String result){
        phase=Phase.STOPPED;held=hits=misses=wrong=0;elapsedBase=0;segmentAt=0;lastErrorMs=null;lastResult=result;
        java.util.Arrays.fill(hit,false);java.util.Arrays.fill(missed,false);
    }
    private boolean advance(long now){
        if(now<0||(lastNow>=0&&now<lastNow)){
            reset("时钟变化，已停止");lastNow=now>=0?now:-1;return false;
        }
        lastNow=now;
        if(phase!=Phase.COUNT_IN&&phase!=Phase.PLAYING)return true;
        long position=scaled(now),window=WINDOW_MS*(long)bpm;
        for(int i=0;i<song.notes.size();i++){
            PracticeSong.Note note=song.notes.get(i);
            if(note.midi>=0&&!hit[i]&&!missed[i]&&position-note.startTick*TICK_SCALE>window){
                missed[i]=true;misses++;lastErrorMs=null;lastResult="漏奏";
            }
        }
        phase=position<0?Phase.COUNT_IN:position>=finishScaled()?Phase.FINISHED:Phase.PLAYING;
        if(phase==Phase.FINISHED){held=0;lastResult="练习完成";}
        return true;
    }
}
