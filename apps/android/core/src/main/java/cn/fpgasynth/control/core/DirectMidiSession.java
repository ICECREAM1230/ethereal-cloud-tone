package cn.fpgasynth.control.core;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** One IO owner sends standard MIDI; UI calls only record local intent. */
public final class DirectMidiSession {
    public interface Listener {void changed(View view);}
    public static final class View {
        public final DeviceState state;
        public final String phase,message;
        public final boolean connected,playable,editable;
        public final int applied,localMask,sentMask,queueSize;
        public final long inputEpoch;
        public final Map<Integer,Integer> ccRequests;
        private View(DirectMidiSession s){
            state=s.state;phase=s.phase;message=s.message;connected=s.connected;
            playable=s.canPlay();editable=playable;applied=s.applied;
            localMask=s.desired;sentMask=s.sentMasks[s.applied];queueSize=s.pendingCount();
            inputEpoch=s.inputEpoch;ccRequests=Collections.unmodifiableMap(new LinkedHashMap<>(s.ccRequests));
        }
    }
    private static final class Frame {
        final boolean notes;
        final int channel,mask,strength,cc,value;
        final long at;
        private Frame(boolean notes,int channel,int mask,int strength,int cc,int value,long at){
            this.notes=notes;this.channel=channel;this.mask=mask;this.strength=strength;this.cc=cc;this.value=value;this.at=at;
        }
        static Frame notes(int channel,int mask,int strength,long at){return new Frame(true,channel,mask,strength,0,0,at);}
        static Frame cc(int channel,int cc,int value){return new Frame(false,channel,0,0,cc,value,0);}
    }
    private final Listener listener;
    private final Deque<Frame> frames=new ArrayDeque<>();
    private final int[] sentMasks=new int[3];
    private final LinkedHashMap<Integer,Integer> ccRequests=new LinkedHashMap<>();
    private Map<String,Object> patch;
    private DeviceState state;
    private MidiPort port;
    private boolean connected,foreground=true,release,stop,ioBusy;
    private int applied,desired,velocity=100;
    private Integer nextMode;
    private long generation,inputEpoch,releaseVersion,lastTick=-1,flightGeneration;
    private Frame inFlight;
    private String phase="未连接",message="本地默认值；蓝牙 MIDI 连接后可发送。";

    public DirectMidiSession(Listener listener){this.listener=listener;resetPatch();}
    public synchronized View view(){return new View(this);}
    private void notifyView(){if(listener!=null)listener.changed(new View(this));}
    private boolean canPlay(){return connected&&foreground&&!release&&nextMode==null&&!stop;}
    private int pendingCount(){return frames.size()+(inFlight!=null&&flightGeneration==generation?1:0);}
    private void clearInput(){desired=0;frames.clear();inputEpoch++;}
    private void requestRelease(){release=true;releaseVersion++;}
    private void resetPatch(){
        patch=Json.object(DemoDevice.INITIAL);ccRequests.clear();
        patch.put("local",true);patch.put("source","local-defaults-and-sent-values");patch.put("remote_state",false);
        // DeviceState's compatibility fields carry no readiness or owner claim.
        patch.put("ready",false);Map<String,Object> engine=Json.map(patch.get("engine"));
        engine.put("available",false);engine.put("valid",false);localRoute();
    }
    private void localRoute(){
        Map<String,Object> engine=Json.map(patch.get("engine"));long legacy=applied==1?1:0;
        engine.put("requested",legacy);engine.put("applied",legacy);refreshState();
    }
    private void refreshState(){state=new DeviceState(Json.stringify(patch));}
    /** The IO owner already opened this physical port. Connecting itself performs no IO. */
    public synchronized void connect(MidiPort value){
        if(value==null)throw new IllegalArgumentException("port");
        if(value==port&&ioBusy)throw new IllegalArgumentException("port already in use");
        generation++;clearInput();port=value;connected=true;foreground=true;release=stop=false;nextMode=null;
        Arrays.fill(sentMasks,0);applied=0;lastTick=-1;resetPatch();
        phase="MIDI 已连接";message="本地默认值；通过 MIDI 通道 1 发送，设备音色与发声未回读。";notifyView();
    }
    /** ControlPorts retires and closes the physical port after the safety tick. */
    public synchronized void disconnect(){
        clearInput();connected=false;nextMode=null;stop=false;requestRelease();
        phase="未连接";message="本 App 触点已清空；正在尝试发送已知音符的 NoteOff。";notifyView();
    }
    public synchronized void pause(){
        foreground=false;clearInput();nextMode=null;stop=false;requestRelease();
        phase="已暂停";message="触点已清空；后台仍尝试发送已知音符的 NoteOff。";notifyView();
    }
    public synchronized void resume(){
        foreground=true;clearInput();nextMode=null;stop=false;requestRelease();lastTick=-1;
        phase=connected?"MIDI 已连接":"未连接";message="旧触点不会重放，请重新按键；显示本地默认与已发送值。";notifyView();
    }
    public synchronized void releaseNotes(){
        clearInput();requestRelease();message="已清空本地触点，正在发送已知音符的 NoteOff。";notifyView();
    }
    public synchronized void velocity(int value){
        if(value<1||value>127)throw new IllegalArgumentException("velocity outside 1..127");velocity=value;
    }
    private boolean enqueue(Frame frame){
        if(pendingCount()>=32){clearInput();requestRelease();message="发送队列已满，旧触点已清空，请重新按键。";notifyView();return false;}
        frames.addLast(frame);return true;
    }
    public synchronized boolean keys(int mask,int strength,long now){
        if(mask<0||mask>Protocol.MASK_MAX||strength<1||strength>127||now<0)throw new IllegalArgumentException("invalid keys");
        if(!canPlay())return false;
        velocity=strength;if(mask==desired)return true;
        if(!enqueue(Frame.notes(applied,mask,strength,now)))return false;
        desired=mask;notifyView();return true;
    }
    public synchronized boolean parameter(int cc,int value){
        if(cc<0||cc>127||value<0||value>127)throw new IllegalArgumentException("CC outside 0..127");
        if(!canPlay()||!enqueue(Frame.cc(applied,cc,value)))return false;
        notifyView();return true;
    }
    public synchronized boolean mode(int value){
        if(value<0||value>2)throw new IllegalArgumentException("mode outside 0..2");
        if(!canPlay())return false;if(value==applied)return true;
        clearInput();nextMode=value;requestRelease();phase="切换 MIDI 通道";
        message="先发送旧通道 NoteOff；切换后请重新按键。";notifyView();return true;
    }
    public synchronized boolean panic(){
        if(!connected||!foreground)return false;
        clearInput();nextMode=null;stop=true;requestRelease();phase="停止演奏";
        message="将发送本 App NoteOff 及通道 1/2/3 的 CC123/120；其他来源的状态未回读。";notifyView();return true;
    }
    private static void addOff(List<MidiMessage> out,int channel,int mask){
        for(int bit=0;bit<25;bit++)if((mask&(1<<bit))!=0)out.add(MidiMessage.noteOff(channel+1,48+bit));
    }
    private static List<MidiMessage> releaseMessages(int[] masks,boolean stop){
        List<MidiMessage> out=new ArrayList<>();for(int channel=0;channel<3;channel++)addOff(out,channel,masks[channel]);
        if(stop)for(int channel=1;channel<=3;channel++){
            out.add(MidiMessage.controlChange(channel,123,0));out.add(MidiMessage.controlChange(channel,120,0));
        }
        return out;
    }
    private List<MidiMessage> messages(Frame frame){
        if(!frame.notes)return Collections.singletonList(MidiMessage.controlChange(frame.channel+1,frame.cc,frame.value));
        List<MidiMessage> out=new ArrayList<>();int old=sentMasks[frame.channel];addOff(out,frame.channel,old&~frame.mask);
        for(int bit=0;bit<25;bit++)if((frame.mask&~old&(1<<bit))!=0)out.add(MidiMessage.noteOn(frame.channel+1,48+bit,frame.strength));
        return out;
    }
    private void finishRelease(){
        if(nextMode!=null){applied=nextMode;nextMode=null;localRoute();}
        if(connected&&foreground){phase="MIDI 已连接";message="本地触点已释放；通过 MIDI 通道 "+(applied+1)+" 发送，请重新按键。";}
    }
    private static void close(MidiPort value){try{value.close();}catch(RuntimeException ignored){}}
    /** No heartbeat or remote polling. check() observes only the port's memory link status. */
    public void tick(long now){
        final MidiPort target;final long g,epoch,releaseAt;final Frame frame;final boolean safety,stopping;
        final int[] oldMasks;final List<MidiMessage> output;
        synchronized(this){
            if(ioBusy||port==null)return;
            if(lastTick>=0&&(now<lastTick||now-lastTick>=1000)&&(sentMasks[0]!=0||sentMasks[1]!=0||sentMasks[2]!=0)){
                clearInput();requestRelease();message="演奏调度停顿，旧触点已清空，请重新按键。";notifyView();
            }
            lastTick=now;
            Frame head=frames.peekFirst();
            if(!release&&head!=null&&head.notes&&head.mask!=0&&(now<head.at||now-head.at>300)){
                clearInput();requestRelease();message="按键排队超过 300 ms，已清空旧触点，请重新按键。";notifyView();
            }
            if(release&&sentMasks[0]==0&&sentMasks[1]==0&&sentMasks[2]==0&&!stop){
                release=false;finishRelease();notifyView();
            }
            if(!connected&&!release)return;
            target=port;g=generation;epoch=inputEpoch;releaseAt=releaseVersion;
            oldMasks=sentMasks.clone();safety=release;stopping=stop;
            frame=safety||!foreground?null:frames.pollFirst();
            output=safety?releaseMessages(oldMasks,stopping):frame==null?Collections.<MidiMessage>emptyList():messages(frame);
            ioBusy=true;inFlight=frame;flightGeneration=g;
        }
        boolean sent=false,cancelled=false,stale=false,failed=false,changed=false;
        try{
            target.check();
            synchronized(this){cancelled=g!=generation||port!=target||(frame!=null&&epoch!=inputEpoch);}
            if(!cancelled&&!output.isEmpty()){target.send(Collections.unmodifiableList(output),now);sent=true;}
            synchronized(this){
                stale=g!=generation||port!=target;
                if(!stale&&!cancelled){
                    if(safety){
                        Arrays.fill(sentMasks,0);
                        if(releaseAt==releaseVersion){release=false;stop=false;finishRelease();}
                        changed=true;
                    }else if(frame!=null){
                        if(frame.notes)sentMasks[frame.channel]=frame.mask;
                        else{ccRequests.put(frame.cc,frame.value);LocalPatchState.applyCC(patch,frame.cc,frame.value);refreshState();}
                        if(connected&&foreground&&!release){phase="MIDI 已连接";message="MIDI 已发送；显示本地默认与已发送值，设备采用和发声未回读。";}
                        changed=true;
                    }
                }
            }
        }catch(Exception e){
            failed=true;
            synchronized(this){
                stale=g!=generation||port!=target;
                if(!stale){
                    generation++;clearInput();port=null;connected=false;release=stop=false;nextMode=null;Arrays.fill(sentMasks,0);resetPatch();
                    phase="连接中断";message="MIDI 链路失败，旧触点已清空，请重新连接："+e.getMessage();changed=true;
                }
            }
        }finally{
            if(stale&&!failed){
                // A superseded successful write may have sounded only on the old port.
                int[] cleanup=oldMasks.clone();if(sent&&frame!=null&&frame.notes)cleanup[frame.channel]=frame.mask;
                if(sent&&safety)Arrays.fill(cleanup,0);
                List<MidiMessage> off=releaseMessages(cleanup,false);
                try{if(!off.isEmpty())target.send(Collections.unmodifiableList(off),now);}catch(Exception ignored){}
            }
            if(failed||stale)close(target);
            synchronized(this){ioBusy=false;inFlight=null;if(changed)notifyView();}
        }
    }
}
