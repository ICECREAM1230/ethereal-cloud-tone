package cn.fpgasynth.control.core;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.*;
import java.lang.reflect.*;
/** Lifecycle acceptance tests against a labelled host model, never an audio claim. */
public final class V2SessionTest {
    private static Path fixtures;private static int assertions;private static long clock;
    private static void check(boolean b,String message){assertions++;if(!b)throw new AssertionError(message);}
    private static V2Session open(RuntimeModel m)throws Exception{
        clock=0;V2Session s=new V2Session(null);s.connect(m,new FmApi.Caps(m.read("v2-live-capabilities.json")));step(s,12);check(s.view().playable,"live FM with offline profile runtime=false");return s;
    }
    private static void step(V2Session s,int n){for(int i=0;i<n;i++){s.tick(clock);clock+=25;}}
    public static void main(String[] args)throws Exception{
        fixtures=Paths.get(args[0]);RuntimeModel m=new RuntimeModel(fixtures);V2Session s=open(m);
        check(m.sent.get(0)[3]==3&&m.sent.get(0)[4]==0,"new token empty bitmap first");
        check(s.view().context==10&&s.view().state.fm.epoch==4,"HTTP context distinct from native epoch");
        check(s.keys(1,100,clock),"new press accepted");step(s,5);check(s.view().ownerMask==1,"owner key acknowledgement");
        check(s.keys(0,100,clock),"release accepted");step(s,5);check(s.view().ownerMask==0,"release acknowledged");
        check(s.fmParameter(18,0,0,31),"catalog scoped parameter accepted");step(s,5);check(m.count(7,18)==1,"parameter once");
        check(!s.fmParameter(18,0,4,31),"reject actual operator overflow");check(!s.fmParameter(18,0,0,32),"reject range overflow");
        s.keys(1,100,clock);step(s,4);long tok=s.view().token,epoch=s.view().inputEpoch;
        check(s.fmPreset(0),"preset accepted");step(s,25);
        check(s.view().token!=tok&&s.view().inputEpoch>epoch&&s.view().localMask==0,"preset rebuild and clear old touches");
        int old=m.count(3,1);step(s,20);check(m.count(3,1)==old,"preset no old On replay");
        tok=s.view().token;check(s.panic(),"Panic accepted");step(s,25);check(s.view().token!=tok&&s.view().localMask==0,"Panic token rebuilt");
        check(s.mode(0,0),"switch DDS");step(s,25);check(s.view().state.applied==0,"mode applied readback");
        check(s.mode(2,1),"explicit FM selection");step(s,25);check(s.view().state.applied==2,"FM mode readback");
        m=new RuntimeModel(fixtures);s=open(m);s.keys(1,100,clock);step(s,4);m.holdParameters=true;
        s.fmParameter(18,0,0,30);step(s,26);check(m.count(3,1)>=3,"parameter pending does not block key heartbeats");
        check(s.view().message.contains("等待"),"later key result does not adopt pending parameter");
        m=new RuntimeModel(fixtures);s=open(m);s.keys(1,100,clock);step(s,4);tok=s.view().token;old=m.count(3,1);
        m.externalContext();step(s,35);check(s.view().token!=tok&&s.view().localMask==0,"external context reopens empty");check(m.count(3,1)<=old+1,"no old queue replay after detection");
        m=new RuntimeModel(fixtures);s=open(m);s.keys(1,100,clock);step(s,4);m.fm().put("session_id",0L);m.fm().put("flags",1L);step(s,25);
        check(!s.view().playable&&s.view().localMask==0,"PLL/native session loss gates FM");check(m.count(5,2)==0,"PLL loss never autoSELECT");
        m=new RuntimeModel(fixtures);s=open(m);s.keys(1,100,clock);step(s,4);old=m.count(3,1);s.pause();step(s,5);s.resume();step(s,25);
        check(s.view().localMask==0&&m.count(3,1)==old,"pause resume no On replay");
        m=new RuntimeModel(fixtures);s=open(m);m.dropOn=true;s.keys(1,100,clock);step(s,8);check(m.count(3,1)==1&&!s.view().playable,"ambiguous On never retried");
        m=new RuntimeModel(fixtures);s=open(m);s.keys(1,100,clock);step(s,4);clock+=1100;s.tick(clock);step(s,8);check(s.view().localMask==0,"late scheduler clears held keys");
        m=new RuntimeModel(fixtures);s=open(m);s.keys(1,100,clock);clock+=301;s.tick(clock);step(s,6);check(m.count(3,1)==0,"stale queued On not sent");
        m=new RuntimeModel(fixtures);s=open(m);m.rejectParameter=true;s.fmParameter(18,0,0,29);step(s,8);check(s.view().localMask==0,"owner STALE clears intent");
        m=new RuntimeModel(fixtures);s=open(m);s.keys(1,100,clock);step(s,4);old=m.count(3,1);
        Json.list(m.fm().get("voice_generations")).set(0,1L);step(s,14);check(s.view().localMask==1,"normal voice allocation generation does not stop held notes");
        s.disconnect();step(s,4);check(!s.view().playable&&s.view().token==0,"disconnect drains release without new session");
        m=new RuntimeModel(fixtures);s=open(m);for(int i=0;i<34;i++)s.keys((i%2)+1,100,clock);step(s,8);check(s.view().localMask==0,"overflow queue clears all intent");
        m=new RuntimeModel(fixtures);s=open(m);Field sequence=V2Session.class.getDeclaredField("sequence");sequence.setAccessible(true);sequence.setLong(s,Protocol.UINT_MAX);s.tick(clock);check(!s.view().playable,"sequence exhaustion never wraps");
        m=new RuntimeModel(fixtures);s=open(m);s.keys(1,100,clock);step(s,4);s.networkLost();step(s,4);s.resume();step(s,8);check(!s.view().playable&&s.view().localMask==0,"network loss needs explicit reconnect");
        m=new RuntimeModel(fixtures);s=open(m);tok=s.view().token;m.externalContext();s.releaseNotes();step(s,20);
        check(s.view().token!=0&&s.view().token!=tok&&s.view().playable&&s.view().localMask==0,"stale safety empty rebuilds new token");
        m=new RuntimeModel(fixtures);s=open(m);tok=s.view().token;m.token++;s.releaseNotes();step(s,20);
        check(s.view().token!=0&&s.view().token!=tok&&s.view().playable,"expired safety token rebuilds");
        m=new RuntimeModel(fixtures);s=open(m);m.externalContext();s.mode(0,0);step(s,20);
        check(s.view().playable&&m.count(5,0)==0,"stale mode pre-release discards old mode intent");
        m=new RuntimeModel(fixtures);s=open(m);int sessions=m.sessions;s.networkLost();s.pause();s.resume();step(s,20);
        check(m.sessions==sessions&&!s.view().playable,"network loss then immediate pause never enables reconnect");
        final RuntimeModel lateModel=new RuntimeModel(fixtures);CountDownLatch sessionEntered=new CountDownLatch(1),allowSession=new CountDownLatch(1);
        Transport late201=(path,body)->{if(path.equals("/api/v2/session")){sessionEntered.countDown();try{if(!allowSession.await(2,TimeUnit.SECONDS))throw new AssertionError("late201 wait");}catch(InterruptedException e){throw new java.io.IOException(e);}}return lateModel.request(path,body);};
        V2Session late=new V2Session(null);late.connect(late201,new FmApi.Caps(lateModel.read("v2-live-capabilities.json")));
        Thread io=new Thread(()->late.tick(0));io.start();check(sessionEntered.await(2,TimeUnit.SECONDS),"201 in flight");late.pause();allowSession.countDown();io.join(2000);check(!io.isAlive()&&late.view().token==0&&!late.view().playable,"late201 cannot resume paused UI");
        final RuntimeModel commandModel=new RuntimeModel(fixtures);CountDownLatch onEntered=new CountDownLatch(1),allowOn=new CountDownLatch(1);
        Transport late202=(path,body)->{if(body!=null&&path.equals("/api/v2/command")&&body.split(" ")[4].equals("1")){onEntered.countDown();try{if(!allowOn.await(2,TimeUnit.SECONDS))throw new AssertionError("late202 wait");}catch(InterruptedException e){throw new java.io.IOException(e);}}return commandModel.request(path,body);};
        V2Session cancelled=new V2Session(null);cancelled.connect(late202,new FmApi.Caps(commandModel.read("v2-live-capabilities.json")));clock=0;step(cancelled,12);cancelled.keys(1,100,clock);final long ioTime=clock;
        io=new Thread(()->{for(int i=0;i<8;i++)cancelled.tick(ioTime+i*25);});io.start();check(onEntered.await(2,TimeUnit.SECONDS),"On in flight");cancelled.disconnect();allowOn.countDown();io.join(2000);step(cancelled,4);commandModel.request("/api/v2/state",null);
        check(cancelled.view().token==0&&cancelled.view().localMask==0&&Json.number(commandModel.client().get("keys"))==0,"late202 disconnect queues safety empty, never replays On");
        System.out.println("V2Session: "+assertions+" lifecycle assertions PASS (host model; no audio/device)");
    }
}
