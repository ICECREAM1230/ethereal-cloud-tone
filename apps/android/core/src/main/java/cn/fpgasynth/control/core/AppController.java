package cn.fpgasynth.control.core;
import java.util.*;

/** Protocol discovery stays on the IO owner. Only a real v2 404 selects v1. */
public final class AppController {
    public interface Listener {void changed(View view);}
    public static final class View {
        public final String phase,message;
        public final DeviceState state;
        public final FmApi.State v2;
        public final FmApi.Caps caps;
        public final FmApi.Catalog catalog;
        public final boolean connected,playable,editable,demo,fmSelectable,fmEditable,canSwitch;
        public final int localMask,ownerMask,client,queueSize,requested,applied,performanceRequested,performanceApplied,midiChannel;
        public final boolean channelRoutes;
        public final boolean directMidi;
        public final long inputEpoch,sequence,token;
        public final Map<Integer,Integer> ccRequests;
        public boolean ccAvailable(int cc){return v2==null||v2.ccAvailable(cc);}
        private View(AppController s,ControlSession.View old,V2Session.View modern,DirectMidiSession.View midi){
            boolean v=s.route==2,l=s.route==1,m=s.route==4;directMidi=m;
            phase=m?midi.phase:v?modern.phase:l?old.phase:s.phase;message=m?midi.message:v?modern.message:l?old.message:s.message;
            state=m?midi.state:v&&modern.state!=null?modern.state.legacy:l?old.state:null;v2=v?modern.state:null;caps=v?modern.caps:null;catalog=v?modern.catalog:null;
            connected=m?midi.connected:v?modern.connected:l&&old.connected;playable=m?midi.playable:v?modern.playable:l&&old.playable;editable=m?midi.editable:v?modern.editable:l&&old.editable;demo=l&&old.demo;
            fmSelectable=m?midi.playable:v&&modern.fmSelectable;fmEditable=v&&modern.fmEditable;canSwitch=connected&&(m?midi.playable:v?modern.canSwitch:old.editable);
            requested=m?midi.applied:v2!=null?(int)v2.requested:state!=null?state.requested:-1;applied=m?midi.applied:v2!=null?(int)v2.applied:state!=null?state.applied:-1;
            channelRoutes=m||v2!=null&&v2.channelRoutes;performanceRequested=v?modern.performanceRequested:requested;performanceApplied=v?modern.performanceApplied:applied;midiChannel=m?midi.applied:v?modern.midiChannel:-1;
            localMask=m?midi.localMask:v?modern.localMask:l?old.localMask:0;ownerMask=m?midi.sentMask:v?modern.ownerMask:l?old.ownerMask:0;
            client=v?modern.client:l?old.client:-1;queueSize=m?midi.queueSize:v?modern.queueSize:l?old.queueSize:0;
            sequence=v?modern.sequence:l?old.sequence:0;token=v?modern.token:l?old.token:0;
            long child=m?midi.inputEpoch:v?modern.inputEpoch:l?old.inputEpoch:0;
            if(s.lastRoute!=s.route||s.lastEpoch!=child){s.uiEpoch++;s.lastRoute=s.route;s.lastEpoch=child;}inputEpoch=s.uiEpoch;
            ccRequests=m?midi.ccRequests:v?modern.ccRequests:Collections.emptyMap();
        }
    }
    private final Listener listener;
    private final ControlSession legacy=new ControlSession(null);
    private final V2Session modern=new V2Session(null);
    private final DirectMidiSession midi=new DirectMidiSession(null);
    private int route,lastRoute=-1;
    private long generation,uiEpoch,lastEpoch;
    private boolean foreground=true;
    private Transport pending;
    private String phase="未连接",message="连接合成器 Wi-Fi 后建立会话。";
    public AppController(Listener listener){this.listener=listener;}
    public synchronized View view(){return new View(this,legacy.view(),modern.view(),midi.view());}
    private void notifyView(){if(listener!=null)listener.changed(view());}
    public synchronized void connect(Transport t,boolean demo){
        generation++;legacy.disconnect();modern.disconnect();midi.disconnect();pending=t;foreground=true;
        if(demo){route=1;legacy.connect(t,true);pending=null;}else{route=3;phase="发现协议";message="读取 HTTP v2 能力；等待空按键确认后演奏。";}
        notifyView();
    }
    /** The caller has already opened the physical MIDI link on its IO owner. */
    public synchronized void connectMidi(MidiPort port){generation++;legacy.disconnect();modern.disconnect();pending=null;foreground=true;route=4;midi.connect(port);notifyView();}
    public synchronized void disconnect(){generation++;pending=null;legacy.disconnect();modern.disconnect();midi.disconnect();route=0;phase="未连接";message="本 App 触点已清空，网络不可用时由租约释放。";notifyView();}
    public synchronized void pause(){generation++;foreground=false;pending=null;legacy.pause();modern.pause();midi.pause();if(route==3)route=0;notifyView();}
    public synchronized void resume(){foreground=true;legacy.resume();modern.resume();midi.resume();notifyView();}
    public synchronized void networkLost(){if(route==4)return;generation++;pending=null;legacy.networkLost();modern.networkLost();notifyView();}
    public synchronized void releaseNotes(){if(route==4)midi.releaseNotes();else if(route==2)modern.releaseNotes();else legacy.releaseNotes();notifyView();}
    public synchronized void velocity(int n){if(route==4)midi.velocity(n);else if(route==2)modern.velocity(n);else legacy.velocity(n);}
    public synchronized boolean keys(int mask,int velocity,long now){return route==4?midi.keys(mask,velocity,now):route==2?modern.keys(mask,velocity,now):route==1&&legacy.keys(mask,velocity,now);}
    public synchronized boolean parameter(int cc,int n){return route==4?midi.parameter(cc,n):route==2?modern.parameter(cc,n):route==1&&legacy.parameter(cc,n);}
    public synchronized boolean decay(int n){return route==2?modern.decay(n):route==1&&legacy.decay(n);}
    public synchronized boolean mode(int id){return route==4?midi.mode(id):route==2?modern.mode(id,id==2&&modern.view().catalog!=null?modern.view().catalog.profile.backend_id:0):route==1&&legacy.mode(id);}
    public synchronized boolean fmParameter(long id,int voice,int op,long value){return route==2&&modern.fmParameter(id,voice,op,value);}
    public synchronized boolean fmPreset(long id){return route==2&&modern.fmPreset(id);}
    public synchronized boolean panic(){return route==4?midi.panic():route==2?modern.panic():route==1&&legacy.panic();}
    public void tick(long now){
        int r;long g;Transport t;synchronized(this){r=route;g=generation;t=pending;}
        if(r==3){
            // Drain safety releases of old sessions before touching the new endpoint.
            legacy.tick(now);modern.tick(now);midi.tick(now);
            if(t==null)return;
            try{
                Transport.Response response=t.request("/api/v2/capabilities",null);
                FmApi.Caps c=response.code==200?new FmApi.Caps(response.body):null;
                synchronized(this){if(g!=generation||!foreground)return;
                    if(response.code==404){legacy.connect(t,false);route=1;}
                    else if(response.code==200){modern.connect(t,c);route=2;}
                    else throw new IllegalArgumentException("HTTP "+response.code+"，协议发现失败");pending=null;}
            }catch(Exception e){synchronized(this){if(g!=generation)return;pending=null;route=0;phase="连接中断";message=e.getMessage();}}
        }else if(r==4)midi.tick(now);else if(r==2)modern.tick(now);else if(r==1)legacy.tick(now);else{legacy.tick(now);modern.tick(now);midi.tick(now);}
        notifyView();
    }
}
