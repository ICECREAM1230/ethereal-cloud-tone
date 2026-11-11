package cn.fpgasynth.control.core;

import java.io.IOException;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.Map;

/** One IO owner calls tick; UI methods only enqueue intent. Never retry an ambiguous NoteOn. */
public final class ControlSession {
    public interface Listener { void changed(View view); }
    public static final class View {
        public final String phase, message;
        public final DeviceState state;
        public final boolean playable, editable, demo, connected;
        public final int localMask, ownerMask, queueSize, client;
        public final long inputEpoch, sequence, token;
        private View(ControlSession s) {
            phase=s.phase; message=s.message; state=s.state;
            demo=s.demo||(s.state!=null&&Boolean.TRUE.equals(s.state.raw.get("mock")));client=s.client;
            connected=s.token!=0 && s.initialized && s.foreground;
            playable=s.canPlay(); editable=connected && s.state!=null && s.state.playable()
                    && s.mode==null && !s.stop && !s.release && (s.pending==null || (s.pending.op!=5 && s.pending.op!=4));
            localMask=s.desired; ownerMask=s.confirmed; queueSize=s.frames.size();
            inputEpoch=s.inputEpoch; sequence=s.sequence; token=s.token;
        }
    }
    private static final class Frame {
        final int mask, velocity; final long at;
        Frame(int mask,int velocity,long at){this.mask=mask;this.velocity=velocity;this.at=at;}
    }
    private static final class Pending {
        final int op; final long id,a,at,epoch;
        Pending(int op,long id,long a,long at,long epoch){this.op=op;this.id=id;this.a=a;this.at=at;this.epoch=epoch;}
    }
    private final Listener listener;
    private Transport transport, nextTransport;
    private boolean demo, nextDemo, foreground=true, connecting, initialized, release, discardAfterRelease, stop;
    private String phase="未连接", message="连接合成器 Wi-Fi 后建立会话。";
    private long generation, inputEpoch, token, sequence, heartbeat=-1000, nextPoll;
    private int client=-1, desired, confirmed, velocity=100;
    private Integer mode, decay;
    private DeviceState state;
    private Pending pending;
    private final Deque<Frame> frames=new ArrayDeque<>();
    private final LinkedHashMap<Integer,Integer> parameters=new LinkedHashMap<>();

    public ControlSession(Listener listener){this.listener=listener;}
    public synchronized View view(){return new View(this);}
    private void notifyView(){if(listener!=null)listener.changed(new View(this));}
    private boolean canPlay(){return foreground && initialized && token!=0 && state!=null && state.playable()
            && mode==null && !stop && !release && (pending==null||(pending.op!=5 && pending.op!=4));}
    private void clearIntent(){desired=0;frames.clear();parameters.clear();mode=decay=null;stop=false;inputEpoch++;}
    public synchronized void connect(Transport t,boolean demo){
        generation++; clearIntent();pending=null;initialized=false;
        nextTransport=t;nextDemo=demo;connecting=true;release=token!=0;discardAfterRelease=release;
        foreground=true;phase="正在连接";message="建立会话并读取设备当前音色…";notifyView();
    }
    public synchronized void disconnect(){
        generation++;clearIntent();pending=null;connecting=false;nextTransport=null;initialized=false;
        release=token!=0;discardAfterRelease=true;phase="未连接";message="本 App 按键已松开；网络不可用时由 1.2 秒租约释放。";notifyView();
    }
    public synchronized void pause(){
        generation++;foreground=false;clearIntent();pending=null;connecting=false;nextTransport=null;
        release=token!=0;discardAfterRelease=false;phase="已暂停";message="后台不演奏；已清除本地按键并尝试释放。";notifyView();
    }
    public synchronized void resume(){
        foreground=true;clearIntent();pending=null;nextPoll=0;
        if(token!=0){release=true;phase="恢复连接";message="核对原会话，随后请重新按键。";}
        else {phase="未连接";message="请重新连接，断线前的音符不会重放。";}
        notifyView();
    }
    public synchronized void networkLost(){fail("Wi-Fi 已断开，请重新连接。按键租约最多 1.2 秒。",true);}
    public synchronized void releaseNotes(){
        desired=0;frames.clear();inputEpoch++;
        if(pending!=null&&pending.op==3)pending=null;
        if(token!=0&&initialized && mode==null && (pending==null||pending.op!=5))release=true;
        notifyView();
    }
    public synchronized boolean keys(int mask,int velocity,long now){
        if(mask<0||mask>Protocol.MASK_MAX||velocity<1||velocity>127)throw new IllegalArgumentException();
        if(!canPlay())return false;
        this.velocity=velocity;
        if(mask==desired)return true;
        desired=mask;
        if(frames.size()>=32){clearIntent();release=true;message="触控队列已满，已释放，请重新按键。";}
        else frames.addLast(new Frame(mask,velocity,now));
        notifyView();return true;
    }
    public synchronized void velocity(int value){if(value<1||value>127)throw new IllegalArgumentException();velocity=value;}
    public synchronized boolean parameter(int cc,int value){
        Protocol.encode(1,1,2,cc,value);
        if(!new View(this).editable || (state.requested==1 && cc!=7 && cc!=10))return false;
        parameters.put(cc,value);notifyView();return true;
    }
    public synchronized boolean decay(int ms){
        Protocol.encode(1,1,6,ms,0);
        if(!new View(this).editable||!state.available||state.requested!=1)return false;
        decay=ms;notifyView();return true;
    }
    public synchronized boolean mode(int value){
        Protocol.encode(1,1,5,value,0);
        if(!new View(this).editable || (value==1 && !state.available))return false;
        clearIntent();pending=null;mode=value;release=true;
        phase="切换引擎";message="释放按键并等待模式读回…";notifyView();return true;
    }
    public synchronized boolean panic(){
        if(token==0||!foreground)return false;
        clearIntent();pending=null;stop=true;release=false;
        phase="停止请求";message="即将停止所有客户端声音并关闭诊断测试音。";notifyView();return true;
    }
    private void fail(String text,boolean bestEffort){
        generation++;clearIntent();pending=null;connecting=false;nextTransport=null;initialized=false;
        release=bestEffort && token!=0;discardAfterRelease=true;
        if(!release){token=0;client=-1;}
        phase="连接中断";message=text;notifyView();
    }
    private long allocate(){
        if(sequence>=Protocol.UINT_MAX)throw new IllegalStateException("序号已耗尽，请重新连接。");
        return ++sequence;
    }
    private Transport.Response request(Transport t,String path,String body) throws IOException {
        Transport.Response r=t.request(path,body);
        if(r.code<200||r.code>=300)throw new IOException(httpError(r.code));
        return r;
    }
    public static String httpError(int code){
        switch(code){
            case 400:return "设备拒绝命令格式 (400)。";
            case 403:return "设备拒绝控制标记 (403)。";
            case 409:return "命令序号失效 (409)，请重新连接。";
            case 410:return "会话已过期 (410)，请重新连接。";
            case 503:return "设备的四个会话已占满 (503)，停止其他客户端后等待 10 秒再连接。";
            default:return "设备响应 HTTP "+code+"。";
        }
    }

    /** Scheduled every 25 ms; blocking transport has bounded deadlines. No parallel calls. */
    public void tick(long now){
        Transport t;long g,id=0,epoch;int op=0;long a=0,b=0;boolean init=false,poll=false,safety=false,drop=false;
        synchronized(this){
            g=generation;epoch=inputEpoch;t=transport;
            if(token!=0 && sequence>=Protocol.UINT_MAX){
                fail("会话序号已耗尽，请重新连接；按键由 1.2 秒租约释放。",false);return;
            }
            if(foreground && initialized && desired!=0 && now-heartbeat>=1000){
                clearIntent();pending=null;release=true;epoch=inputEpoch;
                message="演奏心跳延迟，已释放，请重新按键。";notifyView();
            }
            if(release && token!=0){
                safety=true;drop=discardAfterRelease;release=false;op=3;a=0;b=velocity;id=allocate();pending=null;
            } else if(connecting && foreground){
                t=nextTransport;transport=t;demo=nextDemo;nextTransport=null;connecting=false;
                token=0;sequence=0;client=-1;state=null;confirmed=0;init=true;
            } else if(!foreground||token==0)return;
            else if(pending!=null){
                if(now-pending.at>(pending.op==5?3500:900)){
                    fail("等待 ESP32 采用超时，已释放。请重新连接并重新按键。",true);return;
                }
                if(now>=nextPoll)poll=true;else return;
            } else if(stop){op=4;stop=false;}
            else if(mode!=null){op=5;a=mode;mode=null;}
            else if(!frames.isEmpty() || now-heartbeat>=250){
                Frame f=frames.isEmpty()?new Frame(desired,velocity,now):frames.removeFirst();
                if(f.mask!=0 && now-f.at>300){
                    clearIntent();f=new Frame(0,velocity,now);epoch=inputEpoch;
                    message="按键请求延迟超过 300 ms，已释放，请重新按键。";notifyView();
                }
                op=3;a=f.mask;b=f.velocity;
            } else if(decay!=null){op=6;a=decay;decay=null;}
            else if(!parameters.isEmpty()){
                Map.Entry<Integer,Integer> e=parameters.entrySet().iterator().next();op=2;a=e.getKey();b=e.getValue();parameters.remove(e.getKey());
            } else if(now>=nextPoll)poll=true;else return;
            if(op!=0 && !safety)id=allocate();
        }
        try {
            if(init){
                Transport.Response r=request(t,"/api/session","");
                if(r.code!=201)throw new IOException("设备未创建会话。");
                Map<String,Object> v=Json.object(r.body);long tok=Json.number(v.get("token")),c=Json.number(v.get("client"));
                if(tok<=0||tok>Protocol.UINT_MAX||c<0||c>3||Json.number(v.get("lease_ms"))!=1200)throw new IOException("会话 ABI 不兼容。");
                DeviceState s=new DeviceState(request(t,"/api/state",null).body);
                synchronized(this){if(g!=generation)return;token=tok;client=(int)c;state=s;heartbeat=now-250;nextPoll=now;
                    initialized=false;phase="同步会话";message="已读取设备音色；等待 ESP32 采用空按键状态。";notifyView();}
            } else if(op!=0){
                long tok; synchronized(this){if(g!=generation)return;tok=token;}
                Transport.Response r=request(t,"/api/command",Protocol.encode(tok,id,op,a,b));
                if(r.code!=202)throw new IOException("命令未按 ABI 返回 202。");
                Map<String,Object> receipt=Json.object(r.body);
                if(Json.number(receipt.get("accepted"))!=id || !"received".equals(receipt.get("stage")) || !"unverified".equals(receipt.get("fpga")))
                    throw new IOException("命令回执不兼容。");
                synchronized(this){
                    if(g!=generation)return;
                    if(op==3)heartbeat=now;
                    if(safety){
                        desired=confirmed=0;
                        if(drop){token=0;client=-1;discardAfterRelease=false;}
                        else if(foreground){pending=new Pending(3,id,0,now,inputEpoch);nextPoll=now;}
                    } else if(epoch==inputEpoch){
                        pending=new Pending(op,id,a,now,epoch);nextPoll=now;
                        message="请求已接收，等待 ESP32 采用；FPGA 发声未验证。";
                        notifyView();
                    }
                }
            } else if(poll){
                DeviceState s=new DeviceState(request(t,"/api/state",null).body);
                synchronized(this){
                    if(g!=generation)return;
                    if(initialized && s.tokens[client]!=token){fail("会话被替换，请重新连接。",false);return;}
                    boolean disruption=state!=null && (state.panic!=s.panic||state.recoveries!=s.recoveries
                            || (!state.switching&&s.switching)||(!state.recovering&&s.recovering)
                            || state.requested!=s.requested || (state.playable()&&!s.playable()));
                    state=s;
                    if(disruption){
                        clearIntent();release=pending==null||pending.op!=5;
                        if(pending!=null&&pending.op==3)pending=null;
                        message="设备停止、切换或恢复中，按键已释放；完成后请重新弹奏。";
                    }
                    Pending p=pending;
                    if(p!=null && s.tokens[client]==token){
                        boolean adopted=p.op==3?s.processed[client]>=p.id&&s.keys[client]==p.a
                                :p.op==4?s.stops[client]>=p.id:s.parameters[client]>=p.id;
                        if(p.op==5)adopted=adopted&&s.requested==p.a&&s.playable();
                        if(adopted){
                            if(p.op==3){confirmed=(int)p.a;initialized=true;}
                            if(p.op==4){confirmed=0;clearIntent();release=true;}
                            if(p.op==5)release=true;
                            pending=null;
                            message=p.op==5?"模式已读回，请重新按键；实际发声未验证。":"ESP32 已采用；FPGA 实际发声未验证。";
                        }
                    }
                    phase=s.playable() && initialized && !release && (pending==null||pending.op!=5)?"已连接":"等待设备";
                    nextPoll=now+(pending==null?500:30);notifyView();
                }
            }
        } catch(Exception e){
            synchronized(this){if(g!=generation)return;
                if(safety){token=0;client=-1;release=false;if(!connecting){initialized=false;phase=foreground?"连接中断":"已暂停";message="释放请求未确认，由 1.2 秒演奏租约兜底。";notifyView();}}
                else fail(e instanceof IOException?e.getMessage():"设备数据不兼容："+e.getMessage(),token!=0);
            }
        }
    }
}
