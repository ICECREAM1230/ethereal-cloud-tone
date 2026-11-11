package cn.fpgasynth.control.core;
/** Sensor MIDI event output seam. Accepted control data does not confirm physical audio. */
public interface MidiOutput {
    boolean send(MidiMessage message,long now);
    /** Clear shadow state only after the caller has released active notes. */
    void reset();
}
