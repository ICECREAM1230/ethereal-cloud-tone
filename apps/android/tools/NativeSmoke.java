package cn.fpgasynth.control.tests;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import cn.fpgasynth.control.core.ControlSession;
import cn.fpgasynth.control.core.AppController;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.util.function.BooleanSupplier;

/** Runs only on the dedicated emulator; uses real View MotionEvents and lifecycle transitions. */
public final class NativeSmoke extends Instrumentation {
    private Activity activity;
    private AppController session;
    private View keyboard;
    private StringBuilder report=new StringBuilder();
    private String endpoint;
    private boolean probe;
    private Object field(String name)throws Exception{Field f=activity.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(activity);}
    private static void check(boolean v,String m){if(!v)throw new AssertionError(m);}
    private void waitFor(BooleanSupplier p,String m){long end=SystemClock.elapsedRealtime()+5000;while(!p.getAsBoolean()&&SystemClock.elapsedRealtime()<end)SystemClock.sleep(25);check(p.getAsBoolean(),m+"; "+session.view().message+" local="+session.view().localMask+" owner="+session.view().ownerMask+" playable="+session.view().playable+" focus="+activity.hasWindowFocus());}
    private Button find(View v,String text){if(v instanceof Button&&((Button)v).getText().toString().equals(text))return (Button)v;
        if(v instanceof ViewGroup)for(int i=0;i<((ViewGroup)v).getChildCount();i++){Button b=find(((ViewGroup)v).getChildAt(i),text);if(b!=null)return b;}return null;}
    private void event(int action,int[] ids,float[] xs){
        MotionEvent.PointerProperties[] p=new MotionEvent.PointerProperties[ids.length];MotionEvent.PointerCoords[] c=new MotionEvent.PointerCoords[ids.length];
        for(int i=0;i<ids.length;i++){p[i]=new MotionEvent.PointerProperties();p[i].id=ids[i];p[i].toolType=MotionEvent.TOOL_TYPE_FINGER;
            c[i]=new MotionEvent.PointerCoords();c[i].x=keyboard.getWidth()/15f*xs[i];c[i].y=keyboard.getHeight()*.82f;c[i].pressure=1;c[i].size=1;}
        long now=SystemClock.uptimeMillis();MotionEvent e=MotionEvent.obtain(now,now,action,ids.length,p,c,0,0,1,1,0,0,InputDevice.SOURCE_TOUCHSCREEN,0);
        runOnMainSync(()->keyboard.dispatchTouchEvent(e));e.recycle();
    }
    private void snap(String name)throws Exception {
        waitForIdleSync();SystemClock.sleep(1400);Bitmap image=getUiAutomation().takeScreenshot();check(image!=null,"screenshot");
        File f=new File(getTargetContext().getExternalFilesDir(null),name+".png");try(FileOutputStream out=new FileOutputStream(f)){image.compress(Bitmap.CompressFormat.PNG,100,out);}image.recycle();
    }
    private void pass(String text){report.append("PASS ").append(text).append('\n');}
    private static ControlSession legacySession(AppController controller)throws Exception{
        Field f=AppController.class.getDeclaredField("legacy");f.setAccessible(true);return (ControlSession)f.get(controller);
    }
    @Override public void onCreate(Bundle args){super.onCreate(args);endpoint=args.getString("endpoint");probe="true".equals(args.getString("probe"));start();}
    @Override public void onStart(){
        Bundle result=new Bundle();
        try{
            Intent launch=new Intent().setClassName("cn.fpgasynth.control","cn.fpgasynth.control.MainActivity").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            activity=startActivitySync(launch);session=(AppController)field("session");keyboard=(View)field("keyboard");
            if(probe){
                Class<?> type=Class.forName("cn.fpgasynth.control.WifiTransport");java.lang.reflect.Constructor<?> ctor=type.getDeclaredConstructor(android.net.ConnectivityManager.class,String.class);ctor.setAccessible(true);
                cn.fpgasynth.control.core.Transport transport=(cn.fpgasynth.control.core.Transport)ctor.newInstance(getTargetContext().getSystemService(android.net.ConnectivityManager.class),endpoint);
                for(String path:new String[]{"/api/session","/api/state","/api/state"}){
                    long at=SystemClock.elapsedRealtime();
                    try{cn.fpgasynth.control.core.Transport.Response r=transport.request(path,path.equals("/api/session")?"":null);report.append(path+" "+r.code+" chars="+r.body.length()+" elapsed="+(SystemClock.elapsedRealtime()-at)+"ms\n");}
                    catch(Exception e){java.io.StringWriter trace=new java.io.StringWriter();e.printStackTrace(new java.io.PrintWriter(trace));report.append(path+" elapsed="+(SystemClock.elapsedRealtime()-at)+"ms "+trace+"\n");}
                }
                android.net.ConnectivityManager manager=getTargetContext().getSystemService(android.net.ConnectivityManager.class);
                for(int i=0;i<3;i++){
                    long at=SystemClock.elapsedRealtime();java.net.HttpURLConnection c=(java.net.HttpURLConnection)manager.getActiveNetwork().openConnection(new java.net.URL(endpoint+"/api/state"));c.setConnectTimeout(2000);c.setReadTimeout(2000);
                    try{int code=c.getResponseCode();int count=0;try(java.io.InputStream in=c.getInputStream()){while(in.read()!=-1)count++;}report.append("long probe "+code+" chars="+count+" elapsed="+(SystemClock.elapsedRealtime()-at)+"ms\n");}
                    catch(Exception e){report.append("long probe "+e+" elapsed="+(SystemClock.elapsedRealtime()-at)+"ms\n");}finally{c.disconnect();}
                }
                for(int i=0;i<3;i++){
                    long at=SystemClock.elapsedRealtime();java.net.HttpURLConnection c=(java.net.HttpURLConnection)new java.net.URL(endpoint+"/api/state").openConnection();c.setConnectTimeout(350);c.setReadTimeout(350);
                    try{int code=c.getResponseCode();int count=0;try(java.io.InputStream in=c.getInputStream()){while(in.read()!=-1)count++;}report.append("default route "+code+" chars="+count+" elapsed="+(SystemClock.elapsedRealtime()-at)+"ms\n");}
                    catch(Exception e){report.append("default route "+e+" elapsed="+(SystemClock.elapsedRealtime()-at)+"ms\n");}finally{c.disconnect();}
                }
                java.net.URI u=new java.net.URI(endpoint);
                for(int i=0;i<3;i++){
                    long at=SystemClock.elapsedRealtime();try(java.net.Socket socket=manager.getActiveNetwork().getSocketFactory().createSocket()){
                        socket.connect(new java.net.InetSocketAddress(u.getHost(),u.getPort()),350);socket.setSoTimeout(350);
                        socket.getOutputStream().write(("GET /api/state HTTP/1.1\r\nHost: "+u.getHost()+"\r\nConnection: close\r\n\r\n").getBytes(java.nio.charset.StandardCharsets.US_ASCII));socket.getOutputStream().flush();
                        byte[] first=new byte[256];int n=socket.getInputStream().read(first);report.append("raw Wi-Fi socket bytes="+n+" elapsed="+(SystemClock.elapsedRealtime()-at)+"ms\n");
                    }catch(Exception e){report.append("raw Wi-Fi socket "+e+" elapsed="+(SystemClock.elapsedRealtime()-at)+"ms\n");}
                }
                result.putString("report",report.toString());result.putString("result","PROBE");finish(Activity.RESULT_OK,result);return;
            }
            snap("01-connection-portrait");check(session.view().token==0,"no automatic hardware connection");
            runOnMainSync(()->{Button b=find(activity.getWindow().getDecorView(),"进入模拟体验");check(b!=null,"demo button");b.performClick();});
            waitFor(()->session.view().playable,"demo ready");snap("02-play-portrait");
            check(session.view().state.n("volume")==207&&session.view().state.parameters[0]==0,"initial patch not overwritten");pass("installed APK starts, native demo and initial patch read");
            event(MotionEvent.ACTION_DOWN,new int[]{9},new float[]{.5f});
            event(MotionEvent.ACTION_POINTER_DOWN|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),new int[]{9,42},new float[]{.5f,2.5f});
            waitFor(()->session.view().ownerMask==17,"C3/E3 chord adoption");
            event(MotionEvent.ACTION_POINTER_UP,new int[]{9,42},new float[]{.5f,2.5f});waitFor(()->session.view().ownerMask==16,"stable IDs after pointer up");
            event(MotionEvent.ACTION_MOVE,new int[]{42},new float[]{4.5f});waitFor(()->session.view().ownerMask==128,"glissando to G3");
            event(MotionEvent.ACTION_CANCEL,new int[]{42},new float[]{4.5f});waitFor(()->session.view().ownerMask==0,"cancel release");pass("real multi-pointer MotionEvents, index changes, glissando, cancel");
            event(MotionEvent.ACTION_DOWN,new int[]{9},new float[]{.5f});
            event(MotionEvent.ACTION_POINTER_DOWN|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),new int[]{9,42},new float[]{.5f,.5f});waitFor(()->session.view().ownerMask==1,"same-key contacts");
            event(MotionEvent.ACTION_POINTER_UP,new int[]{9,42},new float[]{.5f,.5f});SystemClock.sleep(100);check(session.view().localMask==1,"same-key second contact retained");
            event(MotionEvent.ACTION_UP,new int[]{42},new float[]{.5f});waitFor(()->session.view().ownerMask==0,"final same-key release");pass("duplicate-key contacts do not prematurely release");
            event(MotionEvent.ACTION_DOWN,new int[]{9},new float[]{.5f});waitFor(()->session.view().ownerMask==1,"held before background");
            runOnMainSync(()->activity.moveTaskToBack(true));waitFor(()->!session.view().playable&&session.view().localMask==0,"background clears notes");
            getTargetContext().startActivity(launch.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));waitFor(()->session.view().playable,"foreground resume");
            waitFor(()->activity.hasWindowFocus(),"resumed window focus before a new touch");waitForIdleSync();
            check(session.view().localMask==0&&session.view().ownerMask==0,"no background replay");pass("actual task background and foreground clears old notes");
            Field transportField=ControlSession.class.getDeclaredField("transport");transportField.setAccessible(true);
            cn.fpgasynth.control.core.DemoDevice simulated=(cn.fpgasynth.control.core.DemoDevice)transportField.get(legacySession(session));
            event(MotionEvent.ACTION_DOWN,new int[]{9},new float[]{.5f});waitFor(()->session.view().ownerMask==1,"held before recovery");
            synchronized(simulated){java.util.Map<String,Object> e=cn.fpgasynth.control.core.Json.map(simulated.mutableState().get("engine"));
                e.put("fault",2L);e.put("recoveries",1L);e.put("recovering",true);e.put("switching",true);e.put("valid",false);}
            waitFor(()->session.view().state.recovering&&!session.view().playable&&session.view().localMask==0,"active recovery clears input");
            synchronized(simulated){java.util.Map<String,Object> e=cn.fpgasynth.control.core.Json.map(simulated.mutableState().get("engine"));
                e.put("recovering",false);e.put("switching",false);e.put("valid",true);}
            waitFor(()->session.view().playable&&session.view().ownerMask==0,"safe recovery retains historical fault");waitForIdleSync();
            check(session.view().state.fault==2&&session.view().localMask==0,"sticky fault diagnostic and empty re-arm");
            runOnMainSync(()->{try{String text=((android.widget.TextView)field("engineInfo")).getText().toString();
                check(text.contains("上次故障记录")&&!text.contains("已禁止演奏"),"recovered UI explains history");}catch(Exception e){throw new RuntimeException(e);}});
            event(MotionEvent.ACTION_MOVE,new int[]{9},new float[]{2.5f});SystemClock.sleep(100);
            check(session.view().localMask==0&&session.view().ownerMask==0,"old held pointer cannot re-arm by moving");
            event(MotionEvent.ACTION_UP,new int[]{9},new float[]{2.5f});event(MotionEvent.ACTION_DOWN,new int[]{71},new float[]{.5f});
            waitFor(()->session.view().ownerMask==1,"fresh touch after recovery");event(MotionEvent.ACTION_UP,new int[]{71},new float[]{.5f});
            waitFor(()->session.view().ownerMask==0,"fresh touch release");
            runOnMainSync(()->{try{((Button)field("ks")).performClick();}catch(Exception e){throw new RuntimeException(e);}});
            waitFor(()->session.view().playable&&session.view().state.applied==1,"KS adopted");check(!session.parameter(74,50),"DDS disabled in KS");snap("03-ks-portrait");pass("KS mode readback and DDS gating");
            check(session.view().state.fault==2,"mode confirmation retains historical fault");pass("sticky fault recovery, UI diagnostics, old held pointer discarded, fresh touch and mode confirmation");
            runOnMainSync(()->activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE));
            waitFor(()->keyboard.getWidth()>keyboard.getHeight()*2,"landscape sizing");snap("04-play-landscape");pass("rotation keeps 25-key native surface");
            runOnMainSync(()->activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT));SystemClock.sleep(300);
            runOnMainSync(()->{Button b=find(activity.getWindow().getDecorView(),"音色");check(b!=null,"sound tab");b.performClick();});snap("05-sound-portrait");
            pass("native sound page and capability-disabled controls");
            if(endpoint!=null){
                Class<?> type=Class.forName("cn.fpgasynth.control.WifiTransport");java.lang.reflect.Constructor<?> ctor=type.getDeclaredConstructor(android.net.ConnectivityManager.class,String.class);ctor.setAccessible(true);
                cn.fpgasynth.control.core.Transport inspect=(cn.fpgasynth.control.core.Transport)ctor.newInstance(getTargetContext().getSystemService(android.net.ConnectivityManager.class),endpoint);
                long existingVolume=new cn.fpgasynth.control.core.DeviceState(inspect.request("/api/state",null).body).n("volume");
                runOnMainSync(()->{try{
                    Button tab=find(activity.getWindow().getDecorView(),"连接");tab.performClick();
                    ((android.widget.EditText)field("address")).setText(endpoint);((Button)field("connect")).performClick();
                }catch(Exception e){throw new RuntimeException(e);}});
                waitFor(()->session.view().playable&&session.view().demo,"native HTTP mock connected");
                check(session.view().state.n("volume")==existingVolume,"HTTP existing patch retained");
                event(MotionEvent.ACTION_DOWN,new int[]{9},new float[]{.5f});waitFor(()->session.view().ownerMask==1,"HTTP NoteOn adopted");
                event(MotionEvent.ACTION_UP,new int[]{9},new float[]{.5f});waitFor(()->session.view().ownerMask==0,"HTTP NoteOff adopted");
                int targetVolume=existingVolume==140?35:70;
                check(session.parameter(7,targetVolume),"HTTP volume request");waitFor(()->session.view().state.n("volume")==targetVolume*2L,"HTTP parameter adopted");
                snap("06-http-mock-portrait");pass("installed APK native Wi-Fi HTTP route, receipt and owner adoption, initial patch and explicit CC");
                runOnMainSync(()->{keyboard.dispatchTouchEvent(MotionEvent.obtain(0,0,MotionEvent.ACTION_CANCEL,0,0,0));session.disconnect();});
            }
            result.putString("report",report.toString());result.putString("result","PASS");finish(Activity.RESULT_OK,result);
        }catch(Throwable e){result.putString("report",report.toString());result.putString("result","FAIL "+e);finish(Activity.RESULT_CANCELED,result);}
    }
}
