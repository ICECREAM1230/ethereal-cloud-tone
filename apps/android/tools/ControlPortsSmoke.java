package cn.fpgasynth.control.tests;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import cn.fpgasynth.control.core.AppController;
import cn.fpgasynth.control.core.ControlPorts;
import cn.fpgasynth.control.core.ControlPort;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BooleanSupplier;

/** Dedicated emulator only. Permission callbacks are simulated; no BLE scan or GATT is started. */
public final class ControlPortsSmoke extends Instrumentation {
    private Activity activity;
    private final StringBuilder report=new StringBuilder();
    private Object field(String name)throws Exception{Field f=activity.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(activity);}
    private void set(String name,Object value)throws Exception{Field f=activity.getClass().getDeclaredField(name);f.setAccessible(true);f.set(activity,value);}
    private void invoke(String name,Class<?>[] types,Object...values)throws Exception{Method m=activity.getClass().getDeclaredMethod(name,types);m.setAccessible(true);m.invoke(activity,values);}
    private static void check(boolean condition,String message){if(!condition)throw new AssertionError(message);}
    private void main(Action action){runOnMainSync(()->{try{action.run();}catch(Exception e){throw new RuntimeException(e);}});}
    private interface Action {void run()throws Exception;}
    private void waitFor(BooleanSupplier p,String why){long end=SystemClock.elapsedRealtime()+5000;while(!p.getAsBoolean()&&SystemClock.elapsedRealtime()<end)SystemClock.sleep(25);check(p.getAsBoolean(),why);}
    private void snap(String name)throws Exception{
        waitForIdleSync();SystemClock.sleep(500);Bitmap b=getUiAutomation().takeScreenshot();check(b!=null,"screenshot");
        File f=new File(getTargetContext().getExternalFilesDir(null),"ports-"+name+".png");try(FileOutputStream out=new FileOutputStream(f)){b.compress(Bitmap.CompressFormat.PNG,100,out);}b.recycle();
    }
    private void pass(String text){report.append("PASS ").append(text).append('\n');}
    @Override public void onCreate(Bundle args){super.onCreate(args);start();}
    @Override public void onStart(){
        Bundle result=new Bundle();
        try{
            Intent launch=new Intent().setClassName("cn.fpgasynth.control","cn.fpgasynth.control.MainActivity").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            activity=startActivitySync(launch);
            main(()->((Button[])field("tabs"))[2].performClick());
            AppController session=(AppController)field("session");ControlPorts ports=(ControlPorts)field("ports");
            check(ports.selected()==ControlPort.Mode.WIFI&&!ports.isOpen()&&session.view().token==0,"startup never opens a physical port");
            main(()->{check(((View)field("wifiSection")).getVisibility()==View.VISIBLE&&((View)field("bluetoothSection")).getVisibility()==View.GONE,"Wi-Fi form alone visible");});
            snap("01-wifi-tablet");
            main(()->((Button)field("bluetoothMode")).performClick());
            waitFor(()->ports.selected()==ControlPort.Mode.BLUETOOTH&&!ports.isOpen(),"select Bluetooth without connection");waitForIdleSync();
            main(()->{check(((View)field("wifiSection")).getVisibility()==View.GONE&&((View)field("bluetoothSection")).getVisibility()==View.VISIBLE,"Bluetooth form alone visible");
                check(((Button)field("bluetoothMode")).isSelected()&&!((Button)field("wifiMode")).isSelected(),"mode toggles are exclusive");
                check(field("permissionAction")==null,"mode selection does not request permission");});
            snap("02-bluetooth-tablet");pass("installed APK: exclusive connection forms, selected mode, no automatic connection or permission request");
            AtomicLong intent=(AtomicLong)field("connectionIntent");long before=intent.get();
            main(()->{set("permissionAction","connect");set("permissionIntent",before);callActivityOnPause(activity);});
            check(intent.get()==before,"system authorization pause must preserve the pending operation generation");
            main(()->invoke("onActivityResult",new Class<?>[]{int.class,int.class,Intent.class},702,Activity.RESULT_CANCELED,null));
            check(field("permissionAction")!=null,"result while paused must be deferred until foreground");
            main(()->callActivityOnResume(activity));
            main(()->check(field("permissionAction")==null&&((TextView)field("portNotice")).getText().toString().contains("取消"),"enable cancellation produces a visible retry notice"));
            pass("system-enable pause/result/resume keeps pending intent and shows cancellation without background work");
            main(()->{set("permissionAction","scan");set("permissionIntent",intent.get());callActivityOnPause(activity);
                activity.onRequestPermissionsResult(701,new String[]{"android.permission.BLUETOOTH_SCAN"},new int[]{-1});});
            check(field("permissionAction")!=null,"permission denial while paused is deferred");main(()->callActivityOnResume(activity));
            main(()->check(field("permissionAction")==null&&((TextView)field("portNotice")).getText().toString().contains("权限"),"permission denial explains retry"));
            pass("first permission denial is resolved in foreground without scan or connection");
            main(()->{set("permissionAction","scan");set("permissionIntent",intent.get());callActivityOnPause(activity);
                invoke("onActivityResult",new Class<?>[]{int.class,int.class,Intent.class},702,Activity.RESULT_OK,null);});
            check(field("permissionAction")!=null,"successful result is deferred while paused");
            main(()->{invoke("onUserLeaveHint",new Class<?>[]{});callActivityOnResume(activity);});
            check(field("permissionAction")==null&&!ports.isOpen(),"user leaving cancels deferred success without starting BLE");
            pass("deferred success cannot revive BLE work after explicit user leave");
            main(()->{set("permissionAction","scan");set("permissionIntent",intent.get());((Button)field("wifiMode")).performClick();});
            waitFor(()->ports.selected()==ControlPort.Mode.WIFI&&!ports.isOpen(),"return to Wi-Fi");
            main(()->{invoke("onActivityResult",new Class<?>[]{int.class,int.class,Intent.class},702,Activity.RESULT_OK,null);
                check(field("permissionAction")==null&&((View)field("bluetoothSection")).getVisibility()==View.GONE,"mode switch cancels pending BLE action");});
            pass("mode change invalidates late authorization callbacks and keeps both ports closed");
            result.putString("result","PASS");result.putString("report",report.toString());finish(Activity.RESULT_OK,result);
        }catch(Throwable e){java.io.StringWriter trace=new java.io.StringWriter();e.printStackTrace(new java.io.PrintWriter(trace));result.putString("result","FAIL");result.putString("report",report+trace.toString());finish(Activity.RESULT_CANCELED,result);}
    }
}
