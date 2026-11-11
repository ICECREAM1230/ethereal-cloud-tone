package cn.fpgasynth.control.core;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Map current motion intent to standard channel-1 MIDI data, without device IO. */
public final class SensorMidiMapper {
    public static final int TILT_NOTES=0,TILT_OFF=-1;
    private int rollMapping=TILT_NOTES,pitchCc=TILT_OFF,note=-1;
    private boolean armed;
    private long epoch,lastNow;
    private double pitchCenter;
    private final CcState rollState=new CcState(),pitchState=new CcState();
    private static final class CcState {
        int value=-1;long at=-1;
        void clear(){value=-1;at=-1;}
    }

    /** Stop and send its NoteOff before changing mappings. Nothing is sent here. */
    public synchronized void configure(int rollMapping,int pitchCc){
        if(armed||note>=0)throw new IllegalStateException("Stop before configuring MIDI mapping");
        if((rollMapping!=TILT_NOTES&&rollMapping!=TILT_OFF&&!controller(rollMapping))
                ||(pitchCc!=TILT_OFF&&!controller(pitchCc))
                ||(rollMapping>0&&rollMapping==pitchCc))throw new IllegalArgumentException("Unsupported or duplicate axis controller");
        this.rollMapping=rollMapping;this.pitchCc=pitchCc;clearControllers();
    }
    /** The raw pitch is degrees on the same coordinate system as Frame.pitch. */
    public synchronized void calibrate(double pitchRaw){
        if(!finite(pitchRaw))throw new IllegalArgumentException("Finite pitch calibration required");
        pitchCenter=angle(pitchRaw);
    }
    /** Explicitly start only after MotionInstrument.arm succeeded for this epoch. */
    public synchronized void arm(long epoch,long now){
        if(note>=0)throw new IllegalStateException("Send stop NoteOff before arming again");
        if(now<0)throw new IllegalArgumentException("Nonnegative monotonic milliseconds required");
        this.epoch=epoch;lastNow=now;armed=true;clearControllers();
    }

    /**
     * Returns immutable messages ordered NoteOff, controllers, NoteOn. The caller
     * supplies a current frame each time; no unsent controller value is retained.
     */
    public synchronized List<MidiMessage> update(MotionInstrument.Frame frame,long epoch,boolean eligible,long now){
        if(!armed)return Collections.emptyList();
        if(now<0||now<lastNow||epoch!=this.epoch||!eligible||!valid(frame))return stop();
        lastNow=now;
        int nextNote=frame.mask==0?-1:rollMapping==TILT_NOTES?frame.note:60;
        List<MidiMessage> out=new ArrayList<>();
        if(note>=0&&note!=nextNote){out.add(MidiMessage.noteOff(1,note));note=-1;}
        if(rollMapping>0)controller(out,rollState,rollMapping,ccValue(frame.roll),now);
        if(pitchCc!=TILT_OFF)controller(out,pitchState,pitchCc,ccValue(angle(angle(frame.pitch)-pitchCenter)),now);
        if(nextNote>=0&&note!=nextNote){out.add(MidiMessage.noteOn(1,nextNote,frame.velocity));note=nextNote;}
        return out.isEmpty()?Collections.emptyList():Collections.unmodifiableList(out);
    }

    /** Release exactly the MIDI note emitted earlier, discard CC state, and disarm. */
    public synchronized List<MidiMessage> stop(){
        List<MidiMessage> out=note<0?Collections.emptyList():Collections.singletonList(MidiMessage.noteOff(1,note));
        note=-1;armed=false;clearControllers();return out;
    }

    private void clearControllers(){rollState.clear();pitchState.clear();}
    private static void controller(List<MidiMessage> out,CcState state,int cc,int value,long now){
        if(value==state.value||(state.at>=0&&now-state.at<50))return;
        out.add(MidiMessage.controlChange(1,cc,value));state.value=value;state.at=now;
    }
    private static boolean valid(MotionInstrument.Frame frame){
        return frame!=null&&frame.fresh&&frame.calibrated&&frame.armed
                &&finite(frame.roll)&&finite(frame.pitch)&&finite(frame.force)&&frame.force>=0
                &&frame.note>=48&&frame.note<=72&&frame.velocity>=1&&frame.velocity<=127
                &&(frame.mask==0||frame.mask==(1<<(frame.note-48)));
    }
    private static boolean controller(int cc){return cc==7||cc==10||cc==74;}
    private static int ccValue(double angle){return (int)Math.round((Math.max(-35,Math.min(35,angle))+35)*127/70);}
    private static boolean finite(double value){return !Double.isNaN(value)&&!Double.isInfinite(value);}
    private static double angle(double value){
        double out=value%360;
        if(out>180)out-=360;
        if(out< -180)out+=360;
        return out;
    }
}
