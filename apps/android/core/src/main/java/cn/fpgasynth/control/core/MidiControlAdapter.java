package cn.fpgasynth.control.core;

/**
 * Convert a supported subset of MIDI data into the existing ESP32 control API.
 * This does not send BLE-MIDI, USB-MIDI, or network MIDI bytes.
 */
public final class MidiControlAdapter implements MidiOutput {
    public interface Target {
        boolean notes(int mask,int velocity,long now);
        boolean cc(int cc,int value);
    }
    private final Target target;
    private int mask,velocity=1;
    public MidiControlAdapter(Target target){
        if(target==null)throw new IllegalArgumentException("Target required");this.target=target;
    }
    public synchronized boolean send(MidiMessage message,long now){
        if(message==null||message.channel()!=1)return false;
        int kind=message.status&0xf0;
        if(kind==0xb0){
            int cc=message.data1;
            return (cc==7||cc==10||cc==74)&&target.cc(cc,message.data2);
        }
        if(kind!=0x90&&kind!=0x80)return false;
        int note=message.data1;
        if(note<48||note>72)return false;
        int bit=1<<(note-48);
        int nextMask=kind==0x90?mask|bit:mask&~bit;
        int nextVelocity=kind==0x90?message.data2:velocity;
        if(!target.notes(nextMask,nextVelocity,now))return false;
        mask=nextMask;velocity=nextVelocity;return true;
    }
    /** The caller must release notes before discarding the accepted shadow. */
    public synchronized void reset(){mask=0;velocity=1;}
}
