package cn.fpgasynth.control.core;
import java.nio.file.*;
import java.util.concurrent.*;
public final class AppControllerTest {
    private static int count;
    private static void check(boolean b,String m){count++;if(!b)throw new AssertionError(m);}
    private static void step(AppController c,int n){for(int i=0;i<n;i++)c.tick(i*25L);}
    public static void main(String[] args)throws Exception{
        Path f=Paths.get(args[0]);RuntimeModel m=new RuntimeModel(f);AppController c=new AppController(null);c.connect(m,false);step(c,20);
        check(c.view().playable&&c.view().fmEditable,"v2 discovered and gated FM");
        RuntimeModel bad=new RuntimeModel(f);bad.capsCode=503;c.connect(bad,false);step(c,3);check(!c.view().playable&&bad.sessions==0,"503 not v1 fallback");
        DemoDevice demo=new DemoDevice(()->0L);c.connect(demo,false);step(c,20);check(c.view().playable&&c.view().v2==null,"404 v1 fallback");
        RuntimeModel noFm=new RuntimeModel(f);
        noFm.state.clear();noFm.state.putAll(Json.object(noFm.read("v2-state.json")));
        Transport oldCaps=(p,b)->p.equals("/api/v2/capabilities")?new Transport.Response(200,noFm.read("v2-capabilities.json")):noFm.request(p,b);
        c.connect(oldCaps,false);step(c,20);check(!c.view().fmSelectable&&c.view().playable,"noFM explicitly closed, DDS remains playable");
        AppController late=new AppController(null);CountDownLatch entered=new CountDownLatch(1),allow=new CountDownLatch(1);
        Transport blocking=(p,b)->{entered.countDown();try{if(!allow.await(2,TimeUnit.SECONDS))throw new AssertionError("timeout");}catch(InterruptedException e){throw new java.io.IOException(e);}return new Transport.Response(200,m.read("v2-live-capabilities.json"));};
        late.connect(blocking,false);Thread io=new Thread(()->late.tick(0));io.start();entered.await(2,TimeUnit.SECONDS);late.pause();allow.countDown();io.join(2000);
        check(!io.isAlive()&&!late.view().playable&&late.view().token==0,"late capabilities cannot resume paused session");
        System.out.println("AppController: "+count+" assertions PASS");
    }
}
