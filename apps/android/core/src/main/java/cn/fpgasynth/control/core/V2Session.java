package cn.fpgasynth.control.core;

import cn.fpgasynth.control.generated.FmContract;
import java.io.IOException;
import java.util.*;

/** HTTP context and native identities are independent. UI queues intent; one IO owner calls tick. */
public final class V2Session {
    public interface Listener { void changed(View view); }
    public static final class View {
        public final String phase,message;
        public final FmApi.State state;
        public final FmApi.Caps caps;
        public final FmApi.Catalog catalog;
        public final boolean connected,playable,editable,fmSelectable,fmEditable,canSwitch;
        public final int localMask,ownerMask,client,queueSize,performanceRequested,performanceApplied,midiChannel;
        public final long token,sequence,context,inputEpoch;
        public final Map<Integer,Integer> ccRequests;
        private View(V2Session s){
            phase=s.phase;message=s.message;state=s.state;caps=s.caps;catalog=s.catalog;
            connected=s.foreground&&s.initialized&&s.token!=0;
            playable=s.canPlay();editable=connected&&s.stable()&&!s.transitioning()&&state.playable()&&state.applied!=2;
            canSwitch=connected&&s.stable()&&!s.transitioning();
            fmSelectable=connected&&s.stable()&&!s.transitioning()&&(state.channelRoutes?state.canSelectPerformance(2):catalog!=null&&state.canSelectFm());
            fmEditable=playable&&state.applied==2&&state.fm.hasFeature(FmContract.FM_FEATURE_SET_PARAMETER);
            localMask=s.desired;ownerMask=s.confirmed;client=s.client;queueSize=s.frames.size()+s.operations.size();
            token=s.token;sequence=s.sequence;context=s.context;inputEpoch=s.inputEpoch;
            performanceApplied=state==null?-1:state.performanceEngine(s.client);performanceRequested=s.selectedRoute();midiChannel=state==null?-1:state.midiChannel(s.client);
            ccRequests=Collections.unmodifiableMap(new LinkedHashMap<>(s.ccRequests));
        }
    }
    private static final class Frame {
        final int mask,velocity;final long at;
        Frame(int mask,int velocity,long at){this.mask=mask;this.velocity=velocity;this.at=at;}
    }
    private static final class Op {
        final int code;final long a,b,c;
        Op(int code,long a,long b,long c){this.code=code;this.a=a;this.b=b;this.c=c;}
        boolean transition(){return code==FmContract.FM_HTTP_OPCODE_STOP||code==FmContract.FM_HTTP_OPCODE_ENGINE_SELECT||code==FmContract.FM_HTTP_OPCODE_FM_PRESET;}
    }
    private static final class Pending {
        final Op op;final long id,at,context;
        Pending(Op op,long id,long at,long context){this.op=op;this.id=id;this.at=at;this.context=context;}
    }
    private final Listener listener;
    private Transport transport,nextTransport;
    private FmApi.Caps caps,nextCaps;
    private FmApi.State state;
    private FmApi.Catalog catalog;
    private final Deque<Frame> frames=new ArrayDeque<>();
    private final LinkedHashMap<String,Op> operations=new LinkedHashMap<>();
    private final LinkedHashMap<Integer,Integer> ccRequests=new LinkedHashMap<>();
    private Pending keyPending,operationPending;
    private boolean foreground=true,opening,initialized,release,dropAfterRelease,refreshCaps,resumeConnection,connectionEligible;
    private long generation,inputEpoch,token,sequence,context,heartbeat=-1000,nextPoll;
    private int desired,confirmed,velocity=100,client=-1;
    private String phase="未连接",message="连接设备后核对 HTTP v2 能力。";
    public V2Session(Listener listener){this.listener=listener;}
    public synchronized View view(){return new View(this);}
    private void notifyView(){if(listener!=null)listener.changed(new View(this));}
    private boolean stable(){return state!=null&&(state.channelRoutes?state.cpuReady:!state.switching&&!state.recovering&&state.requested==state.applied);}
    private int selectedRoute(){
        if(operationPending!=null&&operationPending.op.code==FmContract.FM_HTTP_OPCODE_ENGINE_SELECT)return (int)operationPending.op.a;
        for(Op op:operations.values())if(op.code==FmContract.FM_HTTP_OPCODE_ENGINE_SELECT)return (int)op.a;
        return state==null?-1:state.performanceEngine(client);
    }
    private boolean transitioning(){return release||(operationPending!=null&&operationPending.op.transition())||hasTransition();}
    private boolean hasTransition(){for(Op op:operations.values())if(op.transition())return true;return false;}
    private boolean canPlay(){return foreground&&initialized&&token!=0&&stable()&&!transitioning()&&state.playableFor(client)&&(state.channelRoutes||state.applied!=2||catalog!=null);}
    private void clearIntent(){desired=confirmed=0;frames.clear();operations.clear();ccRequests.clear();inputEpoch++;}
    private void clearPending(){keyPending=operationPending=null;}
    public synchronized void connect(Transport t,FmApi.Caps value){
        generation++;clearIntent();clearPending();connectionEligible=true;resumeConnection=false;nextTransport=t;nextCaps=value;opening=true;foreground=true;initialized=false;
        release=token!=0;dropAfterRelease=release;phase="核对设备";message="读取能力与目录，建立新会话并确认空按键。";notifyView();
    }
    public synchronized void disconnect(){
        generation++;clearIntent();clearPending();connectionEligible=false;resumeConnection=false;opening=false;nextTransport=null;initialized=false;release=token!=0;dropAfterRelease=true;
        phase="未连接";message="本地触点已清空；断网时由 1.2 秒租约释放。";notifyView();
    }
    public synchronized void pause(){boolean canResume=connectionEligible;disconnect();connectionEligible=canResume;resumeConnection=canResume;foreground=false;phase="已暂停";notifyView();}
    public synchronized void resume(){
        foreground=true;clearIntent();clearPending();
        if(transport!=null&&resumeConnection){release=token!=0;dropAfterRelease=release;opening=true;refreshCaps=true;resumeConnection=false;}
        else {phase="未连接";message="请重新连接，旧音符不会重放。";}
        notifyView();
    }
    public synchronized void networkLost(){fail("Wi-Fi 断开；请重新连接，旧音符不会重放。",true);}
    public synchronized void releaseNotes(){
        desired=0;frames.clear();inputEpoch++;keyPending=null;
        if(token!=0&&!hasTransition()&&(operationPending==null||!operationPending.op.transition()))release=true;
        notifyView();
    }
    public synchronized void velocity(int value){if(value<1||value>127)throw new IllegalArgumentException();velocity=value;}
    public synchronized boolean keys(int mask,int strength,long now){
        if(mask<0||mask>Protocol.MASK_MAX||strength<1||strength>127)throw new IllegalArgumentException();
        if(!canPlay())return false;velocity=strength;if(mask==desired)return true;desired=mask;
        if(frames.size()>=32){clearIntent();clearPending();release=true;message="触控队列满，已清键；请重新按键。";}
        else frames.addLast(new Frame(mask,strength,now));notifyView();return true;
    }
    private boolean queue(String key,Op op){
        try{FmApi.encode(token,1,context,op.code,op.a,op.b,op.c,state,catalog,client);}catch(IllegalArgumentException e){return false;}
        if(operations.size()>=32&&!operations.containsKey(key))return false;
        operations.put(key,op);notifyView();return true;
    }
    public synchronized boolean parameter(int cc,int value){
        View v=new View(this);if((!v.editable&&!v.fmEditable)||!state.ccAvailable(cc))return false;
        if(state.applied!=0&&cc!=7&&cc!=10)return false;
        boolean ok=queue("cc:"+cc,new Op((int)FmContract.FM_HTTP_OPCODE_CC,cc,value,0));if(ok)ccRequests.put(cc,value);return ok;
    }
    public synchronized boolean decay(int ms){return new View(this).editable&&state.applied==1&&queue("decay",new Op((int)FmContract.FM_HTTP_OPCODE_KS_DECAY,ms,0,0));}
    public synchronized boolean mode(int value,long backend){
        View v=new View(this);if(!v.connected||!stable()||transitioning()||value<0||value>2||!caps.performanceAvailable(value)||(value==2&&!v.fmSelectable))return false;
        if(state.channelRoutes)backend=0;
        Op op=new Op((int)FmContract.FM_HTTP_OPCODE_ENGINE_SELECT,value,backend,0);
        try{FmApi.encode(token,1,context,op.code,op.a,op.b,0,state,catalog,client);}catch(IllegalArgumentException e){return false;}
        clearIntent();clearPending();operations.put("transition",op);release=true;phase="切换引擎";message="先清键，等待设备确认新模式。";notifyView();return true;
    }
    public synchronized boolean fmParameter(long id,int voice,int operator,long value){
        if(!new View(this).fmEditable||catalog==null)return false;
        FmContract.Parameter p=FmContract.getParameter(catalog.profile.id,id);if(p==null)return false;
        long v=FmContract.FM_TARGET_NONE,o=FmContract.FM_TARGET_NONE;
        if(p.scope_id==FmContract.FM_SCOPE_VOICE||p.scope_id==FmContract.FM_SCOPE_VOICE_OPERATOR)v=voice;
        if(p.scope_id==FmContract.FM_SCOPE_PATCH_OPERATOR||p.scope_id==FmContract.FM_SCOPE_VOICE_OPERATOR)o=operator;
        try{long target=FmContract.packTarget(v,o);return queue("fm:"+id+":"+target,new Op((int)FmContract.FM_HTTP_OPCODE_FM_PARAMETER,id,target,value));}
        catch(IllegalArgumentException e){return false;}
    }
    public synchronized boolean fmPreset(long id){
        if(!new View(this).fmEditable||!state.fm.hasFeature(FmContract.FM_FEATURE_APPLY_PRESET))return false;
        Op op=new Op((int)FmContract.FM_HTTP_OPCODE_FM_PRESET,id,0,0);
        try{FmApi.encode(token,1,context,op.code,id,0,0,state,catalog,client);}catch(IllegalArgumentException e){return false;}
        clearIntent();clearPending();operations.put("transition",op);release=true;phase="加载音色";message="先清键，等待音色完成后建立新会话。";notifyView();return true;
    }
    public synchronized boolean panic(){
        if(token==0||!foreground)return false;
        clearIntent();clearPending();release=false;operations.put("transition",new Op((int)FmContract.FM_HTTP_OPCODE_STOP,0,0,0));
        phase="停止请求";message="等待全局停止确认；旧按键已丢弃。";notifyView();return true;
    }
    private void fail(String reason,boolean safety){
        generation++;clearIntent();clearPending();opening=false;nextTransport=null;initialized=false;resumeConnection=false;connectionEligible=false;
        release=safety&&token!=0;dropAfterRelease=true;if(!release){token=0;client=-1;}
        phase="连接中断";message=reason;notifyView();
    }
    private void reopen(String reason){
        clearIntent();clearPending();initialized=false;token=0;client=-1;opening=true;refreshCaps=true;catalog=null;
        release=dropAfterRelease=false;phase="重新同步";message=reason+"；建立新 token 后请重新按键。";notifyView();
    }
    private long allocate(){if(sequence>=Protocol.UINT_MAX)throw new IllegalStateException("序号耗尽，请重新连接");return ++sequence;}
    private static final class HttpFailure extends IOException {
        private static final long serialVersionUID=1L;
        final int code;final String reason;
        HttpFailure(Transport.Response response){super("HTTP "+response.code+" "+response.body);code=response.code;String value="";try{Object raw=Json.object(response.body).get("code");if(raw instanceof String)value=(String)raw;}catch(IllegalArgumentException ignored){}reason=value;}
    }
    private static Transport.Response request(Transport t,String path,String body)throws IOException{
        Transport.Response r=t.request(path,body);if(r.code<200||r.code>=300)throw new HttpFailure(r);return r;
    }
    /** At most one runtime request per tick; catalog pagination only while no performance token exists. */
    public void tick(long now){
        Transport t;long g,tok=0,ctx=0,id=0;Op op=null;boolean init=false,poll=false,safety=false,drop=false;
        synchronized(this){
            g=generation;t=transport;
            if(token!=0&&sequence>=Protocol.UINT_MAX){fail("序号耗尽，请重新连接；租约将释放按键。",false);return;}
            if(initialized&&foreground&&desired!=0&&now-heartbeat>=1000){clearIntent();clearPending();release=true;message="演奏心跳延迟，已清键；请重新弹奏。";}
            if(release&&token!=0){release=false;safety=true;drop=dropAfterRelease;op=new Op((int)FmContract.FM_HTTP_OPCODE_KEYS_UPDATE,0,velocity,0);keyPending=null;}
            else if(opening&&foreground){
                if(nextTransport!=null){transport=t=nextTransport;caps=nextCaps;nextTransport=null;nextCaps=null;refreshCaps=false;state=null;catalog=null;}
                token=0;client=-1;sequence=0;confirmed=0;init=true;
            }else if(!foreground||token==0)return;
            else{
                if((keyPending!=null&&now-keyPending.at>900)||(operationPending!=null&&now-operationPending.at>(operationPending.op.transition()?3500:900))){fail("等待 ESP32 采用超时，已清键。",true);return;}
                // Transition operations follow their preceding empty-key acknowledgement.
                if(operationPending==null&&!operations.isEmpty()&&operations.values().iterator().next().transition()&&keyPending==null){
                    String k=operations.keySet().iterator().next();op=operations.remove(k);
                }else if(keyPending==null&&(!frames.isEmpty()||now-heartbeat>=250)&&!hasTransition()&&(operationPending==null||!operationPending.op.transition())){
                    Frame f=frames.isEmpty()?new Frame(desired,velocity,now):frames.removeFirst();
                    if(f.mask!=0&&now-f.at>300){clearIntent();clearPending();f=new Frame(0,velocity,now);message="旧按键超过 300 ms，已丢弃。";}
                    op=new Op((int)FmContract.FM_HTTP_OPCODE_KEYS_UPDATE,f.mask,f.velocity,0);
                }else if(operationPending==null&&keyPending==null&&!operations.isEmpty()){
                    String k=operations.keySet().iterator().next();op=operations.remove(k);
                }else if(now>=nextPoll)poll=true;else return;
            }
            if(op!=null){id=allocate();tok=token;ctx=context;}
        }
        try{
            if(init){
                FmApi.Caps cp;boolean refresh;synchronized(this){cp=caps;refresh=refreshCaps;}
                if(refresh)cp=new FmApi.Caps(request(t,"/api/v2/capabilities",null).body);
                FmApi.State st=new FmApi.State(request(t,"/api/v2/state",null).body,cp);
                if(st.channelRoutes?!st.cpuReady:st.switching||st.recovering){synchronized(this){if(g!=generation)return;caps=cp;state=st;message="设备切换或恢复中，等待稳定后重建空会话。";notifyView();}return;}
                FmApi.Catalog cat=null;String catalogReason="";
                if(st.fm.profile!=null&&cp.contractMatches){try{cat=FmApi.Catalog.load(t,cp,st);}catch(Exception e){catalogReason="FM目录未匹配："+e.getMessage();}}
                synchronized(this){if(g!=generation||!foreground)return;caps=cp;state=st;catalog=cat;}
                Transport.Response r=request(t,"/api/v2/session","");if(r.code!=201)throw new IOException("session未返回201");
                Map<String,Object> receipt=Json.object(r.body);long newToken=FmApi.u32(receipt.get("token")),c=FmApi.u32(receipt.get("client")),newContext=FmApi.u32(receipt.get("context_epoch"));
                if(FmApi.u32(receipt.get("api_major"))!=2||FmApi.u32(receipt.get("api_minor"))!=0||newToken==0||c>3||FmApi.u32(receipt.get("lease_ms"))!=1200||newContext!=st.context)throw new IOException("会话版本或上下文不匹配，请重新连接");
                synchronized(this){if(g!=generation||!foreground)return;token=newToken;client=(int)c;context=newContext;sequence=0;opening=false;refreshCaps=false;initialized=false;
                    release=true;dropAfterRelease=false;phase="同步会话";message=catalogReason.isEmpty()?"等待 ESP32 确认空按键，FPGA 发声未验证。":catalogReason;notifyView();}
            }else if(op!=null){
                String body;synchronized(this){if(g!=generation)return;body=FmApi.encode(tok,id,ctx,op.code,op.a,op.b,op.c,state,catalog,client);}
                Transport.Response r=request(t,"/api/v2/command",body);if(r.code!=202)throw new IOException("command未返回202");
                Map<String,Object> receipt=Json.object(r.body);
                if(FmApi.u32(receipt.get("api_major"))!=2||FmApi.u32(receipt.get("accepted"))!=id||FmApi.u32(receipt.get("context_epoch"))!=ctx||!"received".equals(receipt.get("stage"))||!"unverified".equals(receipt.get("fpga")))throw new IOException("命令回执不兼容");
                synchronized(this){if(g!=generation)return;
                    if(op.code==FmContract.FM_HTTP_OPCODE_KEYS_UPDATE){heartbeat=now;if(drop){token=0;client=-1;initialized=false;dropAfterRelease=false;}else keyPending=new Pending(op,id,now,ctx);}
                    else operationPending=new Pending(op,id,now,ctx);
                    nextPoll=now;message="HTTP 已接收，等待 ESP32 采用；FPGA 发声未验证。";notifyView();}
            }else if(poll){
                FmApi.Caps cp;synchronized(this){cp=caps;}
                FmApi.State st=new FmApi.State(request(t,"/api/v2/state",null).body,cp);
                synchronized(this){if(g!=generation)return;
                    FmApi.State before=state;state=st;
                    if(st.context!=context){reopen("设备上下文已改变");return;}
                    if(initialized&&st.tokens[client]!=token){fail("会话失效，请重新连接",false);return;}
                    boolean nativeChanged=before!=null&&(before.fm.session!=st.fm.session||before.fm.epoch!=st.fm.epoch||before.fm.topology!=st.fm.topology||!Arrays.equals(before.fm.operators,st.fm.operators));
                    boolean selectingRoute=operationPending!=null&&operationPending.op.code==FmContract.FM_HTTP_OPCODE_ENGINE_SELECT;
                    boolean routeChanged=before!=null&&before.midiChannel(client)!=st.midiChannel(client)&&!(selectingRoute&&st.midiChannel(client)==operationPending.op.a);
                    boolean disrupted=before!=null&&(st.channelRoutes?routeChanged||(before.playableFor(client)&&!st.playableFor(client)):
                            nativeChanged||before.requested!=st.requested||(!before.switching&&st.switching)||(!before.recovering&&st.recovering)||(before.playable()&&!st.playable()));
                    if(disrupted){clearIntent();clearPending();release=true;initialized=false;message="设备身份、布局或就绪状态改变，旧按键已清空。";}
                    Pending[] waiting={keyPending,operationPending};for(Pending p:waiting){if(p==null||st.tokens[client]!=token)continue;
                        if(st.resultSequences[client]==p.id&&st.resultContexts[client]==p.context&&st.resultCodes[client]!=FmContract.FM_RESULT_OK){fail("ESP32 拒绝请求 result="+st.resultCodes[client]+"，旧请求已丢弃。",true);return;}
                        boolean adopted=p.op.code==FmContract.FM_HTTP_OPCODE_KEYS_UPDATE?st.processed[client]>=p.id&&st.keys[client]==p.op.a:p.op.code==FmContract.FM_HTTP_OPCODE_STOP?st.stops[client]>=p.id:st.parameters[client]>=p.id;
                        if(p.op.code==FmContract.FM_HTTP_OPCODE_ENGINE_SELECT)adopted=adopted&&(st.channelRoutes?
                                st.cpuReady&&st.midiChannel(client)==p.op.a&&st.resultSequences[client]==p.id&&st.resultContexts[client]==p.context&&st.resultCodes[client]==FmContract.FM_RESULT_OK:
                                st.requested==p.op.a&&st.applied==p.op.a&&!st.switching&&!st.recovering);
                        if(adopted){if(p==keyPending){confirmed=(int)p.op.a;initialized=true;keyPending=null;}else {operationPending=null;if(p.op.transition()&&!(st.channelRoutes&&p.op.code==FmContract.FM_HTTP_OPCODE_ENGINE_SELECT)){reopen("停止、模式或音色已采用");return;}}}
                    }
                    phase=canPlay()?"已连接":"等待设备";message=operationPending!=null?"等待 ESP32 采用参数；按键心跳继续。":keyPending!=null?"等待 ESP32 采用按键。":"ESP32 状态已确认；FPGA 发声未验证。";
                    nextPoll=now+((keyPending!=null||operationPending!=null)?30:250);notifyView();
                }
            }
        }catch(Exception e){synchronized(this){if(g!=generation)return;
            boolean expired=e instanceof HttpFailure&&(((HttpFailure)e).code==409&&"stale_context".equals(((HttpFailure)e).reason)||((HttpFailure)e).code==410&&("unknown_session".equals(((HttpFailure)e).reason)||"session_expired".equals(((HttpFailure)e).reason)));
            if(expired&&(!safety||(!drop&&foreground)))reopen("HTTP 会话或上下文过期，已丢弃旧请求");
            else if(safety){token=0;client=-1;initialized=false;release=false;dropAfterRelease=false;clearPending();message="释放未确认，按键由 1.2 秒租约释放。";notifyView();}
            else fail(e.getMessage()==null?"设备数据不兼容":e.getMessage(),token!=0);
        }}
    }
}
