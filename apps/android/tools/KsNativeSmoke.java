package cn.fpgasynth.control.tests;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import android.widget.Button;
import android.widget.SeekBar;
import android.widget.TextView;
import cn.fpgasynth.control.core.*;
import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicReference;

/** Installed native UI + actual session; replaces only the physical BLE MIDI port.
 * No BLE scanning, RF, remote RPC, audio, or hardware operation. New UI references
 * are reflective so this runner also compiles against 0.8.0 for the required RED.
 */
public final class KsNativeSmoke extends Instrumentation {
    private Activity activity;
    private Bundle args;
    private AppController session;
    private ControlPorts ports;
    private View wood;
    private Port port;
    private int assertions,probeStart,manualCancellationStart=-1;
    private final StringBuilder report=new StringBuilder();
    private static final String TAG="KsNativeSmoke";
    private volatile String currentPhase="created";
    private volatile boolean completed;
    private Thread watchdog;
    private void phase(String value){currentPhase=value;android.util.Log.i(TAG,value);}
    private void idle(String why){phase("idle begin: "+why);waitForIdleSync();phase("idle done: "+why);}
    private void startWatchdog(){
        final Thread instrumentThread=Thread.currentThread();
        watchdog=new Thread(()->{
            try{Thread.sleep(45000);}catch(InterruptedException done){return;}
            if(completed)return;
            android.util.Log.e(TAG,"45s watchdog: phase="+currentPhase+" instrumentation="+instrumentThread.getName());
            try{
                Map<Thread,StackTraceElement[]> stacks=Thread.getAllStackTraces();
                for(Map.Entry<Thread,StackTraceElement[]> entry:stacks.entrySet()){
                    Thread target=entry.getKey();if(target!=instrumentThread&&!"main".equals(target.getName()))continue;
                    android.util.Log.e(TAG,"THREAD "+target.getName()+" id="+target.getId()+" state="+target.getState());
                    for(StackTraceElement frame:entry.getValue())android.util.Log.e(TAG,"    at "+frame);
                }
            }catch(Throwable error){android.util.Log.e(TAG,"watchdog stack collection failed",error);}
        },"KS-native-watchdog");
        watchdog.setDaemon(true);watchdog.start();
    }
    private interface Action {void run()throws Exception;}
    private interface Condition {boolean get()throws Exception;}
    private static final String[] CHORDS={"C","Am","Dm","F","G","Em"};
    private static final int[][] NOTES={
        {67,64,60,55,52,48},{72,69,64,60,57,52},{69,65,62,57,53,50},
        {69,65,60,57,53,48},{71,67,62,59,55,50},{71,67,64,59,55,52}};
    private static final String[] PITCH={"C","C♯","D","D♯","E","F","F♯","G","G♯","A","A♯","B"};
    private static final class Sent {
        final MidiMessage message;final long at;
        Sent(MidiMessage m){message=m;at=SystemClock.elapsedRealtime();}
    }
    private static final class Port implements MidiPort {
        volatile boolean closed;volatile int opens,requests;
        final List<Sent> sent=new ArrayList<>();
        public Mode mode(){return Mode.BLUETOOTH;}
        public void open(){opens++;}
        public void check()throws IOException{if(closed)throw new IOException("closed");}
        public synchronized void send(List<MidiMessage> values,long now)throws IOException{check();for(MidiMessage m:values)sent.add(new Sent(m));}
        public Response request(String path,String body)throws IOException{requests++;throw new IOException("unexpected RPC");}
        public void close(){closed=true;}
        synchronized int size(){return sent.size();}
        synchronized List<Sent> since(int start,int status){List<Sent> values=new ArrayList<>();for(int i=start;i<sent.size();i++)if(sent.get(i).message.status==status)values.add(sent.get(i));return values;}
        synchronized int count(int start,int status,int note){int n=0;for(Sent s:since(start,status))if(s.message.data1==note)n++;return n;}
    }
    private Object field(String name)throws Exception{Field f=activity.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(activity);}
    private void call(String name,Class<?>[] types,Object... values)throws Exception{Method m=activity.getClass().getDeclaredMethod(name,types);m.setAccessible(true);m.invoke(activity,values);}
    private void ui(Action action){AtomicReference<Throwable> error=new AtomicReference<>();runOnMainSync(()->{try{action.run();}catch(Throwable e){error.set(e);}});if(error.get()!=null)throw new RuntimeException(error.get());}
    private void check(boolean ok,String why){assertions++;if(!ok)throw new AssertionError(why);}
    private void waitFor(Condition condition,String why)throws Exception{waitFor(condition,8000,why);}
    private void waitFor(Condition condition,long timeout,String why)throws Exception{phase("wait begin: "+why);long end=SystemClock.elapsedRealtime()+timeout;while(SystemClock.elapsedRealtime()<end){if(condition.get()){phase("wait done: "+why);return;}SystemClock.sleep(15);}diagnostics("timeout: "+why);throw new AssertionError(why+"; report="+report);}
    private void diagnostics(String reason){
        try{
            AppController.View state=session.view();
            report.append("DIAGNOSTIC ").append(reason).append(" at=").append(SystemClock.elapsedRealtime())
                .append(" local=").append(state.localMask).append(" owner=").append(state.ownerMask)
                .append(" queue=").append(state.queueSize).append(" epoch=").append(state.inputEpoch)
                .append(" playable=").append(state.playable).append(" applied=").append(state.performanceApplied).append('\n');
            if(port!=null){synchronized(port){report.append("SENT start=").append(probeStart).append(" total=").append(port.sent.size()).append(" noteOns=").append(port.since(probeStart,0x91).size()).append(" events=");for(int i=probeStart;i<port.sent.size();i++){Sent event=port.sent.get(i);report.append('[').append(event.at).append(' ').append(event.message.description()).append(']');}report.append('\n');}}
            if(wood!=null)ui(()->{
                int[] location=new int[2];wood.getLocationOnScreen(location);
                report.append("WOOD enabled=").append(wood.isEnabled()).append(" shown=").append(wood.isShown())
                    .append(" size=").append(wood.getWidth()).append('x').append(wood.getHeight())
                    .append(" screen=").append(Arrays.toString(location)).append(" clip=").append(visible(wood));
                for(String key:new String[]{"playable","rejected","epoch"}){try{Field f=wood.getClass().getDeclaredField(key);f.setAccessible(true);report.append(' ').append(key).append('=').append(f.get(wood));}catch(Exception missing){report.append(' ').append(key).append("=unavailable");}}
                report.append('\n');AccessibilityNodeProvider provider=wood.getAccessibilityNodeProvider();
                if(provider!=null)for(int id:new int[]{6,12,18,24,30,36}){
                    AccessibilityNodeInfo n=provider.createAccessibilityNodeInfo(id);if(n==null){report.append("CELL ").append(id).append(" missing\n");continue;}
                    Rect local=new Rect(),screen=new Rect();n.getBoundsInParent(local);n.getBoundsInScreen(screen);
                    report.append("CELL ").append(id).append(" label=").append(n.getContentDescription()).append(" local=").append(local).append(" screen=").append(screen)
                        .append(" visible=").append(n.isVisibleToUser()).append(" contained=").append(visible(wood).contains(screen)).append('\n');n.recycle();
                }
            });
        }catch(Throwable diagnosticError){report.append("DIAGNOSTIC unavailable: ").append(diagnosticError).append('\n');}
    }
    private void ready(String why)throws Exception{
        waitFor(()->session.view().playable&&session.view().queueSize==0&&!((Boolean)field("connectionBusy"))&&activity.hasWindowFocus(),why);
        idle(why+" before render");
        // A render posted by the completed IO release must be applied before input.
        phase("render begin: "+why);ui(()->call("render",new Class[]{AppController.View.class},session.view()));idle(why+" after render");
    }
    private void empty(String why)throws Exception{waitFor(()->session.view().localMask==0&&session.view().ownerMask==0&&session.view().queueSize==0,why);}
    private void connect()throws Exception{
        phase("connect begin");
        ui(()->{call("select",new Class[]{int.class},2);((Button)field("bluetoothMode")).performClick();});
        waitFor(()->ports.selected()==ControlPort.Mode.BLUETOOTH&&!((Boolean)field("connectionBusy")),"Bluetooth selected without scan");
        Port next=new Port();Class<?> factory=Class.forName(activity.getClass().getName()+"$PortFactory");
        Object proxy=Proxy.newProxyInstance(factory.getClassLoader(),new Class[]{factory},(p,m,a)->next);
        ui(()->call("queuePortAction",new Class[]{factory,boolean.class},proxy,false));port=next;ready("fake physical link + actual direct session ready");
        check(port.opens==1&&port.requests==0&&session.view().directMidi&&!session.view().demo,"one MIDI open, no demo or RPC");
        ui(()->((Button)field("ks")).performClick());waitFor(()->session.view().applied==1,"KS route applied");ready("KS release barrier complete");
        check(session.view().midiChannel==1,"KS channel is MIDI CH2");
    }
    private AccessibilityNodeInfo node(int id)throws Exception{
        AtomicReference<AccessibilityNodeInfo> result=new AtomicReference<>();
        ui(()->{AccessibilityNodeProvider provider=wood.getAccessibilityNodeProvider();check(provider!=null,"wood has virtual accessibility provider");result.set(provider.createAccessibilityNodeInfo(id));});
        check(result.get()!=null,"virtual node "+id+" exists");return result.get();
    }
    private Rect bounds(int id)throws Exception{AccessibilityNodeInfo n=node(id);Rect r=new Rect();n.getBoundsInParent(r);n.recycle();check(!r.isEmpty(),"node "+id+" has nonempty local bounds");return r;}
    private Rect visible(View view){Rect r=new Rect();view.getGlobalVisibleRect(r);return r;}
    private boolean cellVisible(int id)throws Exception{
        Rect local=bounds(id);AtomicReference<Boolean> result=new AtomicReference<>(false);
        ui(()->{int[] xy=new int[2];wood.getLocationOnScreen(xy);Rect screen=new Rect(local);screen.offset(xy[0],xy[1]);result.set(wood.isShown()&&visible(wood).contains(screen));});return result.get();
    }
    private void expose(int id)throws Exception{phase("expose cell "+id);Rect local=bounds(id);ui(()->wood.requestRectangleOnScreen(local,true));idle("expose cell "+id);}
    private void accessible(int id)throws Exception{ui(()->check(wood.getAccessibilityNodeProvider().performAction(id,AccessibilityNodeInfo.ACTION_CLICK,null),"virtual node "+id+" accepts click"));}
    private long downTime;
    private void event(View target,int action,int[] ids,float[] x,float[] y,boolean begin){
        ui(()->{
            check(target.isShown()&&activity.hasWindowFocus(),"touch target is shown and window focused");
            int[] xy=new int[2];target.getLocationOnScreen(xy);Rect clip=visible(target);
            MotionEvent.PointerProperties[] props=new MotionEvent.PointerProperties[ids.length];MotionEvent.PointerCoords[] coords=new MotionEvent.PointerCoords[ids.length];
            for(int i=0;i<ids.length;i++){
                check(clip.contains(xy[0]+Math.round(x[i]),xy[1]+Math.round(y[i])),"physical pointer "+ids[i]+" is inside global visible clip "+clip);
                props[i]=new MotionEvent.PointerProperties();props[i].id=ids[i];props[i].toolType=MotionEvent.TOOL_TYPE_FINGER;
                coords[i]=new MotionEvent.PointerCoords();coords[i].x=x[i];coords[i].y=y[i];coords[i].pressure=1;coords[i].size=1;
            }
            long now=SystemClock.uptimeMillis();if(begin)downTime=now;
            MotionEvent e=MotionEvent.obtain(downTime,now,action,ids.length,props,coords,0,0,1,1,0,0,InputDevice.SOURCE_TOUCHSCREEN,0);
            if(target==wood&&action==MotionEvent.ACTION_DOWN&&manualCancellationStart>=0){
                int mark=manualCancellationStart;report.append("MANUAL CANCEL inputAt=").append(SystemClock.elapsedRealtime()).append(" firstAutoAt=").append(port.since(mark,0x91).get(0).at)
                    .append(" noteOnsBefore=").append(port.since(mark,0x91).size()).append(" queueBefore=").append(session.view().queueSize).append(" point=").append(x[0]).append(',').append(y[0]).append(" clip=").append(clip).append('\n');
                synchronized(port){report.append("MANUAL CANCEL before events=");for(int i=mark;i<port.sent.size();i++){Sent sent=port.sent.get(i);report.append('[').append(sent.at).append(' ').append(sent.message.description()).append(']');}report.append('\n');}
                check(port.since(mark,0x91).size()==1,"manual cancellation input arrives before second automatic NoteOn");
            }
            check(target.dispatchTouchEvent(e),"native View consumes pointer event");e.recycle();
        });
    }
    private void cellEvent(int id,int action,int pointer,boolean begin)throws Exception{Rect r=bounds(id);event(wood,action,new int[]{pointer},new float[]{r.exactCenterX()},new float[]{r.exactCenterY()},begin);}
    private void tap(int id)throws Exception{
        expose(id);
        if(cellVisible(id)){cellEvent(id,MotionEvent.ACTION_DOWN,29,true);cellEvent(id,MotionEvent.ACTION_UP,29,false);}
        else{accessible(id);report.append("ACCESSIBILITY fallback for clipped cell ").append(id).append('\n');}
    }
    private void snapshot(String name)throws IOException{
        phase("snapshot begin: "+name);
        long captureAt=SystemClock.elapsedRealtime();Bitmap image=getUiAutomation().takeScreenshot();check(image!=null,"screenshot available");
        File target=new File(getTargetContext().getExternalFilesDir(null),"ks-native-"+args.getString("profile","tablet")+"-"+name+".png");
        try(FileOutputStream stream=new FileOutputStream(target)){image.compress(Bitmap.CompressFormat.PNG,100,stream);}image.recycle();report.append("SCREENSHOT ").append(target).append(" durationMs=").append(SystemClock.elapsedRealtime()-captureAt).append('\n');phase("snapshot done: "+name);
    }
    private void layout()throws Exception{
        phase("layout begin");
        // This lookup intentionally makes the unchanged 0.8.0 APK fail RED.
        wood=(View)field("plucked");check(wood.isShown()&&((View)field("ksConsole")).isShown(),"KS shows wood and controls by default");
        check(!((View)field("pianoCard")).isShown(),"KS piano dock starts collapsed");
        check(getTargetContext().getResources().getIdentifier("ks_honey_spruce","drawable","cn.fpgasynth.control")!=0,"original wood resource packaged in APK");
        AccessibilityNodeInfo host=node(View.NO_ID);check(host.getChildCount()==42,"six chord heads and 36 virtual note targets");host.recycle();
        for(int column=0;column<6;column++){
            AccessibilityNodeInfo head=node(column);String label=String.valueOf(head.getContentDescription());check(label.contains(CHORDS[column]),"chord head label "+CHORDS[column]);check(head.isClickable()&&head.isEnabled(),"chord head is actionable");head.recycle();bounds(column);
            for(int row=0;row<6;row++){
                int id=6+row*6+column,note=NOTES[column][row];AccessibilityNodeInfo cell=node(id);label=String.valueOf(cell.getContentDescription());
                check(label.contains(CHORDS[column])&&label.contains(PITCH[note%12]+(note/12-1)),"cell label matches prototype chord + note: "+label);
                check(cell.isClickable()&&cell.isEnabled(),"note cell actionable");cell.recycle();Rect r=bounds(id);check(r.left>=0&&r.top>=0&&r.right<=wood.getWidth()&&r.bottom<=wood.getHeight(),"cell bounds stay within wood");
            }
        }
        check(wood.getContentDescription().toString().contains("已发送"),"wood accessibility describes MIDI sent status");
        String feedback=String.valueOf(((View)field("ksPerformance")).getContentDescription());
        check(feedback.contains("本地触点")&&feedback.contains("已发送")&&feedback.contains("音频数据未接入"),"KS feedback separates local/sent and does not fabricate audio");
        check(!((View)field("decay")).isEnabled(),"BLE T60 stays capability gated");
        android.content.res.Configuration config=activity.getResources().getConfiguration();report.append("WINDOW ").append(config.screenWidthDp).append('x').append(config.screenHeightDp).append("dp font=").append(config.fontScale).append('\n');
        snapshot("default");
        if(!args.getString("profile","tablet").contains("landscape")){
            check(cellVisible(6)&&cellVisible(36),"default tablet/portrait shows all six rows with dock collapsed");
        }
    }
    private void touchAndGlissando()throws Exception{
        phase("touchAndGlissando begin");
        expose(6);int start=port.size();probeStart=start;check(cellVisible(6),"top C cell available for physical native touch");cellEvent(6,MotionEvent.ACTION_DOWN,17,true);
        waitFor(()->port.count(start,0x91,67)==1,"physical high C cell emits G4 channel2");
        // A stationary finger is still a live contact. Its next fresh MOVE must
        // traverse every crossed string even after a gap without motion samples.
        long heldAt=SystemClock.elapsedRealtime();SystemClock.sleep(350);
        report.append("STATIONARY HOLD requestedMs=350 actualMs=").append(SystemClock.elapsedRealtime()-heldAt)
            .append(" downAgeMs=").append(SystemClock.uptimeMillis()-downTime).append('\n');
        if(cellVisible(36)){
            Rect top=bounds(6),bottom=bounds(36);
            // One large MOVE crosses the entire column; an intermediate historical
            // sample also crosses rows. Stable pointer id deliberately is not 0.
            ui(()->{
                MotionEvent.PointerProperties p=new MotionEvent.PointerProperties();p.id=17;p.toolType=MotionEvent.TOOL_TYPE_FINGER;
                MotionEvent.PointerCoords c=new MotionEvent.PointerCoords();c.x=top.exactCenterX();c.y=(top.exactCenterY()+bottom.exactCenterY())*.5f;c.pressure=1;c.size=1;
                long now=SystemClock.uptimeMillis();if(now<=downTime){SystemClock.sleep(1);now=SystemClock.uptimeMillis();}
                report.append("MOVE downAgeMs=").append(now-downTime).append(" historyAt=").append(now-1).append(" currentAt=").append(now).append('\n');MotionEvent e=MotionEvent.obtain(downTime,now-1,MotionEvent.ACTION_MOVE,1,new MotionEvent.PointerProperties[]{p},new MotionEvent.PointerCoords[]{c},0,0,1,1,0,0,InputDevice.SOURCE_TOUCHSCREEN,0);
                c.y=bottom.exactCenterY();e.addBatch(now,new MotionEvent.PointerCoords[]{c},0);check(e.getHistorySize()==1,"sparse glissando includes native history");wood.dispatchTouchEvent(e);e.recycle();
            });
            diagnostics("immediately after sparse MOVE");cellEvent(36,MotionEvent.ACTION_UP,17,false);waitFor(()->port.since(start,0x91).size()==6,"sparse touch MOVE emits every crossed string");assertOrder(start,NOTES[0],"physical high-to-low C traversal");
            report.append("PHYSICAL six-row sparse/history traversal PASS\n");
        }else{
            cellEvent(6,MotionEvent.ACTION_UP,17,false);report.append("CLIPPED short landscape: no full-board physical traversal claim; individual cells use scroll/accessibility\n");
            for(int row=1;row<6;row++)tap(6+row*6);
            waitFor(()->port.since(start,0x91).size()==6,"all six notes operable with scroll/accessibility in clipped layout");
        }
        snapshot("playing");empty("plucked pulses expire");
        for(Sent on:port.since(start,0x91)){
            Sent off=null;for(Sent candidate:port.since(start,0x81))if(candidate.message.data1==on.message.data1&&candidate.at>=on.at){off=candidate;break;}
            check(off!=null&&off.at-on.at<=680,"600ms pulse releases within 80ms Android/IO scheduling tolerance: "+on.message.data1);
        }
    }
    private void assertOrder(int start,int[] expected,String why){List<Sent> notes=port.since(start,0x91);check(notes.size()==expected.length,why+" note count "+notes.size());for(int i=0;i<expected.length;i++)check(notes.get(i).message.data1==expected[i],why+" index "+i);}
    private void retriggerAndPointers()throws Exception{
        phase("retriggerAndPointers begin");
        int start=port.size();final int firstStart=start;tap(6);waitFor(()->port.count(firstStart,0x91,67)==1,"first same note pulse sent");tap(6);waitFor(()->port.count(firstStart,0x91,67)==2,"retrigger emits another NoteOn before prior expiry");
        List<Sent> sequence=new ArrayList<>();synchronized(port){for(int i=start;i<port.sent.size();i++){Sent s=port.sent.get(i);if(s.message.data1==67&&(s.message.status==0x91||s.message.status==0x81))sequence.add(s);}}
        check(sequence.size()>=3&&sequence.get(0).message.status==0x91&&sequence.get(1).message.status==0x81&&sequence.get(2).message.status==0x91,"same-note retrigger is On, Off, On");empty("retrigger pulse finishes");
        expose(6);check(cellVisible(6)&&cellVisible(7),"two distinct top-row cells visible for real multi-pointer input");Rect a=bounds(6),b=bounds(7);start=port.size();final int mark=start;
        event(wood,MotionEvent.ACTION_DOWN,new int[]{41},new float[]{a.exactCenterX()},new float[]{a.exactCenterY()},true);
        event(wood,MotionEvent.ACTION_POINTER_DOWN|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),new int[]{41,83},new float[]{a.exactCenterX(),b.exactCenterX()},new float[]{a.exactCenterY(),b.exactCenterY()},false);
        waitFor(()->port.count(mark,0x91,67)==1&&port.count(mark,0x91,72)==1,"two stable pointer IDs trigger distinct cells");
        // Reorder indices while preserving IDs; no spurious re-pluck is permitted.
        event(wood,MotionEvent.ACTION_MOVE,new int[]{83,41},new float[]{b.exactCenterX(),a.exactCenterX()},new float[]{b.exactCenterY(),a.exactCenterY()},false);
        event(wood,MotionEvent.ACTION_CANCEL,new int[]{83,41},new float[]{b.exactCenterX(),a.exactCenterX()},new float[]{b.exactCenterY(),a.exactCenterY()},false);
        empty("ACTION_CANCEL clears all pointer pulses");quiet(port,mark,2,680,"cancelled pointers cannot replay");
    }
    private void spacing(int milliseconds)throws Exception{
        ui(()->{
            SeekBar bar=(SeekBar)field("strumSpacing");int before=bar.getProgress(),target=milliseconds-20;
            Bundle values=new Bundle();values.putFloat(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE,target);
            boolean accepted=bar.performAccessibilityAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS.getId(),values);
            report.append("SPACING before=").append(before).append(" target=").append(target).append(" returned=").append(accepted).append(" actual=").append(bar.getProgress()).append('\n');
            if(before!=target)check(accepted,"native spacing change action accepted");
            check(bar.getProgress()==target,"native spacing has requested progress "+target);
            check(((TextView)field("strumText")).getText().toString().equals("扫弦间隔 "+milliseconds+" ms"),"spacing label says exactly "+milliseconds+" ms");
        });
    }
    private void strums()throws Exception{
        phase("strums begin");
        // Select C using a real/accessible cell, then wait for its pulse to finish.
        tap(6);empty("C selected and previous pulse expired");
        for(int interval:new int[]{20,160})for(boolean up:new boolean[]{false,true}){
            spacing(interval);int start=port.size();ui(()->((Button)field(up?"strumUp":"strumDown")).performClick());
            waitFor(()->port.since(start,0x91).size()==6,"automatic six-note strum");int[] wanted=NOTES[0].clone();if(up)for(int i=0;i<3;i++){int temp=wanted[i];wanted[i]=wanted[5-i];wanted[5-i]=temp;}
            assertOrder(start,wanted,(up?"up":"down")+" strum at "+interval+"ms");List<Sent> values=port.since(start,0x91);long span=values.get(5).at-values.get(0).at;
            // Wire writes are performed by the real 20ms IO owner; short-interval
            // frames may queue, so verify total cadence within one tick per frame.
            check(span>=interval*5-80&&span<=interval*5+180,"strum output cadence interval="+interval+" span="+span);
            empty("automatic strum pulses finish");
        }
        spacing(160);expose(7);check(cellVisible(7),"manual cancel cell visible before automatic sweep starts");Rect manual=bounds(7);
        int start=port.size();final int manualStart=start;ui(()->((Button)field("strumDown")).performClick());
        waitFor(()->port.since(manualStart,0x91).size()==1&&session.view().queueSize==0,"first auto note sent before manual cancellation");
        // Prepare scrolling and geometry before starting the sweep; dispatch from
        // the cached visible coordinate without any layout/idle wait in between.
        manualCancellationStart=manualStart;try{event(wood,MotionEvent.ACTION_DOWN,new int[]{29},new float[]{manual.exactCenterX()},new float[]{manual.exactCenterY()},true);}finally{manualCancellationStart=-1;}
        event(wood,MotionEvent.ACTION_UP,new int[]{29},new float[]{manual.exactCenterX()},new float[]{manual.exactCenterY()},false);
        waitFor(()->port.count(manualStart,0x91,72)==1,"manual Am cell interrupts pending C sweep");quiet(port,start,2,800,"manual touch cancels not-yet-run automatic notes");empty("manual cancel eventually releases");
        // Chord head is independently operable, including with screen readers.
        start=port.size();final int chordStart=start;accessible(1);waitFor(()->port.since(chordStart,0x91).size()==6,"accessible Am chord head triggers six notes");assertOrder(start,NOTES[1],"accessible Am chord head");empty("accessible chord finishes");
    }
    private void quiet(Port target,int start,int expected,long duration,String why)throws Exception{
        long until=SystemClock.elapsedRealtime()+duration;
        while(SystemClock.elapsedRealtime()<until){if(target.since(start,0x91).size()!=expected){probeStart=start;diagnostics(why+" expectedNoteOns="+expected);check(false,why);}SystemClock.sleep(20);}
        if(target.since(start,0x91).size()!=expected){probeStart=start;diagnostics(why+" expectedNoteOns="+expected);check(false,why);}
    }
    private void awaitWhileUiHeld(Condition condition,String why)throws Exception{
        long until=SystemClock.elapsedRealtime()+250;
        while(SystemClock.elapsedRealtime()<until){if(condition.get())return;SystemClock.sleep(5);}
        throw new AssertionError(why+" (normal IO owner did not progress within250ms)");
    }
    private void epochProbe()throws Exception{
        phase("epochProbe begin");
        ready("epoch probe starts with rendered ready state");empty("epoch probe starts without prior notes");spacing(20);
        final int start=port.size();probeStart=start;final int[] baseline={0},remainingMask={-1};final long[] remainingDue={0};
        ui(()->{
            Field frameField=wood.getClass().getDeclaredField("frame");frameField.setAccessible(true);Runnable oldFrame=(Runnable)frameField.get(wood);
            Field modelField=wood.getClass().getDeclaredField("instrument");modelField.setAccessible(true);Object oldModel=modelField.get(wood);
            Method mask=oldModel.getClass().getMethod("mask"),nextAt=oldModel.getClass().getMethod("nextAt");
            long began=SystemClock.elapsedRealtime();((Button)field("strumDown")).performClick();
            // Keep the main thread occupied so the real first send can complete,
            // but neither queued render nor any pending strum frame can run.
            awaitWhileUiHeld(()->port.since(start,0x91).size()>=1,"first strum NoteOn reaches real IO owner");
            baseline[0]=port.since(start,0x91).size();check(baseline[0]==1,"exactly one old-epoch note before release");
            long oldEpoch=session.view().inputEpoch;session.releaseNotes();long nextEpoch=session.view().inputEpoch;
            check(nextEpoch!=oldEpoch,"actual core release advances epoch without rendering wood");
            awaitWhileUiHeld(()->session.view().playable&&session.view().ownerMask==0&&session.view().queueSize==0,"normal IO owner completes real release barrier");
            while(SystemClock.elapsedRealtime()-began<60)SystemClock.sleep(5);
            long age=SystemClock.elapsedRealtime()-began;check(age<250,"old frame remains inside300ms freshness gate");
            report.append("EPOCH old=").append(oldEpoch).append(" next=").append(nextEpoch).append(" ageMs=").append(age)
                .append(" baselineNoteOns=").append(baseline[0]).append(" beforeMask=").append(mask.invoke(oldModel)).append(" beforeDue=").append(nextAt.invoke(oldModel)).append('\n');
            // Invoke the actual View callback while its render is still withheld.
            // No manual tick, direct MIDI call, or fake input model is involved.
            oldFrame.run();remainingMask[0]=((Number)mask.invoke(oldModel)).intValue();remainingDue[0]=((Number)nextAt.invoke(oldModel)).longValue();
            long observeUntil=SystemClock.elapsedRealtime()+45;while(SystemClock.elapsedRealtime()<observeUntil)SystemClock.sleep(5);
            report.append("EPOCH afterFrame mask=").append(remainingMask[0]).append(" due=").append(remainingDue[0])
                .append(" noteOns=").append(port.since(start,0x91).size()).append(" local=").append(session.view().localMask)
                .append(" queue=").append(session.view().queueSize).append(" epoch=").append(session.view().inputEpoch).append('\n');
        });
        diagnostics("epoch probe after withheld render callback");
        check(remainingMask[0]==0&&remainingDue[0]<0,"old-epoch View callback must clear pending model work");
        quiet(port,start,baseline[0],800,"old-epoch pending strum cannot add NoteOn after recovery");empty("epoch probe leaves no notes");
        ready("fresh epoch rendered after rejected old callback");report.append("EPOCH pending callback rejection PASS\n");
    }
    private void sourceUnion()throws Exception{
        phase("sourceUnion begin");
        ui(()->((Button)field("ksKeyboardButton")).performClick());ready("piano dock expansion release barrier");empty("piano dock expansion starts empty");View keyboard=(View)field("keyboard");check(keyboard.isShown(),"existing piano can expand alongside wood");
        float x=keyboard.getWidth()*11.5f/15f,y=keyboard.getHeight()*.82f;int start=port.size();event(keyboard,MotionEvent.ACTION_DOWN,new int[]{97},new float[]{x},new float[]{y},true);
        waitFor(()->port.count(start,0x91,67)==1,"piano G4 held on KS channel");
        // Provider click avoids claiming an offscreen wood cell is touchable when
        // the deliberately expanded dock reduces the phone's wood viewport.
        accessible(6);long until=SystemClock.elapsedRealtime()+680;while(SystemClock.elapsedRealtime()<until){check(port.count(start,0x81,67)==0,"wood expiry must not release piano's held same note");SystemClock.sleep(20);}
        check((session.view().localMask&(1<<19))!=0&&(session.view().ownerMask&(1<<19))!=0,"held piano remains in source union after wood expiry");snapshot("piano-coexistence");
        event(keyboard,MotionEvent.ACTION_UP,new int[]{97},new float[]{x},new float[]{y},false);waitFor(()->port.count(start,0x81,67)==1,"only piano UP releases union G4");empty("source union fully released");
        ui(()->((Button)field("ksKeyboardButton")).performClick());ready("collapse piano release barrier");
    }
    private int pending()throws Exception{
        ready("ready for cancellation probe");empty("no previous notes before cancellation probe");tap(6);empty("C selected before cancellation probe");spacing(160);int start=port.size();
        ui(()->((Button)field("strumDown")).performClick());waitFor(()->port.since(start,0x91).size()==1,"pending strum starts exactly one note");return start;
    }
    private void safety()throws Exception{
        phase("safety begin");
        int start=pending();ui(()->((Button)field("stop")).performClick());empty("stop clears masks");quiet(port,start,1,800,"stop cannot replay delayed strum");ready("stop release barrier");
        start=pending();ui(()->((Button)field("fm")).performClick());waitFor(()->session.view().performanceApplied==2,"FM route applied");ready("FM release barrier");empty("mode switch clears masks");quiet(port,start,1,800,"old KS timers cannot add notes after mode switch");check(!wood.isShown(),"wood hidden for FM");
        ui(()->((Button)field("ks")).performClick());ready("return to KS");start=pending();ui(()->call("select",new Class[]{int.class},2));empty("connection page clears inputs");quiet(port,start,1,800,"leaving play page cancels delayed strum");
        Port retired=port;ui(()->((Button)field("wifiMode")).performClick());waitFor(()->retired.closed&&!ports.isOpen()&&ports.selected()==ControlPort.Mode.WIFI,"Wi-Fi selection closes old MIDI port");check(retired.requests==0,"no application protocol RPC on old BLE link");
        connect();wood=(View)field("plucked");start=pending();Port paused=port;final int pauseStart=start;
        ui(()->callActivityOnPause(activity));waitFor(()->paused.closed&&session.view().localMask==0&&session.view().ownerMask==0,"pause clears masks and retires physical port");quiet(paused,pauseStart,1,800,"background cannot replay pending strum");
        ui(()->callActivityOnResume(activity));idle("resume");check(session.view().localMask==0&&session.view().ownerMask==0,"resume does not restore old touches");quiet(paused,pauseStart,1,200,"resume cannot replay old timers");check(paused.requests==0,"lifecycle path has zero RPC");
    }
    @Override public void onCreate(Bundle arguments){args=arguments==null?new Bundle():arguments;start();}
    @Override public void onStart(){startWatchdog();phase("onStart launch begin");Bundle result=new Bundle();try{
        Intent launch=getTargetContext().getPackageManager().getLaunchIntentForPackage("cn.fpgasynth.control");launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK);activity=startActivitySync(launch);idle("launch");
        session=(AppController)field("session");ports=(ControlPorts)field("ports");connect();layout();
        if("true".equalsIgnoreCase(args.getString("probeHold","false"))){touchAndGlissando();}
        else{epochProbe();if(!"true".equalsIgnoreCase(args.getString("probeEpoch","false"))){touchAndGlissando();retriggerAndPointers();strums();sourceUnion();safety();}}
        result.putString("result","PASS");result.putString("evidence",assertions+" assertions; actual native UI/session + fake physical port; no RF/audio\n"+report);
    }catch(Throwable error){StringWriter trace=new StringWriter();error.printStackTrace(new PrintWriter(trace));result.putString("result","FAIL");result.putString("error",trace.toString());result.putString("evidence",report.toString());}
    finally{phase("cleanup begin");if(activity!=null)ui(()->activity.finish());phase("finish result="+result.getString("result"));completed=true;if(watchdog!=null)watchdog.interrupt();finish(0,result);}}
}
