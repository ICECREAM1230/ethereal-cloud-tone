package cn.fpgasynth.control.core;

import java.io.IOException;
import java.util.*;

/** Exercises the same ControlPorts seam as the UI, with no simulated device replies. */
public final class MidiPortIntegrationTest {
    private static int checks;
    private static void check(boolean yes,String why){checks++;if(!yes)throw new AssertionError(why);}
    private static final class Port implements MidiPort {
        int opens,requests;boolean closed,failOpen;final List<MidiMessage> sent=new ArrayList<>();
        public Mode mode(){return Mode.BLUETOOTH;}
        public void open()throws IOException{opens++;if(failOpen)throw new IOException("GATT open failed");}
        public void check()throws IOException{if(closed)throw new IOException("closed");}
        public void send(List<MidiMessage> messages,long now)throws IOException{check();sent.addAll(messages);}
        public Response request(String path,String body)throws IOException{requests++;throw new IOException("No RPC supported");}
        public void close(){closed=true;}
    }
    public static void main(String[] args)throws Exception{
        AppController app=new AppController(null);ControlPorts ports=new ControlPorts(app);Port port=new Port();
        ports.select(ControlPort.Mode.BLUETOOTH,0);ports.connect(port,0);
        check(port.opens==1,"selected MIDI port must open its physical link exactly once");
        check(app.view().connected&&app.view().playable&&!app.view().demo,"GATT readiness alone enables direct MIDI");
        for(int i=0;i<10;i++)ports.tick(i*25);
        check(port.requests==0&&port.sent.isEmpty(),"idle direct connection has no API handshake, polling or heartbeat");
        check(app.view().token==0,"direct mode must not fabricate a remote token");
        check(app.keys(1,100,250),"key queued");ports.tick(250);
        check(port.sent.size()==1&&port.sent.get(0).status==0x90&&port.sent.get(0).data1==48,"C3 sent on default channel1");
        check(app.view().directMidi&&app.view().ownerMask==1,"direct view exposes transmitted notes without a fake device identity");
        PerformanceFeedback feedback=new PerformanceFeedback();feedback.update(1,true,false,1,1,100,250,true);
        check(feedback.snapshot(250).sendOnly&&!feedback.audio(1,250,.1,.1,.2,.2),"send-only feedback never implies device adoption or audio");
        check(app.mode(2),"choose FM MIDI route");ports.tick(275);
        check(port.sent.get(port.sent.size()-1).status==0x80,"old channel released before changing route");
        check(app.view().applied==2&&app.keys(2,90,300),"FM route ready without native FM descriptor");ports.tick(300);
        check(port.sent.get(port.sent.size()-1).status==0x92,"FM MIDI uses channel3");
        ports.select(ControlPort.Mode.WIFI,325);
        check(port.closed&&!ports.isOpen()&&!app.view().playable,"Wi-Fi choice retires old MIDI port without opening another one");
        check(port.sent.get(port.sent.size()-1).status==0x82,"mode change released FM note on its original channel");
        Port failed=new Port();failed.failOpen=true;ports.select(ControlPort.Mode.BLUETOOTH,350);
        try{ports.connect(failed,350);throw new AssertionError("open failure required");}catch(IOException expected){}
        check(failed.closed&&!ports.isOpen()&&!app.view().connected,"failed GATT open leaves no active MIDI session");
        System.out.println("MIDI port integration: "+checks+" assertions PASS; no hardware");
    }
}
