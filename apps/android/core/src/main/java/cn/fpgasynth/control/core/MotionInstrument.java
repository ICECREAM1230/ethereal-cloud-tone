package cn.fpgasynth.control.core;

/**
 * A calibrated, monophonic motion instrument, independent of Android and transport.
 * Angles are degrees, force is linear acceleration in m/s², and both clocks are
 * milliseconds on the same local monotonic time base. Call frame every 20ms even
 * without new sensor events so a strike or a disconnected source is released.
 */
public final class MotionInstrument {
    private static final long FRESH_MS=250,STRIKE_MS=180,INTERVAL_MS=300;
    private static final double QUIET=1.5,ATTACK=4,ROLL_LIMIT=35;
    private static final double SMOOTHING=.35,STEP=5,HYSTERESIS=.75;
    private static final int[] NOTES={48,50,52,53,55,57,59,60,62,64,65,67,69,71,72};
    private boolean clockSeen,hasSample,calibrated,armed,ready,active;
    private long lastNow,lastSampleAt=-1,lastStrikeAt=-1,armedEpoch;
    private double rawRoll,pitch,force,center,filteredRoll;
    private int selected=7,strikeNote=7,strikeVelocity=1,ceiling=127;
    private String status="请校准";

    /** An immutable value snapshot. Even an idle frame has a legal MIDI velocity. */
    public static final class Frame {
        public final boolean fresh,calibrated,armed;
        public final double roll,pitch,force;
        public final int note,mask,velocity;
        public final String status;
        private Frame(MotionInstrument m){
            fresh=m.hasSample;calibrated=m.calibrated;armed=m.armed;
            roll=m.calibrated?m.filteredRoll:m.rawRoll;pitch=m.pitch;force=m.force;
            note=NOTES[m.active?m.strikeNote:m.selected];
            mask=m.active?1<<(note-48):0;
            velocity=Math.max(1,Math.min(m.strikeVelocity,m.ceiling));
            status=m.status;
        }
    }

    /**
     * Accepts a combined sensor sample. Old or duplicate sensor timestamps are
     * ignored without refreshing the source; malformed data cancels performance.
     * Eligibility and the ceiling come from the most recent arm/frame call.
     */
    public synchronized void sample(double roll,double pitch,double force,long sampleAt,long now){
        if(!advance(now))return;
        if(!finite(roll)||!finite(pitch)||!finite(force)||force<0){invalidate("数据无效");return;}
        if(sampleAt<0||sampleAt>now){invalidate("时间无效");return;}
        if(sampleAt<=lastSampleAt)return;
        if(now-sampleAt>FRESH_MS){invalidate("数据中断");return;}
        rawRoll=angle(roll);this.pitch=pitch;this.force=force;
        lastSampleAt=sampleAt;hasSample=true;
        if(calibrated){
            double target=Math.max(-ROLL_LIMIT,Math.min(ROLL_LIMIT,angle(rawRoll-center)));
            filteredRoll+=SMOOTHING*(target-filteredRoll);
            select();
        }
        if(!armed)return;
        if(force<QUIET)ready=true;
        else if(force>=ATTACK&&ready){
            // A crossing is consumed even during cooldown: holding high cannot
            // turn a rejected early attack into a delayed repeat.
            ready=false;
            if(lastStrikeAt<0||now-lastStrikeAt>=INTERVAL_MS){
                active=true;lastStrikeAt=now;strikeNote=selected;
                strikeVelocity=velocity(force,ceiling);
            }
        }
    }

    /** Re-centering always releases a current performance; it never starts one. */
    public synchronized boolean calibrate(long now){
        if(!advance(now))return false;
        stopInternal("待开始");
        if(!hasSample){status="数据中断";return false;}
        if(force>=QUIET){status="请保持静止";return false;}
        center=rawRoll;filteredRoll=0;selected=7;calibrated=true;return true;
    }

    /** Bind an explicit quiet start to one current, eligible control generation. */
    public synchronized boolean arm(long epoch,boolean eligible,int ceilingVelocity,long now){
        if(!advance(now))return false;
        stopInternal("已停止");
        if(!calibrated){status="请校准";return false;}
        if(!hasSample){status="数据中断";return false;}
        if(!eligible){status="尚未就绪";return false;}
        if(!validCeiling(ceilingVelocity)){status="力度无效";return false;}
        if(force>=QUIET){status="请保持静止";return false;}
        armedEpoch=epoch;ceiling=ceilingVelocity;armed=true;ready=true;status="演奏中";return true;
    }

    /** Release a strike and require another manual arm, while keeping the center. */
    public synchronized void stop(String reason){stopInternal(reason);}

    /** Discard calibration, sensor freshness and the old local clock generation. */
    public synchronized void reset(String reason){
        stopInternal(reason);clockSeen=false;hasSample=false;calibrated=false;
        lastNow=0;lastSampleAt=-1;lastStrikeAt=-1;armedEpoch=0;
        rawRoll=0;pitch=0;force=0;center=0;filteredRoll=0;
        selected=7;strikeNote=7;strikeVelocity=1;ceiling=127;
    }

    /** Polling enforces NoteOff, freshness, current eligibility and current epoch. */
    public synchronized Frame frame(long now,long epoch,boolean eligible,int ceilingVelocity){
        if(advance(now)&&armed){
            if(epoch!=armedEpoch)stopInternal("会话已变化");
            else if(!eligible)stopInternal("已停止");
            else if(!validCeiling(ceilingVelocity))stopInternal("力度无效");
        }
        ceiling=validCeiling(ceilingVelocity)?ceilingVelocity:1;
        return new Frame(this);
    }

    private boolean advance(long now){
        if(now<0||(clockSeen&&now<lastNow)){
            clockSeen=now>=0;lastNow=now;lastSampleAt=-1;lastStrikeAt=-1;
            invalidate("时钟异常");return false;
        }
        clockSeen=true;lastNow=now;
        if(hasSample&&now-lastSampleAt>FRESH_MS)invalidate("数据中断");
        if(active&&now-lastStrikeAt>=STRIKE_MS)active=false;
        return true;
    }
    private void invalidate(String reason){hasSample=false;stopInternal(reason);}
    private void stopInternal(String reason){
        armed=false;active=false;ready=false;
        status=reason==null||reason.trim().isEmpty()?"已停止":reason;
    }
    private void select(){
        while(selected<NOTES.length-1&&filteredRoll>(selected-7)*STEP+STEP/2+HYSTERESIS)selected++;
        while(selected>0&&filteredRoll<(selected-7)*STEP-STEP/2-HYSTERESIS)selected--;
    }
    private static boolean finite(double n){return !Double.isNaN(n)&&!Double.isInfinite(n);}
    private static boolean validCeiling(int n){return n>=1&&n<=127;}
    private static int velocity(double force,int ceiling){
        if(force>=12)return ceiling;
        return Math.max(1,Math.min(ceiling,(int)Math.round(1+126*(force-ATTACK)/8)));
    }
    private static double angle(double value){
        double wrapped=value%360;
        if(wrapped>180)wrapped-=360;
        if(wrapped< -180)wrapped+=360;
        return wrapped;
    }
}
