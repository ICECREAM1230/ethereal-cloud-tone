package cn.fpgasynth.control.tests;

import android.app.*;
import android.content.*;
import android.graphics.Bitmap;
import android.os.*;
import android.view.*;
import android.widget.*;
import cn.fpgasynth.control.core.*;
import java.io.*;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Dedicated emulator + explicit RAM owner fixture. No BLE radio, network or hardware. */
public final class Ae350Smoke extends Instrumentation {
    private Activity activity;private Bundle arguments;private int assertions;
    private interface Condition {boolean ok()throws Exception;}
    private void check(boolean value,String why){assertions++;if(!value)throw new AssertionError(why);}
    private void ui(Runnable run){runOnMainSync(run);}
    private Object field(String name)throws Exception{Field f=activity.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(activity);}
    private void call(String name,Class<?>[] types,Object... values)throws Exception{Method m=activity.getClass().getDeclaredMethod(name,types);m.setAccessible(true);m.invoke(activity,values);}
    private void waitFor(Condition condition,String why)throws Exception{long end=SystemClock.elapsedRealtime()+8000;while(SystemClock.elapsedRealtime()<end){if(condition.ok())return;SystemClock.sleep(30);}throw new AssertionError("timeout: "+why);}
    private String asset(String name)throws IOException{try(InputStream in=getContext().getAssets().open("ae350/"+name);ByteArrayOutputStream out=new ByteArrayOutputStream()){byte[] b=new byte[4096];int n;while((n=in.read(b))>=0)out.write(b,0,n);return new String(out.toByteArray(),StandardCharsets.UTF_8);}}
    private final class Owner implements ControlPort {
        final Map<String,Object> caps=Json.object(asset("capabilities.json"));
        final Map<String,Object> state=Json.object(asset("state-initial-client-ks.json"));
        final Mode mode;long token=1000;int sessions;volatile boolean closed;final List<long[]> commands=new ArrayList<>();
        Owner(Mode mode)throws IOException{this.mode=mode;for(Object row:Json.list(state.get("clients"))){Json.map(row).put("token",0L);Json.map(row).put("keys",0L);}}
        Map<String,Object> client(){return Json.map(Json.list(state.get("clients")).get(2));}
        public Mode mode(){return mode;}
        public void close(){closed=true;}
        synchronized long keys(){return Json.number(client().get("keys"));}
        synchronized int route(){return (int)Json.number(client().get("midi_channel"));}
        public synchronized Response request(String path,String body)throws IOException{
            if(closed)throw new IOException("fixture port closed");
            if(path.equals("/api/v2/capabilities"))return new Response(200,Json.stringify(caps));
            if(path.equals("/api/v2/session")){
                check("".equals(body),"session empty POST");sessions++;token++;for(String key:new ArrayList<>(client().keySet()))client().put(key,0L);client().put("token",token);client().put("midi_channel",1L);
                return new Response(201,"{\"api_major\":2,\"api_minor\":0,\"token\":"+token+",\"client\":2,\"lease_ms\":1200,\"context_epoch\":9}");
            }
            if(path.equals("/api/v2/state"))return new Response(200,Json.stringify(state));
            if(path.equals("/api/v2/command")){
                String[] words=body.split(" ");check(words.length==7,"seven integer command");long[] w=new long[7];for(int i=0;i<7;i++)w[i]=Long.parseLong(words[i]);commands.add(w);
                check(w[0]==token&&w[2]==9,"token/context retained");Map<String,Object> c=client();c.put("received",w[1]);c.put("result_sequence",w[1]);c.put("result_context_epoch",9L);c.put("result_code",0L);
                if(w[3]==3){c.put("processed",w[1]);c.put("keys",w[4]);}
                else{c.put(w[3]==4?"stop_processed":"parameter_processed",w[1]);if(w[3]==5){check(w[5]==0,"route backend zero");c.put("midi_channel",w[4]);}}
                return new Response(202,"{\"api_major\":2,\"accepted\":"+w[1]+",\"context_epoch\":9,\"stage\":\"received\",\"fpga\":\"unverified\"}");
            }
            return new Response(404,"{}");
        }
    }
    @Override public void onCreate(Bundle args){arguments=args;start();}
    private void touch(View target,int action){long now=SystemClock.uptimeMillis();MotionEvent event=MotionEvent.obtain(now,now,action,target.getWidth()*.04f,target.getHeight()*.75f,0);target.dispatchTouchEvent(event);event.recycle();}
    private void snapshot(String name)throws IOException{
        Bitmap shot=getUiAutomation().takeScreenshot();if(shot==null)throw new IOException("no screenshot");File f=new File(getTargetContext().getExternalFilesDir(null),"ae350-"+arguments.getString("profile","tablet")+"-"+name+".png");try(FileOutputStream out=new FileOutputStream(f)){shot.compress(Bitmap.CompressFormat.PNG,100,out);}shot.recycle();
    }
    @Override public void onStart(){Bundle result=new Bundle();try{
        Intent launch=getTargetContext().getPackageManager().getLaunchIntentForPackage("cn.fpgasynth.control");launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK);activity=startActivitySync(launch);waitForIdleSync();
        AppController session=(AppController)field("session");ControlPorts ports=(ControlPorts)field("ports");
        ui(()->{try{call("select",new Class[]{int.class},2);((Button)field("bluetoothMode")).performClick();}catch(Exception e){throw new RuntimeException(e);}});
        waitFor(()->ports.selected()==ControlPort.Mode.BLUETOOTH&&!((Boolean)field("connectionBusy")),"Bluetooth selection");
        check(!ports.isOpen(),"mode alone does not connect");
        Owner owner=new Owner(ControlPort.Mode.BLUETOOTH);
        Class<?> factory=Class.forName(activity.getClass().getName()+"$PortFactory");Object adapter=java.lang.reflect.Proxy.newProxyInstance(factory.getClassLoader(),new Class<?>[]{factory},(proxy,method,args)->owner);
        ui(()->{try{call("queuePortAction",new Class[]{factory,boolean.class},adapter,false);}catch(Exception e){throw new RuntimeException(e);}});
        waitFor(()->session.view().playable,"AE350 initial empty handshake");waitForIdleSync();
        check(((Button)field("ks")).isSelected(),"KS route highlighted independently of SUB editor");check(((Button)field("fm")).isEnabled(),"FM route enabled without legacy native descriptor");
        check(((TextView)field("engineInfo")).getText().toString().contains("MIDI 2"),"human channel2 label");
        long token=session.view().token;
        for(int engine:new int[]{2,0,1,2}){
            String key=engine==0?"dds":engine==1?"ks":"fm";ui(()->{try{((Button)field(key)).performClick();}catch(Exception e){throw new RuntimeException(e);}});
            waitFor(()->owner.route()==engine&&session.view().playable,"route "+engine);waitForIdleSync();check(session.view().token==token&&owner.sessions==1,"route switch retains token");check(((Button)field(key)).isSelected(),"selected route visible");
        }
        check(session.view().applied==0&&!session.view().fmEditable,"global editor and native editing capability preserved");
        ui(()->{try{call("select",new Class[]{int.class},0);}catch(Exception e){throw new RuntimeException(e);}});waitForIdleSync();
        TextView routeLabel=(TextView)field("engineInfo");check(routeLabel.getVisibility()==View.VISIBLE&&routeLabel.getGlobalVisibleRect(new android.graphics.Rect())&&routeLabel.getText().toString().contains("MIDI 3")&&routeLabel.getText().toString().contains("编辑 减法"),"performance and editor label must be visible, not just populated");
        View keyboard=(View)field("keyboard");waitFor(()->keyboard.getWidth()>0&&keyboard.getHeight()>0,"keyboard measured");ui(()->touch(keyboard,MotionEvent.ACTION_DOWN));waitFor(()->owner.keys()!=0,"real touch NoteOn through fixture port");snapshot("fm-held");ui(()->touch(keyboard,MotionEvent.ACTION_UP));waitFor(()->owner.keys()==0,"real touch NoteOff");
        ui(()->{try{call("select",new Class[]{int.class},2);((Button)field("wifiMode")).performClick();}catch(Exception e){throw new RuntimeException(e);}});
        waitFor(()->owner.closed&&ports.selected()==ControlPort.Mode.WIFI&&!ports.isOpen(),"mutually exclusive switch closes BLE fixture");
        check(owner.keys()==0,"port switch releases source");
        ui(()->{try{((Button)field("bluetoothMode")).performClick();}catch(Exception e){throw new RuntimeException(e);}});waitFor(()->ports.selected()==ControlPort.Mode.BLUETOOTH&&!((Boolean)field("connectionBusy")),"back to unconnected BLE");
        ui(()->{try{
            LinkedHashMap<String,Button> devices=new LinkedHashMap<>();LinearLayout body=new LinearLayout(activity);body.setOrientation(LinearLayout.VERTICAL);
            Class<?>[] types={LinkedHashMap.class,LinearLayout.class,String.class,String.class,String.class};String mac="AA:BB:CC:DD:EE:FF";
            call("addBleCandidate",types,devices,body,"FPGA_Bluetooth",mac,"已配对 · 待验证");call("addBleCandidate",types,devices,body,"FPGA_Bluetooth",mac,"系统已连接 · 待验证");
            check(body.getChildCount()==1&&devices.size()==1,"known candidate deduplicated by address");check(devices.get(mac).getText().toString().contains("待验证"),"system connection is not App readiness");devices.get(mac).performClick();
            check(((EditText)field("bluetoothAddress")).getText().toString().equals(mac)&&!ports.isOpen(),"candidate selects address without connection");
        }catch(Exception e){throw new RuntimeException(e);}});
        snapshot("bluetooth-candidate");result.putString("result","PASS");result.putString("evidence",assertions+" assertions; native UI/fixture owner; NO RF/ESP32/SPI/audio");
    }catch(Throwable error){StringWriter text=new StringWriter();error.printStackTrace(new PrintWriter(text));result.putString("result","FAIL");result.putString("error",text.toString());}finally{if(activity!=null)ui(()->activity.finish());finish(0,result);}}
}
