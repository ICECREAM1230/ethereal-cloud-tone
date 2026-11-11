package cn.fpgasynth.control.core;

import java.io.IOException;

/** All blocking methods run on the single IO owner; accessors never block the UI. */
public final class ControlPorts {
    private final AppController session;
    private volatile ControlPort.Mode selected=ControlPort.Mode.WIFI;
    private volatile ControlPort active;
    private Transport lease;
    private long generation;
    public ControlPorts(AppController session){this.session=session;}
    public ControlPort.Mode selected(){return selected;}
    public boolean isOpen(){return active!=null;}
    /** Capture before scheduling a grace period; this handle can only abort that old instance. */
    public Runnable abortHandle(){ControlPort port=active;return ()->{if(port!=null)port.close();};}
    public synchronized Transport endpoint()throws IOException{
        if(active==null)throw new IOException("控制端口未连接。");return lease;
    }
    private void retire(long now){
        session.disconnect();
        try{session.tick(now);}finally{
            generation++;ControlPort old=active;active=null;lease=null;
            if(old!=null)old.close();
        }
    }
    public synchronized void select(ControlPort.Mode mode,long now){
        if(mode==null)throw new IllegalArgumentException("mode");
        retire(now);selected=mode;
    }
    public synchronized void connect(ControlPort port,long now)throws IOException{
        if(port==null)throw new IllegalArgumentException("port");
        if(port==active)throw new IOException("不能重复连接同一个端口实例。");
        if(port.mode()!=selected){port.close();throw new IOException("端口与当前控制模式不匹配。");}
        retire(now);active=port;final long epoch=++generation;
        lease=(path,body)->{
            synchronized(ControlPorts.this){
                if(active!=port||generation!=epoch)throw new IOException("旧控制端口已失效。");
                ControlPort.validateRequest(path,body);return port.request(path,body);
            }
        };
        try{
            if(port instanceof MidiPort){((MidiPort)port).open();session.connectMidi((MidiPort)port);}
            else session.connect(lease,false);
        }catch(IOException|RuntimeException e){generation++;active=null;lease=null;port.close();session.disconnect();throw e;}
    }
    public synchronized void connectDemo(Transport demo,long now){retire(now);session.connect(demo,true);}
    public synchronized void disconnect(long now){retire(now);}
    public synchronized void tick(long now){session.tick(now);}
}
