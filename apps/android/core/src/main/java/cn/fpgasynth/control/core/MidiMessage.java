package cn.fpgasynth.control.core;

/** An immutable standard three-byte MIDI channel message, without a wire transport. */
public final class MidiMessage {
    public final int status,data1,data2;
    private MidiMessage(int status,int data1,int data2){this.status=status;this.data1=data1;this.data2=data2;}
    public static MidiMessage noteOn(int channel,int note,int velocity){
        range(note,0,127,"note");range(velocity,1,127,"velocity");
        return new MidiMessage(status(0x90,channel),note,velocity);
    }
    public static MidiMessage noteOff(int channel,int note){
        range(note,0,127,"note");return new MidiMessage(status(0x80,channel),note,0);
    }
    public static MidiMessage controlChange(int channel,int cc,int value){
        range(cc,0,127,"controller");range(value,0,127,"controller value");
        return new MidiMessage(status(0xb0,channel),cc,value);
    }
    public static MidiMessage pitchBend(int channel,int value){
        range(value,0,16383,"pitch bend");return new MidiMessage(status(0xe0,channel),value&127,value>>7);
    }
    public byte[] bytes(){return new byte[]{(byte)status,(byte)data1,(byte)data2};}
    public int channel(){return (status&15)+1;}
    public String description(){
        switch(status&0xf0){
            case 0x90:return "NoteOn ch"+channel()+" "+data1+" v"+data2;
            case 0x80:return "NoteOff ch"+channel()+" "+data1;
            case 0xb0:return "CC ch"+channel()+" "+data1+"="+data2;
            default:return "PitchBend ch"+channel()+" "+(data1|(data2<<7));
        }
    }
    private static int status(int kind,int channel){range(channel,1,16,"channel");return kind|(channel-1);}
    private static void range(int value,int min,int max,String name){
        if(value<min||value>max)throw new IllegalArgumentException(name+" outside "+min+".."+max);
    }
}
