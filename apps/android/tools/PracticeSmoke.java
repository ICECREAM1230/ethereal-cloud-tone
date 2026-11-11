package cn.fpgasynth.control.tests;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.os.Bundle;
import android.view.accessibility.AccessibilityNodeInfo;
import cn.fpgasynth.control.core.*;
import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicReference;

/** Actual native UI/session; only the physical MIDI port is replaced. No RF or audio. */
public final class PracticeSmoke extends Instrumentation {
    Activity activity;Bundle args;int assertions;AppController session;ControlPorts ports;Object panel;
    interface Action{void run()throws Exception;}interface Condition{boolean get()throws Exception;}
    void check(boolean value,String why){assertions++;if(!value)throw new AssertionError(why);}
    Object field(Object target,String name)throws Exception{Field f=target.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(target);}
    Object invoke(Object target,String name,Class<?>[] types,Object... args)throws Exception{Method m=target.getClass().getDeclaredMethod(name,types);m.setAccessible(true);return m.invoke(target,args);}
    void ui(Action a){AtomicReference<Throwable> e=new AtomicReference<>();runOnMainSync(()->{try{a.run();}catch(Throwable x){e.set(x);}});if(e.get()!=null)throw new RuntimeException(e.get());}
    void waitFor(Condition c,String why)throws Exception{long end=SystemClock.elapsedRealtime()+8000;while(SystemClock.elapsedRealtime()<end){if(c.get())return;SystemClock.sleep(20);}throw new AssertionError(why);}
    static final class Port implements MidiPort{
        volatile boolean closed;int requests;List<MidiMessage> sent=new ArrayList<>();
        public Mode mode(){return Mode.BLUETOOTH;}public void open(){}public void check()throws IOException{if(closed)throw new IOException("closed");}
        public synchronized void send(List<MidiMessage> m,long now)throws IOException{check();sent.addAll(m);}
        public Response request(String p,String b)throws IOException{requests++;throw new IOException("RPC forbidden");}public void close(){closed=true;}
        synchronized int ons(){int n=0;for(MidiMessage m:sent)if((m.status&0xf0)==0x90&&m.data2>0)n++;return n;}
    }
    void select(int index)throws Exception{ui(()->invoke(activity,"select",new Class[]{int.class},index));}
    void shot(String name)throws Exception{Bitmap b=getUiAutomation().takeScreenshot();File f=new File(getTargetContext().getExternalFilesDir(null),"practice-"+args.getString("profile","tablet")+"-"+name+".png");try(FileOutputStream out=new FileOutputStream(f)){b.compress(Bitmap.CompressFormat.PNG,100,out);}b.recycle();}
    void click(String name){try{if(name.equals("start"))waitFor(()->session.view().playable&&((Button)field(panel,"start")).isEnabled(),"manual start waits for route readiness");}catch(Exception e){throw new RuntimeException(e);}ui(()->((Button)field(panel,name)).performClick());}
    int guide()throws Exception{return (Integer)field(field(activity,"keyboard"),"guideMask");}
    void touch(View view,int action){long now=SystemClock.uptimeMillis();MotionEvent e=MotionEvent.obtain(now,now,action,view.getWidth()*.03f,view.getHeight()*.8f,0);view.dispatchTouchEvent(e);e.recycle();}
    @Override public void onCreate(Bundle a){args=a;start();}
    @Override public void onStart(){Bundle result=new Bundle();try{
        Intent intent=getTargetContext().getPackageManager().getLaunchIntentForPackage("cn.fpgasynth.control");intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK);activity=startActivitySync(intent);
        // Intentionally reflective: this assertion also runs against 0.9.0 for RED.
        panel=field(activity,"practicePanel");check(panel!=null,"native practice panel exists");
        check(((Button[])field(activity,"tabs")).length==4,"four navigation entries");
        ui(()->{for(Button tab:(Button[])field(activity,"tabs")){check(tab.getLayout()!=null&&tab.getLayout().getLineCount()==1,"navigation labels fit on one visible line");}});
        select(3);check(((View)field(activity,"pianoCard")).getVisibility()==View.GONE,"score page hides performance dock");
        check(field(activity,"scoreLibrary")!=null,"offline score library exists");shot("library");
        session=(AppController)field(activity,"session");ports=(ControlPorts)field(activity,"ports");
        select(2);ui(()->((Button)field(activity,"bluetoothMode")).performClick());waitFor(()->!((Boolean)field(activity,"connectionBusy")),"port selected");
        Port port=new Port();Class<?> factory=Class.forName(activity.getClass().getName()+"$PortFactory");Object proxy=Proxy.newProxyInstance(factory.getClassLoader(),new Class[]{factory},(p,m,a)->port);
        ui(()->invoke(activity,"queuePortAction",new Class[]{factory,boolean.class},proxy,false));waitFor(()->session.view().playable,"MIDI ready");select(0);
        ui(()->invoke(activity,"render",new Class[]{AppController.View.class},session.view()));click("expand");
        ui(()->{Bundle change=new Bundle();change.putFloat(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE,140);check(((View)field(panel,"speed")).performAccessibilityAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS.getId(),change),"speed adjusted through native accessibility action");});
        click("start");waitFor(()->invoke(field(panel,"model"),"scene",new Class[]{long.class},SystemClock.elapsedRealtime()).getClass().getField("phase").get(invoke(field(panel,"model"),"scene",new Class[]{long.class},SystemClock.elapsedRealtime())).toString().equals("FINISHED"),"entire 180 BPM exercise completes");check(port.ons()==0&&port.requests==0,"complete exercise never emits automatic notes or RPC");
        click("expand");click("start");
        waitFor(()->guide()!=0,"absolute clock guide appears after count-in");check(port.ons()==0&&port.requests==0,"count-in and guide never send NoteOn or RPC");SystemClock.sleep(180);shot("guide");
        View keyboard=(View)field(activity,"keyboard");ui(()->touch(keyboard,MotionEvent.ACTION_DOWN));waitFor(()->port.ons()==1,"manual key still sends exactly one MIDI NoteOn");ui(()->touch(keyboard,MotionEvent.ACTION_UP));
        check(((View)field(panel,"quickPause")).isShown(),"compact guide has visible pause control");click("quickPause");check(guide()==0,"pause clears guide");check(!((View)field(panel,"speed")).isEnabled(),"paused tempo cannot change timeline scale");click("quickPause");waitFor(()->guide()!=0,"resume shows guide");
        select(3);check(guide()==0,"score page clears guide");check(((View)field(activity,"pianoCard")).getVisibility()==View.GONE,"library remains full height");
        int previous=port.ons();ui(()->invoke(activity,"sendKeys",new Class[]{int.class,long.class},1,SystemClock.elapsedRealtime()));SystemClock.sleep(120);check(port.ons()==previous,"hidden library cannot send keyboard NoteOn");
        select(0);SystemClock.sleep(250);check(guide()==0,"return does not restart guide");
        click("start");waitFor(()->guide()!=0,"guide can be manually restarted");ui(()->((Button)field(activity,"ks")).performClick());waitFor(()->session.view().performanceApplied==1,"KS route applied");waitFor(()->guide()==0,"channel epoch is rendered and interrupts guide");check(guide()==0,"channel epoch interrupts guide");
        click("start");waitFor(()->guide()!=0,"KS guide starts");check((Integer)field(field(activity,"plucked"),"guideMask")!=0,"KS wood receives independent guide");SystemClock.sleep(180);shot("ks");
        ui(()->callActivityOnPause(activity));check(guide()==0,"background clears guide");ui(()->callActivityOnResume(activity));SystemClock.sleep(250);check(guide()==0,"foreground cannot replay guide");
        result.putString("result","PASS");result.putInt("assertions",assertions);
    }catch(Throwable e){StringWriter out=new StringWriter();e.printStackTrace(new PrintWriter(out));result.putString("result","FAIL");result.putString("error",out.toString());}finally{if(activity!=null)ui(()->activity.finish());}finish(0,result);}
}
