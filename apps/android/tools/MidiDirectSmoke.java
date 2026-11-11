package cn.fpgasynth.control.tests;

import android.app.*;
import android.content.*;
import android.graphics.Bitmap;
import android.os.*;
import android.view.*;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.*;
import cn.fpgasynth.control.core.*;
import java.io.*;
import java.lang.reflect.*;
import java.util.*;

/** Actual native UI and actual direct session, with a fake physical MIDI port only. */
public final class MidiDirectSmoke extends Instrumentation {
    private Activity activity;private Bundle args;private int assertions;
    private interface Condition{boolean get()throws Exception;}
    private void check(boolean value,String why){assertions++;if(!value)throw new AssertionError(why);}
    private Object field(String name)throws Exception{Field f=activity.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(activity);}
    private Object property(Object o,String name)throws Exception{Field f=o.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(o);}
    private void call(String name,Class<?>[] types,Object... values)throws Exception{Method m=activity.getClass().getDeclaredMethod(name,types);m.setAccessible(true);m.invoke(activity,values);}
    private void ui(Runnable run){runOnMainSync(run);}
    private void waitFor(Condition condition,String why)throws Exception{long end=SystemClock.elapsedRealtime()+8000;while(SystemClock.elapsedRealtime()<end){if(condition.get())return;SystemClock.sleep(25);}throw new AssertionError(why);}
    private static final class Port implements MidiPort{
        volatile boolean closed;volatile int opens,requests;final List<MidiMessage> sent=new ArrayList<>();
        public Mode mode(){return Mode.BLUETOOTH;}public void open(){opens++;}
        public void check()throws IOException{if(closed)throw new IOException("closed");}
        public synchronized void send(List<MidiMessage> messages,long now)throws IOException{check();sent.addAll(messages);}
        public Response request(String path,String body)throws IOException{requests++;throw new IOException("unexpected RPC");}
        public void close(){closed=true;}
        synchronized int count(int status,int data){int n=0;for(MidiMessage m:sent)if(m.status==status&&m.data1==data)n++;return n;}
        synchronized int size(){return sent.size();}
    }
    private void touch(View view,int action){long now=SystemClock.uptimeMillis();MotionEvent e=MotionEvent.obtain(now,now,action,view.getWidth()*.04f,view.getHeight()*.78f,0);view.dispatchTouchEvent(e);e.recycle();}
    private void snapshot(String name)throws IOException{
        Bitmap b=getUiAutomation().takeScreenshot();File p=new File(getTargetContext().getExternalFilesDir(null),"midi-direct-"+args.getString("profile","tablet")+"-"+name+".png");try(FileOutputStream out=new FileOutputStream(p)){b.compress(Bitmap.CompressFormat.PNG,100,out);}b.recycle();
    }
    @Override public void onCreate(Bundle arguments){args=arguments;start();}
    @Override public void onStart(){Bundle result=new Bundle();try{
        Intent launch=getTargetContext().getPackageManager().getLaunchIntentForPackage("cn.fpgasynth.control");launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK);activity=startActivitySync(launch);waitForIdleSync();
        AppController session=(AppController)field("session");ControlPorts ports=(ControlPorts)field("ports");
        ui(()->{try{call("select",new Class[]{int.class},2);((Button)field("bluetoothMode")).performClick();}catch(Exception e){throw new RuntimeException(e);}});
        waitFor(()->ports.selected()==ControlPort.Mode.BLUETOOTH&&!((Boolean)field("connectionBusy")),"select Bluetooth");
        Port port=new Port();Class<?> factory=Class.forName(activity.getClass().getName()+"$PortFactory");Object proxy=java.lang.reflect.Proxy.newProxyInstance(factory.getClassLoader(),new Class[]{factory},(p,m,a)->port);
        ui(()->{try{call("queuePortAction",new Class[]{factory,boolean.class},proxy,false);}catch(Exception e){throw new RuntimeException(e);}});
        waitFor(()->session.view().playable,"physical port readiness enables native controls");waitForIdleSync();
        check(session.view().directMidi&&!session.view().demo&&session.view().token==0,"direct connection does not fabricate demo, token or device adoption");
        check(port.opens==1&&port.requests==0,"open once without API handshake");SystemClock.sleep(1100);check(port.size()==0&&port.requests==0,"idle has no polling, heartbeat or MIDI writes");
        check(((Button)field("ks")).isEnabled()&&((Button)field("fm")).isEnabled(),"three MIDI routes available without remote capability discovery");
        TextView label=(TextView)field("engineInfo");check(label.getVisibility()==View.VISIBLE&&label.getText().toString().contains("单向发送"),"single-direction status visible");
        Object waveform=null;for(Object row:(List<?>)field("rows"))if(((PatchControls.Control)property(row,"control")).cc==20&&((View)property(row,"box")).isShown()){waveform=row;break;}check(waveform!=null,"visible waveform choice");Button wave=(Button)property(waveform,"choice");check(wave.getText().toString().contains("本地默认"),"choice default must be identified as local");check(session.parameter(20,32),"waveform MIDI request accepted");waitFor(()->port.count(0xb0,20)==1&&wave.getText().toString().contains("已发送")&&wave.getText().toString().contains("脉冲波"),"choice must show successful transmission with selected value");
        View keyboard=(View)field("keyboard");check(keyboard.getContentDescription().toString().contains("已发送"),"keyboard accessibility must describe sent status");ui(()->touch(keyboard,MotionEvent.ACTION_DOWN));waitFor(()->port.count(0x90,48)==1,"native keyboard sends NoteOn channel1 C3");
        waitFor(()->((View)field("performance")).getContentDescription().toString().contains("已发送1"),"feedback says sent instead of device adopted");snapshot("keyboard");
        ui(()->touch(keyboard,MotionEvent.ACTION_UP));waitFor(()->port.count(0x80,48)==1,"native keyboard sends NoteOff");
        ui(()->{try{call("chooseSurface",new Class[]{boolean.class},true);}catch(Exception e){throw new RuntimeException(e);}});waitFor(()->session.view().playable,"surface release completed");waitForIdleSync();View pads=(View)field("pads");check(pads.getVisibility()==View.VISIBLE,"Pad available in same direct App");check(pads.getContentDescription().toString().contains("已发送"),"Pad labels represent transmission, not device confirmation");
        int before=port.size();ui(()->touch(pads,MotionEvent.ACTION_DOWN));waitFor(()->port.size()>before,"Pad sends MIDI");snapshot("pad");ui(()->touch(pads,MotionEvent.ACTION_UP));waitFor(()->session.view().ownerMask==0,"Pad release transmitted");
        ui(()->{try{call("chooseSurface",new Class[]{boolean.class},false);}catch(Exception e){throw new RuntimeException(e);}});waitFor(()->session.view().playable,"keyboard surface ready");waitForIdleSync();ui(()->{try{((Button)field("fm")).performClick();}catch(Exception e){throw new RuntimeException(e);}});waitFor(()->session.view().applied==2&&session.view().playable,"FM selects MIDI channel3 without native descriptor");
        ui(()->touch(keyboard,MotionEvent.ACTION_DOWN));waitFor(()->port.count(0x92,48)==1,"FM keyboard sends channel3");ui(()->touch(keyboard,MotionEvent.ACTION_UP));waitFor(()->port.count(0x82,48)==1,"FM NoteOff stays on original channel");
        ui(()->{try{call("select",new Class[]{int.class},1);}catch(Exception e){throw new RuntimeException(e);}});waitFor(()->session.view().playable,"sound page release completed");waitForIdleSync();
        check(((View)field("directFmSound")).getVisibility()==View.VISIBLE&&((View)field("fmPanel")).getVisibility()==View.GONE,"FM direct page shows basic MIDI controls and no native RPC editor");
        Object volume=null;for(Object row:(List<?>)field("rows"))if(((PatchControls.Control)property(row,"control")).cc==7&&((View)property(row,"box")).isShown()){volume=row;break;}check(volume!=null,"visible FM MIDI volume");View knob=(View)property(volume,"knob");check(knob.isEnabled(),"FM MIDI volume enabled");TextView value=(TextView)property(volume,"value");check(value.getText().toString().contains("本地默认"),"initial direct parameter must be labeled local, not device state");ui(()->knob.performAccessibilityAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD,null));waitFor(()->port.count(0xb2,7)>0,"volume sends channel3 CC7");waitFor(()->value.getText().toString().contains("已发送"),"transmitted parameter label");snapshot("fm");
        ui(()->{try{call("select",new Class[]{int.class},0);}catch(Exception e){throw new RuntimeException(e);}});waitFor(()->session.view().playable,"play page release completed");waitForIdleSync();ui(()->{try{((Button)field("ks")).performClick();}catch(Exception e){throw new RuntimeException(e);}});waitFor(()->session.view().applied==1&&session.view().playable,"KS MIDI route");
        check(!((View)field("decay")).isEnabled(),"unsupported dedicated T60 RPC is not exposed as a fabricated MIDI control");
        ui(()->{try{call("select",new Class[]{int.class},2);((Button)field("wifiMode")).performClick();}catch(Exception e){throw new RuntimeException(e);}});waitFor(()->port.closed&&!ports.isOpen()&&ports.selected()==ControlPort.Mode.WIFI,"Wi-Fi choice retires direct MIDI without auto-connecting");
        check(port.requests==0,"entire interaction has zero application protocol requests");result.putString("result","PASS");result.putString("evidence",assertions+" native assertions; actual UI/direct core + fake physical port; no RF/audio");
    }catch(Throwable error){StringWriter text=new StringWriter();error.printStackTrace(new PrintWriter(text));result.putString("result","FAIL");result.putString("error",text.toString());}finally{if(activity!=null)ui(()->activity.finish());finish(0,result);}}
}
