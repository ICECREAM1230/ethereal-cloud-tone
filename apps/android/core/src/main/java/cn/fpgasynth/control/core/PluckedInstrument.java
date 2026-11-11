package cn.fpgasynth.control.core;

/**
 * Transport-independent, bounded KS excitation gates in the MIDI48..72 window.
 * All times are milliseconds on one monotonic clock. The caller schedules only
 * nextAt(); idle state produces no traffic. A rejected output fences this input
 * generation until a new epoch or an explicit disabled-to-enabled transition.
 */
public final class PluckedInstrument {
    public interface Output {boolean keys(int mask,long now);}
    public static final long GATE_MS=600;
    private static final long LATE_MS=300,GAP_MS=1000;
    private static final String[] CHORDS={"C","Am","Dm","F","G","Em"};
    private static final String[] NAMES={"C","C♯","D","D♯","E","F","F♯","G","G♯","A","A♯","B"};
    private static final int[][] NOTES={{67,64,60,55,52,48},{72,69,64,60,57,52},
            {69,65,62,57,53,50},{69,65,60,57,53,48},{71,67,62,59,55,50},{71,67,64,59,55,52}};
    private final Output output;
    private final long[] expires=new long[25];
    private boolean enabled,failed,clockSeen,sweepUp;
    private int mask,selected,spacing=45,sweepRow=6,sweepColumn,sweepSpacing;
    private long epoch,lastNow,sweepAt,revision;

    public PluckedInstrument(Output output){if(output==null)throw new IllegalArgumentException("output");this.output=output;}
    private static void column(int col){if(col<0||col>=6)throw new IllegalArgumentException("column");}
    public static String chord(int col){column(col);return CHORDS[col];}
    public static int note(int col,int row){column(col);if(row<0||row>=6)throw new IllegalArgumentException("row");return NOTES[col][row];}
    public static String name(int col,int row){int n=note(col,row);return NAMES[n%12]+(n/12-1);}
    public synchronized void state(boolean enabled,long epoch){
        boolean change=this.epoch!=epoch||this.enabled!=enabled;
        if(change||!enabled){clear();failed=false;}
        this.epoch=epoch;this.enabled=enabled;
    }
    public synchronized void interval(int milliseconds){if(milliseconds<20||milliseconds>160)throw new IllegalArgumentException("spacing");spacing=milliseconds;}
    public synchronized void select(int col){column(col);selected=col;}
    /** Clear only local work; the central session owns epoch/route release fences. */
    public synchronized void clear(){revision++;mask=0;for(int i=0;i<25;i++)expires[i]=0;sweepRow=6;clockSeen=false;}
    /** For autonomous view detach/visibility loss, release before forgetting intent. */
    public synchronized void release(long now){sweepRow=6;if(mask!=0&&!failed)emit(0,now);clear();}
    public synchronized void pluck(int col,int row,long eventAt,long now){
        note(col,row);selected=col;sweepRow=6;
        if(!advance(now)||!enabled||failed||eventAt<0||eventAt>now||now-eventAt>LATE_MS)return;
        start(note(col,row),now);
    }
    public synchronized void strum(boolean up,long now){
        if(!advance(now)||!enabled||failed)return;
        sweepRow=6;
        if(mask!=0&&!emit(0,now))return;
        for(int i=0;i<25;i++)expires[i]=0;
        sweepUp=up;sweepColumn=selected;sweepSpacing=spacing;sweepAt=now;sweepRow=0;
        sweep(now);
    }
    public synchronized void tick(long now){if(advance(now)&&enabled&&!failed)sweep(now);}
    public synchronized int mask(){return mask;}
    public synchronized int selected(){return selected;}
    public synchronized long nextAt(){
        if(!enabled||failed)return -1;
        long next=sweepRow<6?sweepAt:-1;
        for(int i=0;i<25;i++)if((mask&(1<<i))!=0&&(next<0||expires[i]<next))next=expires[i];
        return next;
    }
    private boolean advance(long now){
        if(now<0||(clockSeen&&now<lastNow)){release(Math.max(0,now));return false;}
        long gap=clockSeen?now-lastNow:0;clockSeen=true;lastNow=now;
        if(gap>LATE_MS)sweepRow=6;
        int next=mask;
        for(int i=0;i<25;i++)if((next&(1<<i))!=0&&(now>=expires[i]||gap>GAP_MS))next&=~(1<<i);
        if(next!=mask&&!emit(next,now))return false;
        return true;
    }
    private void sweep(long now){
        while(sweepRow<6&&sweepAt<=now&&!failed){
            int row=sweepUp?5-sweepRow:sweepRow;sweepRow++;
            long due=sweepAt;sweepAt+=sweepSpacing;
            if(now-due<=LATE_MS)start(note(sweepColumn,row),now);
        }
    }
    private void start(int midi,long now){
        int bit=1<<(midi-48);
        if((mask&bit)!=0&&!emit(mask&~bit,now))return;
        expires[midi-48]=now+GATE_MS;
        emit(mask|bit,now);
    }
    private boolean emit(int value,long now){
        long before=revision;mask=value;
        // The host may synchronously clear/change the session inside its output
        // callback. That fence must also stop the caller's remaining off/on or
        // sweep steps, even when the already-written release was accepted.
        if(output.keys(value,now))return revision==before;
        failed=true;clear();return false;
    }
}
