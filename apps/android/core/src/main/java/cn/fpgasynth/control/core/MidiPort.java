package cn.fpgasynth.control.core;

import java.io.IOException;
import java.util.List;

/** One-way MIDI output. Only the physical link is opened; there is no control RPC. */
public interface MidiPort extends ControlPort {
    void open() throws IOException;
    void send(List<MidiMessage> messages,long now) throws IOException;
    void check() throws IOException;
    @Override default Response request(String path,String body)throws IOException {
        throw new IOException("MIDI直发端口不提供状态请求。");
    }
}
