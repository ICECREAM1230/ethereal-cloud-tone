package cn.fpgasynth.control.core;

import java.io.IOException;
import java.util.List;

/** Physical BLE-MIDI output only; no control requests or remote state negotiation. */
public final class BleMidiPort implements MidiPort {
    public static final String SERVICE="03b80e5a-ede8-4b33-a751-6ce34ec4c700";
    public static final String CHARACTERISTIC="7772e5db-3868-4112-a1a9-f2669d106bf3";
    private final BleControlTransport.Link link;
    private final Object state=new Object(),operations=new Object();
    private volatile boolean closed;
    private volatile IOException failure;
    private boolean opened;
    public BleMidiPort(BleControlTransport.Link link){
        if(link==null)throw new IllegalArgumentException();this.link=link;
        link.setReceiver(new BleControlTransport.Receiver(){
            @Override public void received(byte[] packet){}
            @Override public void failed(IOException error){stop(error==null?new IOException("蓝牙MIDI连接中断。"):error);}
        });
    }
    @Override public Mode mode(){return Mode.BLUETOOTH;}
    @Override public void check()throws IOException{
        IOException error=failure;if(error!=null)throw error;
        if(closed)throw new IOException("蓝牙MIDI端口已关闭，请重新连接。");
    }
    private void stop(IOException error){
        synchronized(state){if(closed)return;if(failure==null)failure=error;}
        close();
    }
    @Override public void open()throws IOException{
        synchronized(operations){
            try{check();if(opened)return;link.open();check();opened=true;}
            catch(IOException e){stop(e);throw e;}
        }
    }
    @Override public void send(List<MidiMessage> messages,long now)throws IOException{
        synchronized(operations){
            try{
                check();if(!opened)throw new IOException("蓝牙MIDI端口尚未连接。");
                for(byte[] packet:BleMidiCodec.encode(messages,now,link.packetLimit())){
                    check();link.write(packet);check();
                }
            }catch(IOException e){stop(e);throw e;}
        }
    }
    @Override public void close(){
        synchronized(state){if(closed)return;closed=true;}
        // Never wait for the open/send lock: closing the physical link cancels blocked I/O.
        link.close();
    }
}
