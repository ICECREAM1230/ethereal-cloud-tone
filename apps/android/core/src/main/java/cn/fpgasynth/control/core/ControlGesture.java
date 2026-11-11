package cn.fpgasynth.control.core;

/** UI-thread control gesture: coalesce intermediate values, retain an explicit final value. */
public final class ControlGesture {
    private boolean editing,changed;
    private long epoch,lastAt;
    private int pending,lastSent=-1;
    private static void valid(int value){if(value<0||value>127)throw new IllegalArgumentException("MIDI control outside 0..127");}
    public void begin(int value,long generation,long now){valid(value);editing=true;changed=false;epoch=generation;pending=value;lastSent=-1;lastAt=now;}
    public boolean editing(){return editing;}
    public void cancel(){editing=false;changed=false;lastSent=-1;}
    public Integer move(int value,long generation,long now,boolean eligible){
        valid(value);if(!editing||generation!=epoch||!eligible||now<lastAt){cancel();return null;}
        pending=value;changed=true;
        if(value==lastSent||(lastSent!=-1&&now-lastAt<50))return null;
        lastSent=value;lastAt=now;return value;
    }
    public Integer end(long generation,long now,boolean eligible){
        if(!editing||generation!=epoch||!eligible||now<lastAt){cancel();return null;}
        editing=false;return changed&&pending!=lastSent?pending:null;
    }
}
