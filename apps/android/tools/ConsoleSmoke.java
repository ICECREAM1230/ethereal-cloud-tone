package cn.fpgasynth.control.tests;

import android.app.Activity;
import android.app.Instrumentation;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.Button;
import android.widget.EditText;
import android.view.ViewGroup;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;
import java.lang.reflect.Proxy;

/** Explicitly installed on an emulator; no BLE scan or physical device operation. */
public final class ConsoleSmoke extends Instrumentation {
    private Activity a;private Object session,originalMotionSource;private boolean pads;private StringBuilder report=new StringBuilder();private String profile;
    private static Object get(Object object,String name){try{Field f=object.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(object);}catch(Exception e){throw new RuntimeException(name,e);}}
    private Object field(String name){return get(a,name);}
    private static Object call(Object o,String method,Class<?>[] types,Object... args){try{Method m=o.getClass().getDeclaredMethod(method,types);m.setAccessible(true);return m.invoke(o,args);}catch(Exception e){throw new RuntimeException(method,e);}}
    private Object view(){return call(session,"view",new Class<?>[]{});}
    private int n(String name){return ((Number)get(view(),name)).intValue();}
    private boolean yes(String name){return (Boolean)get(view(),name);}
    private interface Action{void run()throws Exception;}
    private void main(Action f){AtomicReference<Throwable> error=new AtomicReference<>();runOnMainSync(()->{try{f.run();}catch(Throwable e){error.set(e);}});if(error.get()!=null)throw new RuntimeException(error.get());}
    private void check(boolean value,String why){if(!value)throw new AssertionError(why);}
    private void waitFor(BooleanSupplier p,String why){long end=SystemClock.elapsedRealtime()+6000;while(!p.getAsBoolean()&&SystemClock.elapsedRealtime()<end)SystemClock.sleep(25);check(p.getAsBoolean(),why);}
    private void pass(String s){report.append("PASS ").append(s).append('\n');}
    private EditText input(View v){if(v instanceof EditText)return (EditText)v;if(v instanceof ViewGroup)for(int i=0;i<((ViewGroup)v).getChildCount();i++){EditText result=input(((ViewGroup)v).getChildAt(i));if(result!=null)return result;}return null;}
    private void preciseWhileDisabled(){
        Object target=null;for(Object row:(List<?>)field("rows"))if(((Number)get(get(row,"control"),"cc")).intValue()==7){target=row;break;}Object row=target;check(row!=null,"precise control exists");
        main(()->call(row,"precise",new Class<?>[]{}));waitForIdleSync();AlertDialog dialog=(AlertDialog)get(row,"editorDialog");check(dialog!=null&&dialog.isShowing(),"precise editor opens");
        main(()->{input(dialog.getWindow().getDecorView()).setText("87");call(session,"pause",new Class<?>[]{});Object state=view();call(a,"render",new Class<?>[]{state.getClass()},state);dialog.getButton(AlertDialog.BUTTON_POSITIVE).performClick();});
        check(dialog.isShowing(),"failed precise edit must preserve the dialog instead of silently dismissing");check(input(dialog.getWindow().getDecorView()).getError()!=null,"disabled edit explains why it was retained");
        main(()->call(session,"resume",new Class<?>[]{}));waitFor(()->yes("editable"),"session recovers while retaining numeric input");
        main(()->dialog.getButton(AlertDialog.BUTTON_POSITIVE).performClick());check(!dialog.isShowing(),"accepted precise edit dismisses only after submission");
        waitFor(()->((Number)call(get(view(),"state"),"n",new Class<?>[]{String.class},"volume")).longValue()==174,"retained exact value adopted after explicit retry");pass("precise input retains its value on loss of eligibility and applies it after explicit retry");
    }
    private void layoutProbe()throws Exception{
        android.content.res.Configuration config=a.getResources().getConfiguration();report.append("WINDOW ").append(config.screenWidthDp).append('x').append(config.screenHeightDp).append("dp font=").append(config.fontScale).append('\n');
        for(int tab:new int[]{0,1}){final int next=tab;main(()->call(a,"select",new Class<?>[]{int.class},next));waitForIdleSync();snap("layout-"+tab);
            main(()->{for(Button button:(Button[])field("tabs")){Rect visible=new Rect();check(button.getGlobalVisibleRect(visible)&&visible.height()>=button.getHeight()-2,"navigation remains completely visible in short/large-font layout");}
                for(String key:new String[]{"stop","focusButton"}){View button=(View)field(key);Rect visible=new Rect();check(button.getGlobalVisibleRect(visible)&&visible.height()>=button.getHeight()-2,key+" remains completely visible");}
                if(next==1){View viewport=(View)field("content");check(viewport.getHeight()>=72*getTargetContext().getResources().getDisplayMetrics().density,"sound parameters keep a usable viewport");}});
        }
        if((Boolean)field("padMode")&&config.screenWidthDp>=640&&config.screenHeightDp<=520){View pad=(View)field("pads");check(((Number)get(pad,"columns")).intValue()==8,"short landscape uses an 8x2 note surface");
            event(pad,MotionEvent.ACTION_DOWN,new int[]{9},new float[]{.0625f},new float[]{.75f});
            event(pad,MotionEvent.ACTION_POINTER_DOWN|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),new int[]{9,83},new float[]{.0625f,.9375f},new float[]{.75f,.25f});waitFor(()->n("ownerMask")==32769,"8x2 pad geometry preserves low/high note mapping");
            event(pad,MotionEvent.ACTION_CANCEL,new int[]{9,83},new float[]{.0625f,.9375f},new float[]{.75f,.25f});waitFor(()->n("ownerMask")==0,"compact pad cancellation releases both notes");}
        pass("layout: complete navigation/stop/focus buttons and usable sound viewport");
    }
    private long volume(){return ((Number)call(get(view(),"state"),"n",new Class<?>[]{String.class},"volume")).longValue();}
    private void chooseInstrument(int kind){main(()->{((Button)field("surfaceChoice")).performClick();AlertDialog dialog=(AlertDialog)field("surfaceDialog");check(dialog!=null&&dialog.isShowing(),"instrument chooser opens");dialog.getListView().performItemClick(dialog.getListView().getChildAt(kind),kind,kind);});waitFor(()->a.hasWindowFocus(),"chooser returns focus");waitForIdleSync();}
    private void motionSample(double force){main(()->{long now=SystemClock.elapsedRealtime();call(a,"acceptMotion",new Class<?>[]{double.class,double.class,double.class,long.class,long.class},0d,0d,force,now,now);});}
    private void motionSample(double roll,double pitch,double force){main(()->{long now=SystemClock.elapsedRealtime();call(a,"acceptMotion",new Class<?>[]{double.class,double.class,double.class,long.class,long.class},roll,pitch,force,now,now);});}
    private boolean motionArmed(){Object f=field("motionFrame");return f!=null&&(Boolean)get(f,"armed");}
    private void armMotion(){
        motionSample(0);main(()->((Button)get(field("motionPanel"),"calibrate")).performClick());
        waitFor(()->yes("playable"),"calibration release adopted");motionSample(0);
        main(()->((Button)get(field("motionPanel"),"start")).performClick());waitFor(()->motionArmed(),"explicit motion start");
    }
    private void motionProbe()throws Exception{
        check(field("motionPanel")!=null,"motion instrument panel exists");chooseInstrument(2);
        Object nativeSource=field("motionSensors");originalMotionSource=nativeSource;report.append("NATIVE SENSOR available=").append(call(nativeSource,"available",new Class<?>[]{})).append(" running=").append(call(nativeSource,"running",new Class<?>[]{})).append('\n');
        if((Boolean)call(nativeSource,"available",new Class<?>[]{})){
            waitFor(()->field("motionFrame")!=null&&(Boolean)get(field("motionFrame"),"fresh"),"Android virtual sensors deliver fresh data through the native adapter");report.append("NATIVE SENSOR fresh=true (emulated hardware)\n");
            main(()->((Button)get(field("motionPanel"),"calibrate")).performClick());waitFor(()->((Button)get(field("motionPanel"),"start")).isEnabled(),"real virtual sensor calibration is ready");
            main(()->((Button)get(field("motionPanel"),"start")).performClick());waitFor(()->motionArmed(),"native stream can be manually armed");snap("motion-native-ready");main(()->((Button)get(field("motionPanel"),"start")).performClick());
        }
        boolean[] available={true},running={false};Class<?> sourceType=Class.forName("cn.fpgasynth.control.MotionSource",true,a.getClassLoader());
        Object source=Proxy.newProxyInstance(a.getClassLoader(),new Class<?>[]{sourceType},(proxy,method,args)->{
            switch(method.getName()){case "available":return available[0];case "running":return running[0];case "start":running[0]=available[0];return running[0];case "stop":running[0]=false;return null;case "source":return "instrumentation fixtures";default:throw new IllegalStateException(method.getName());}
        });
        main(()->{call(nativeSource,"stop",new Class<?>[]{});Field f=a.getClass().getDeclaredField("motionSensors");f.setAccessible(true);f.set(a,source);call(a,"syncMotion",new Class<?>[]{});});
        check((Boolean)field("motionMode")&&((View)field("motionPanel")).isShown(),"motion is the third surface in the same APK");
        check(!motionArmed()&&n("localMask")==0,"selecting motion never starts a note");
        armMotion();motionSample(10);waitFor(()->n("ownerMask")==4096&&n("localMask")==4096,"sensor frame reaches real session/model at C4");snap("motion-playing");
        waitFor(()->n("ownerMask")==0&&n("localMask")==0,"motion gate automatically sends NoteOff");
        waitFor(()->!motionArmed(),"stale sensor stream disarms");motionSample(0);SystemClock.sleep(80);check(!motionArmed()&&n("localMask")==0,"fresh data cannot silently resume");
        armMotion();motionSample(10);waitFor(()->n("localMask")==4096,"motion started before surface switch");chooseInstrument(0);waitFor(()->n("ownerMask")==0&&!running[0]&&!motionArmed(),"leaving motion releases and unregisters");
        View keyboard=(View)field("keyboard");event(keyboard,MotionEvent.ACTION_DOWN,new int[]{91},new float[]{1f/30},new float[]{.82f});waitFor(()->n("ownerMask")==1,"keyboard still works");event(keyboard,MotionEvent.ACTION_UP,new int[]{91},new float[]{1f/30},new float[]{.82f});waitFor(()->n("ownerMask")==0,"keyboard release");
        available[0]=false;chooseInstrument(2);check(!((Button)get(field("motionPanel"),"start")).isEnabled()&&!running[0],"missing sensors disable motion without enabling a fake source");snap("motion-unavailable");
        available[0]=true;chooseInstrument(0);chooseInstrument(2);armMotion();motionSample(10);waitFor(()->n("localMask")==4096,"motion held before invalid frame");
        motionSample(Double.NaN);waitFor(()->!motionArmed()&&n("localMask")==0,"invalid sensor frame stops immediately");motionSample(0);check(!motionArmed(),"valid data after a fault needs explicit rearm");
        // Fault recovery retains the 300ms anti-repeat gate; keep quiet frames fresh while it expires.
        long quietUntil=SystemClock.elapsedRealtime()+320;while(SystemClock.elapsedRealtime()<quietUntil){motionSample(0);SystemClock.sleep(30);}
        armMotion();motionSample(10);waitFor(()->n("localMask")==4096,"motion held before pause");
        main(()->call(a,"onPause",new Class<?>[]{}));waitFor(()->!running[0]&&n("localMask")==0&&!motionArmed(),"background unregisters and clears motion");
        main(()->call(a,"onResume",new Class<?>[]{}));motionSample(0);check(!motionArmed(),"resume requires manual start");
        pass("motion: real Android adapter capability; injected sensor frames through UI/session, explicit arm, gate off, stale disarm, no auto-rearm, switching, unavailable sensors and pause safety");
    }
    private void chooseMidiRoll(int index){
        main(()->((Button)get(field("motionPanel"),"mapping")).performClick());waitForIdleSync();
        main(()->{AlertDialog dialog=(AlertDialog)field("midiMappingDialog");check(dialog.isShowing(),"MIDI mapping dialog opens");dialog.getListView().performItemClick(dialog.getListView().getChildAt(0),0,0);});waitForIdleSync();
        main(()->{AlertDialog dialog=(AlertDialog)field("midiMappingDialog");dialog.getListView().performItemClick(dialog.getListView().getChildAt(index),index,index);});waitFor(()->a.hasWindowFocus(),"mapping chooser returns focus");waitForIdleSync();
    }
    private void midiProbe()throws Exception{
        check(field("midiMapper")!=null,"sensor MIDI mapping layer exists");motionProbe();chooseMidiRoll(3);check(((Number)field("rollMapping")).intValue()==7,"roll configured as CC7 through native settings");
        armMotion();waitFor(()->volume()==128,"center tilt CC7=64 reaches the old control adapter and actual model");
        for(int i=0;i<20;i++){motionSample(35,0,0);SystemClock.sleep(20);}waitFor(()->volume()==254,"positive tilt reaches CC7=127");
        Object message=field("lastMidiMessage");byte[] bytes=(byte[])call(message,"bytes",new Class<?>[]{});check(bytes.length==3&&(bytes[0]&255)==0xb0&&(bytes[1]&255)==7&&(bytes[2]&255)==127,"sensor mapping produces actual B0 07 7F MIDI bytes");snap("midi-cc7");
        armMotion();motionSample(10);waitFor(()->n("ownerMask")==4096,"CC tilt mode still produces a fixed C4 MIDI NoteOn on a strike");
        main(()->((Button)get(field("motionPanel"),"start")).performClick());waitFor(()->n("ownerMask")==0,"MIDI stop releases note");
        message=field("lastMidiMessage");check(((Number)get(message,"status")).intValue()==0x80&&((Number)get(message,"data1")).intValue()==60,"explicit MIDI NoteOff generated");
        chooseMidiRoll(1);waitFor(()->yes("canSwitch")&&((Button)field("ks")).isEnabled(),"engine switch ready");main(()->((Button)field("ks")).performClick());waitFor(()->n("applied")==1&&yes("playable"),"KS selected");motionSample(0);
        check(!((Button)get(field("motionPanel"),"start")).isEnabled(),"unsupported CC74 mapping cannot start on KS");motionSample(10);check(n("localMask")==0,"unsupported mapping sends no notes or CC");
        chooseMidiRoll(0);armMotion();motionSample(10);waitFor(()->n("ownerMask")==4096,"note MIDI mapping works on KS");main(()->((Button)get(field("motionPanel"),"start")).performClick());waitFor(()->n("ownerMask")==0,"KS MIDI note stops");
        waitFor(()->yes("canSwitch")&&((Button)field("dds")).isEnabled(),"return to subtractive ready");main(()->((Button)field("dds")).performClick());waitFor(()->yes("playable")&&n("applied")==0,"subtractive adopted");
        main(()->{call(field("motionSensors"),"stop",new Class<?>[]{});Field f=a.getClass().getDeclaredField("motionSensors");f.setAccessible(true);f.set(a,originalMotionSource);call(a,"syncMotion",new Class<?>[]{});});chooseMidiRoll(3);
        waitFor(()->field("motionFrame")!=null&&(Boolean)get(field("motionFrame"),"fresh"),"restored real virtual sensor source is fresh");
        main(()->((Button)get(field("motionPanel"),"calibrate")).performClick());waitFor(()->((Button)get(field("motionPanel"),"start")).isEnabled(),"native MIDI calibration ready");main(()->((Button)get(field("motionPanel"),"start")).performClick());waitFor(()->motionArmed()&&volume()==128,"native sensor neutral tilt generates CC7=64 without injected frames");snap("midi-native-ready");
        main(()->((Button)get(field("motionPanel"),"start")).performClick());chooseMidiRoll(0);
        pass("sensor MIDI: native mapping settings, B0 07 7F bytes, CC7 device adoption, fixed C4 NoteOn/Off, unsupported CC74 gate and KS note mapping");
        pass("native Android virtual sensor stream also generates CC7=64 and reaches the model without injected frames");
    }
    private void unifiedProbe()throws Exception{
        check(!pads,"unified delivery runs in the main application package");
        Button swap=(Button)field("surfaceChoice");Rect visible=new Rect();
        check(swap.isShown()&&swap.getGlobalVisibleRect(visible)&&visible.height()>=swap.getHeight()-2,"main app exposes a visible keyboard/Pad switch");
        View keyboard=(View)field("keyboard"),pad=(View)field("pads");int token=n("token");long initialVolume=volume();Object route=field("modeChoice");
        event(keyboard,MotionEvent.ACTION_DOWN,new int[]{19},new float[]{1f/30},new float[]{.82f});waitFor(()->n("ownerMask")==1,"keyboard adopted before switching");
        chooseInstrument(1);waitFor(()->n("ownerMask")==0&&n("localMask")==0,"switch to Pad releases old keyboard notes");waitForIdleSync();
        check((Boolean)field("padMode")&&pad.isShown()&&!keyboard.isShown(),"Pad is playable within the main package");
        check(field("session")==session&&n("token")==token&&field("modeChoice")==route&&volume()==initialVolume&&n("applied")==0,"surface switch preserves the session, route, engine and patch");
        event(keyboard,MotionEvent.ACTION_MOVE,new int[]{19},new float[]{3f/30},new float[]{.82f});SystemClock.sleep(80);check(n("localMask")==0,"old keyboard move cannot revive a note");
        snap("unified-pad");layoutProbe();
        int columns=((Number)get(pad,"columns")).intValue();float x=.5f/columns,x2=1.5f/columns,y=columns==8?.75f:.875f;
        event(pad,MotionEvent.ACTION_DOWN,new int[]{7},new float[]{x},new float[]{y});
        event(pad,MotionEvent.ACTION_POINTER_DOWN|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),new int[]{7,83},new float[]{x,x2},new float[]{y,y});waitFor(()->n("ownerMask")==3,"main package adopts two distinct Pad notes");
        event(pad,MotionEvent.ACTION_POINTER_UP,new int[]{7,83},new float[]{x,x2},new float[]{y,y});waitFor(()->n("ownerMask")==2,"Pad ID survives pointer reindexing");
        chooseInstrument(0);waitFor(()->n("ownerMask")==0&&n("localMask")==0,"switch to keyboard releases the held Pad");waitForIdleSync();
        check(keyboard.isShown()&&!pad.isShown()&&field("session")==session&&n("token")==token&&volume()==initialVolume,"keyboard returns with the same patch and session");
        event(pad,MotionEvent.ACTION_MOVE,new int[]{83},new float[]{x2},new float[]{y});SystemClock.sleep(80);check(n("localMask")==0,"old Pad move cannot revive a note");
        event(keyboard,MotionEvent.ACTION_DOWN,new int[]{91},new float[]{1f/30},new float[]{.82f});waitFor(()->n("ownerMask")==1,"fresh keyboard gesture works after round trip");
        event(keyboard,MotionEvent.ACTION_UP,new int[]{91},new float[]{1f/30},new float[]{.82f});waitFor(()->n("ownerMask")==0,"fresh keyboard note releases");snap("unified-keyboard");
        main(()->{call(a,"select",new Class<?>[]{int.class},0);report.append("SWITCH_GATE after navigation: ").append(yes("canSwitch")).append('\n');});
        waitFor(()->yes("canSwitch")&&((Button)field("ks")).isEnabled(),"engine switch waits for the navigation release acknowledgement");
        main(()->((Button)field("ks")).performClick());waitFor(()->yes("playable")&&n("applied")==1,"KS ready before surface switch");
        int ksToken=n("token");chooseInstrument(1);waitForIdleSync();check((Boolean)field("padMode")&&n("applied")==1&&n("token")==ksToken,"Pad shares the selected KS engine without reconnecting");
        waitFor(()->yes("playable")&&(Boolean)get(pad,"playable"),"KS Pad waits for surface release acknowledgement before a fresh gesture");
        event(pad,MotionEvent.ACTION_DOWN,new int[]{92},new float[]{x},new float[]{y});waitFor(()->n("ownerMask")==1,"Pad plays the shared KS session");
        chooseInstrument(0);waitFor(()->n("ownerMask")==0&&n("localMask")==0,"KS Pad releases when returning to keyboard");
        pass("single main APK: visible keyboard/Pad switch, real multi-pointer notes, old gesture rejection, shared patch/KS/route/session and safe release");
    }
    private void event(View target,int action,int[] ids,float[] xs,float[] ys){
        MotionEvent.PointerProperties[] props=new MotionEvent.PointerProperties[ids.length];MotionEvent.PointerCoords[] coords=new MotionEvent.PointerCoords[ids.length];
        for(int i=0;i<ids.length;i++){props[i]=new MotionEvent.PointerProperties();props[i].id=ids[i];props[i].toolType=MotionEvent.TOOL_TYPE_FINGER;coords[i]=new MotionEvent.PointerCoords();coords[i].x=xs[i]*target.getWidth();coords[i].y=ys[i]*target.getHeight();coords[i].pressure=1;coords[i].size=1;}
        long now=SystemClock.uptimeMillis();MotionEvent e=MotionEvent.obtain(now,now,action,ids.length,props,coords,0,0,1,1,0,0,InputDevice.SOURCE_TOUCHSCREEN,0);
        main(()->target.dispatchTouchEvent(e));e.recycle();
    }
    private void snap(String name)throws Exception{waitForIdleSync();SystemClock.sleep(400);Bitmap image=getUiAutomation().takeScreenshot();check(image!=null,"screenshot");File file=new File(getTargetContext().getExternalFilesDir(null),"console-"+profile+"-"+name+".png");try(FileOutputStream out=new FileOutputStream(file)){image.compress(Bitmap.CompressFormat.PNG,100,out);}image.recycle();}
    @Override public void onCreate(Bundle args){super.onCreate(args);profile=args.getString("profile","tablet");start();}
    @Override public void onStart(){Bundle result=new Bundle();try{
        String pkg=getTargetContext().getPackageName();pads=pkg.endsWith(".pads");a=startActivitySync(new Intent().setClassName(pkg,"cn.fpgasynth.control.MainActivity").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));session=field("session");
        check(field("performance")!=null,"performance feedback panel exists");check(((Number)field("selected")).intValue()==0,"starts at console instead of a setup form");check(n("token")==0,"no automatic hardware connection");
        snap("01-idle");main(()->((Button)field("demoQuick")).performClick());waitFor(()->yes("playable"),"demo ready");waitFor(()->a.hasWindowFocus(),"window focus");waitForIdleSync();snap("02-console");pass("console starts offline without connecting and exposes an explicit demo action");
        if(profile.startsWith("midi")){midiProbe();result.putString("result","PASS");result.putString("report",report.toString());finish(Activity.RESULT_OK,result);return;}
        if(profile.startsWith("motion")){motionProbe();result.putString("result","PASS");result.putString("report",report.toString());finish(Activity.RESULT_OK,result);return;}
        if(profile.startsWith("unified")){unifiedProbe();result.putString("result","PASS");result.putString("report",report.toString());finish(Activity.RESULT_OK,result);return;}
        if(profile.startsWith("layout")){layoutProbe();result.putString("result","PASS");result.putString("report",report.toString());finish(Activity.RESULT_OK,result);return;}
        if(profile.startsWith("precision")){preciseWhileDisabled();result.putString("result","PASS");result.putString("report",report.toString());finish(Activity.RESULT_OK,result);return;}
        View surface=(View)field(pads?"pads":"keyboard");check(surface.getVisibility()==View.VISIBLE,"edition selects visible surface");
        float x=pads?.125f:1f/30,y=pads?.875f:.82f;
        event(surface,MotionEvent.ACTION_DOWN,new int[]{9},new float[]{x},new float[]{y});waitFor(()->n("ownerMask")==1,"first note adopted");
        Object scene=call(field("feedbackModel"),"snapshot",new Class<?>[]{long.class},SystemClock.elapsedRealtime());check(((Number)get(scene,"localCount")).intValue()==1&&((Number)get(scene,"adoptedCount")).intValue()==1,"feedback uses local and actual owner");check(!(Boolean)get(scene,"audioAvailable"),"demo never fakes an audio meter");snap("03-held-note");
        event(surface,MotionEvent.ACTION_POINTER_DOWN|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),new int[]{9,42},new float[]{x,x},new float[]{y,y});
        event(surface,MotionEvent.ACTION_POINTER_UP,new int[]{9,42},new float[]{x,x},new float[]{y,y});SystemClock.sleep(80);check(n("localMask")==1,"same note second pointer retained");
        event(surface,MotionEvent.ACTION_UP,new int[]{42},new float[]{x},new float[]{y});waitFor(()->n("ownerMask")==0,"last pointer releases");pass("real MotionEvents: stable IDs, duplicate-note hold, adoption indicators and release");
        event(surface,MotionEvent.ACTION_DOWN,new int[]{19},new float[]{x},new float[]{y});waitFor(()->n("ownerMask")==1,"held before page switch");
        main(()->call(a,"select",new Class<?>[]{int.class},1));waitFor(()->n("ownerMask")==0&&n("localMask")==0,"page switch clears old keys");
        check(surface.getVisibility()==View.VISIBLE,"keyboard/pads stay available during sound editing");snap("04-sound");
        check(((View)field("content")).getHeight()>0,"sound controls retain a nonzero viewport after leaving performance focus");
        main(()->call(a,"select",new Class<?>[]{int.class},0));waitForIdleSync();pass("sound editing retains an instrument dock and discards old gestures on navigation");
        if(pads){
            event(surface,MotionEvent.ACTION_DOWN,new int[]{7},new float[]{.125f},new float[]{.875f});
            event(surface,MotionEvent.ACTION_POINTER_DOWN|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),new int[]{7,83},new float[]{.125f,.375f},new float[]{.875f,.875f});waitFor(()->n("ownerMask")==3,"two distinct pads adopted");
            event(surface,MotionEvent.ACTION_POINTER_UP,new int[]{7,83},new float[]{.125f,.375f},new float[]{.875f,.875f});waitFor(()->n("ownerMask")==2,"pad ID survives index change");
            event(surface,MotionEvent.ACTION_MOVE,new int[]{83},new float[]{-.1f},new float[]{.875f});waitFor(()->n("ownerMask")==0,"leaving pad releases");
            event(surface,MotionEvent.ACTION_CANCEL,new int[]{83},new float[]{.375f},new float[]{.875f});
            main(()->call(a,"chooseSurface",new Class<?>[]{boolean.class},false));check(((View)field("keyboard")).getVisibility()==View.VISIBLE&&n("localMask")==0,"surface switch clears notes");pass("Pad edition: distinct notes, pointer reindexing, leaving/cancel, switch to keyboard");
        }
        if(((View)field("content")).getVisibility()==View.GONE){main(()->((Button)field("focusButton")).performClick());waitForIdleSync();}
        Object target=null;for(Object row:(List<?>)field("rows"))if(((Number)get(get(row,"control"),"cc")).intValue()==7&&((View)get(row,"box")).isShown()){target=row;break;}check(target!=null,"master level control");Object row=target;View knob=(View)get(row,"knob");
        main(()->{knob.requestRectangleOnScreen(new Rect(0,0,knob.getWidth(),knob.getHeight()),true);});waitForIdleSync();
        main(()->{Rect visible=new Rect();check(knob.getGlobalVisibleRect(visible)&&visible.height()>40&&visible.width()>40,"dial is visible to the user before injecting a drag");});
        int original=((Number)call(knob,"value",new Class<?>[]{})).intValue();
        event(knob,MotionEvent.ACTION_DOWN,new int[]{0},new float[]{.5f},new float[]{.5f});check(((Number)call(knob,"value",new Class<?>[]{})).intValue()==original,"dial touch does not jump");
        event(knob,MotionEvent.ACTION_MOVE,new int[]{0},new float[]{.5f},new float[]{.1f});event(knob,MotionEvent.ACTION_UP,new int[]{0},new float[]{.5f},new float[]{.1f});
        int requested=((Number)call(knob,"value",new Class<?>[]{})).intValue();check(requested>original,"upward relative drag increases control");
        waitFor(()->((Number)call(get(view(),"state"),"n",new Class<?>[]{String.class},"volume")).longValue()==requested*2L,"final control value adopted");
        main(()->knob.performAccessibilityAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD,null));
        waitFor(()->((Number)call(get(view(),"state"),"n",new Class<?>[]{String.class},"volume")).longValue()==(requested-1)*2L,"accessible fine adjustment adopted");pass("relative dial drag, mandatory trailing value and accessible fine adjustment reach real session/model");
        main(()->{((Button)field("ks")).performClick();});waitFor(()->yes("playable")&&n("applied")==1,"KS mode adopted");snap("05-ks");
        main(()->{((Button)field("wifiMode")).performClick();call(a,"select",new Class<?>[]{int.class},2);});waitForIdleSync();check(n("localMask")==0,"connection page releases notes");snap("06-connect");pass("engine and connection navigation retain safe note release");
        result.putString("result","PASS");result.putString("report",report.toString());finish(Activity.RESULT_OK,result);
    }catch(Throwable e){java.io.StringWriter trace=new java.io.StringWriter();e.printStackTrace(new java.io.PrintWriter(trace));result.putString("result","FAIL");result.putString("report",report+trace.toString());finish(Activity.RESULT_CANCELED,result);}}
}
