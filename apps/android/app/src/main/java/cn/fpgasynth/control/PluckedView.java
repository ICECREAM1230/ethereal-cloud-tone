package cn.fpgasynth.control;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.TypedValue;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewParent;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import cn.fpgasynth.control.core.PluckedInstrument;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

/** Native six-string, six-chord KS control surface; never renders audio telemetry. */
final class PluckedView extends View {
    private static final int NO_ID=Integer.MIN_VALUE;
    private static final int INK=0xfff2f1e8,AMBER=0xfff0bd6d,TEAL=0xff77d5c2;
    private final KeyboardView.Listener listener;
    private final PluckedInstrument instrument;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF woodFrame=new RectF(),field=new RectF();
    private final RectF[] targets=new RectF[42];
    private final long[] struckAt=new long[36];
    private final Map<Integer,Contact> contacts=new HashMap<>();
    private final Bitmap wood;
    private final Path clip=new Path();
    private final SurfaceNodes nodes=new SurfaceNodes();
    private final Runnable frame=new Runnable(){@Override public void run(){instrument.tick(SystemClock.elapsedRealtime());reschedule();invalidate();}};
    private boolean playable,sendOnly,rejected;
    private int owner,lastOutputMask,guideMask,nextMask,focusId=18,accessibilityFocus=NO_ID,hoverId=NO_ID;
    private long epoch=Long.MIN_VALUE;

    private static final class Contact {
        float x,y;
        long eventTime;
        int lastId;
        Contact(float x,float y,long time,int id){this.x=x;this.y=y;eventTime=time;lastId=id;}
    }

    PluckedView(Context context,KeyboardView.Listener listener){
        super(context);this.listener=listener;
        instrument=new PluckedInstrument(new PluckedInstrument.Output(){
            @Override public boolean keys(int mask,long now){return output(mask,now);}
        });
        BitmapFactory.Options options=new BitmapFactory.Options();options.inSampleSize=2;
        wood=BitmapFactory.decodeResource(getResources(),R.drawable.ks_honey_spruce,options);
        setFocusable(true);setClickable(true);setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);
        setMinimumHeight((int)dp(368));describe();
    }

    private float dp(float value){return value*getResources().getDisplayMetrics().density;}
    private float sp(float value){return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP,value,getResources().getDisplayMetrics());}
    private void describe(){setContentDescription("KS木质拨弦面，六根横弦，六列和弦，音域C3至C5。点单音或上下滑动拨弦，点和弦名称自动下扫。琥珀色为本地触点，薄荷圆点为"+(sendOnly?"MIDI已发送，未回读设备。":"设备采用。")+"六条虚拟弦不代表硬件声部数量，弦振只是触控反馈。");}

    void state(boolean enabled,int ownerMask,long inputEpoch,boolean directMidi){
        boolean changed=epoch!=inputEpoch||playable!=enabled;
        if(changed||!enabled)clear();
        epoch=inputEpoch;playable=enabled;owner=enabled?ownerMask:0;
        if(sendOnly!=directMidi){sendOnly=directMidi;describe();}
        instrument.state(enabled,inputEpoch);
        if(changed)rejected=false;
        setAlpha(enabled?1f:.55f);invalidate();
    }

    /** MainActivity releases the combined source; do not emit from this method. */
    void clear(){
        removeCallbacks(frame);contacts.clear();instrument.clear();lastOutputMask=0;
        for(int i=0;i<struckAt.length;i++)struckAt[i]=0;
        parentIntercept(false);invalidate();
    }
    void interval(int milliseconds){instrument.interval(milliseconds);}
    void guide(int current,int next){if(guideMask==current&&nextMask==next)return;guideMask=current;nextMask=next;invalidate();}
    private int guideColumn(int note){for(int row=0;row<6;row++)if(PluckedInstrument.note(instrument.selected(),row)==note)return instrument.selected();for(int col=0;col<6;col++)for(int row=0;row<6;row++)if(PluckedInstrument.note(col,row)==note)return col;return -1;}
    void strum(boolean up){if(!canPlay())return;instrument.strum(up,SystemClock.elapsedRealtime());reschedule();invalidate();}
    private boolean canPlay(){return playable&&!rejected&&getVisibility()==VISIBLE&&getWindowVisibility()==VISIBLE;}
    private void parentIntercept(boolean disallow){ViewParent parent=getParent();if(parent!=null)parent.requestDisallowInterceptTouchEvent(disallow);}
    private boolean output(int mask,long now){
        int added=mask&~lastOutputMask;
        for(int row=0;row<6;row++)if((added&(1<<(PluckedInstrument.note(instrument.selected(),row)-48)))!=0)
            struckAt[row*6+instrument.selected()]=now;
        lastOutputMask=mask;
        boolean okay=listener.keys(mask,now);
        if(!okay){rejected=true;contacts.clear();removeCallbacks(frame);parentIntercept(false);}
        return okay;
    }
    private void reschedule(){
        removeCallbacks(frame);
        if(rejected)return;
        long due=instrument.nextAt();
        if(due<0)return;
        long now=SystemClock.elapsedRealtime();
        long delay=Math.max(1,due-now);
        if(ValueAnimator.areAnimatorsEnabled()&&instrument.mask()!=0)delay=Math.min(delay,32);
        postDelayed(frame,delay);
    }
    private void releaseLocal(){
        if(instrument!=null)instrument.release(SystemClock.elapsedRealtime());
        clear();
    }
    @Override protected void onDetachedFromWindow(){releaseLocal();super.onDetachedFromWindow();}
    @Override protected void onVisibilityChanged(View changed,int visibility){super.onVisibilityChanged(changed,visibility);if(visibility!=VISIBLE&&instrument!=null)releaseLocal();}
    @Override protected void onWindowVisibilityChanged(int visibility){super.onWindowVisibilityChanged(visibility);if(visibility!=VISIBLE&&instrument!=null)releaseLocal();}

    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh){
        float edge=dp(5),gutter=dp(21),header=dp(48),footer=dp(20);
        woodFrame.set(edge,edge,w-edge,h-edge);
        field.set(edge+gutter,edge+header,w-edge-dp(4),h-edge-footer);
        float col=field.width()/6,row=field.height()/6;
        for(int x=0;x<6;x++)targets[x]=new RectF(field.left+x*col,woodFrame.top,field.left+(x+1)*col,field.top);
        for(int y=0;y<6;y++)for(int x=0;x<6;x++)targets[6+y*6+x]=new RectF(field.left+x*col,field.top+y*row,field.left+(x+1)*col,field.top+(y+1)*row);
        invalidate();
    }

    private void fill(Canvas canvas,RectF area,int color,float radius){paint.setStyle(Paint.Style.FILL);paint.setColor(color);canvas.drawRoundRect(area,radius,radius,paint);}
    private void text(Canvas canvas,String value,float x,float y,float size,int color,Paint.Align align){paint.setStyle(Paint.Style.FILL);paint.setColor(color);paint.setTextSize(size);paint.setTextAlign(align);canvas.drawText(value,x,y,paint);}
    @Override protected void onDraw(Canvas canvas){
        super.onDraw(canvas);if(targets[0]==null)return;
        int save=canvas.save();clip.reset();clip.addRoundRect(woodFrame,dp(9),dp(9),Path.Direction.CW);canvas.clipPath(clip);
        if(wood!=null){paint.setColor(0xffffffff);canvas.drawBitmap(wood,null,woodFrame,paint);}else fill(canvas,woodFrame,0xffb97831,0);
        fill(canvas,woodFrame,0x1687480f,0);
        fill(canvas,field,0xb52b1b0d,0);
        int selected=instrument.selected();
        RectF head=targets[selected];fill(canvas,head,0xb74b2c14,0);
        fill(canvas,new RectF(head.left,field.top,head.right,field.bottom),0x223c2515,0);
        float headingSize=Math.min(sp(18),targets[0].width()*.44f);
        for(int col=0;col<6;col++){
            RectF h=targets[col];text(canvas,PluckedInstrument.chord(col),h.centerX(),h.centerY()+headingSize*.32f,headingSize,col==selected?INK:0xff3d2715,Paint.Align.CENTER);
            paint.setColor(0x55613c19);paint.setStrokeWidth(dp(.7f));canvas.drawLine(h.left,woodFrame.top,h.left,field.bottom,paint);
        }
        float rowHeight=field.height()/6;
        int local=instrument.mask();long now=SystemClock.elapsedRealtime();boolean animate=ValueAnimator.areAnimatorsEnabled();
        float labelSize=Math.min(sp(12),Math.min(targets[6].width()*.35f,rowHeight*.34f));
        for(int row=0;row<6;row++){
            float lineY=field.top+rowHeight*(row+.36f);
            text(canvas,Integer.toString(row+1),woodFrame.left+dp(10),lineY+dp(3),Math.min(sp(11),rowHeight*.25f),0xff452b16,Paint.Align.CENTER);
            paint.setColor(0x990e0a06);paint.setStrokeWidth(dp(4.8f));canvas.drawLine(field.left,lineY+dp(2),field.right,lineY+dp(2),paint);
            paint.setColor(0xffd3b888);paint.setStrokeWidth(dp(.9f+row*.12f));canvas.drawLine(field.left,lineY,field.right,lineY,paint);
            paint.setColor(0x99fff5d5);paint.setStrokeWidth(dp(.45f));canvas.drawLine(field.left,lineY-dp(1),field.right,lineY-dp(1),paint);
            for(int col=0;col<6;col++){
                int index=row*6+col,id=6+index,note=PluckedInstrument.note(col,row),bit=1<<(note-48);RectF r=targets[id];
                boolean down=(local&bit)!=0&&struckAt[index]>0&&now-struckAt[index]<PluckedInstrument.GATE_MS;
                if(down){
                    float decay=1-Math.min(1,(now-struckAt[index])/(float)PluckedInstrument.GATE_MS);
                    paint.setStyle(Paint.Style.FILL);paint.setColor(0x3df0bd6d);canvas.drawCircle(r.centerX(),lineY,dp(14),paint);
                    paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(1.7f));paint.setColor(AMBER);
                    float offset=animate?(float)Math.sin((now-struckAt[index])*.05)*dp(2.8f)*decay:0;
                    Path vibration=new Path();vibration.moveTo(r.left+dp(3),lineY);vibration.quadTo(r.centerX(),lineY+offset,r.right-dp(3),lineY);canvas.drawPath(vibration,paint);
                }
                paint.setStyle(Paint.Style.FILL);paint.setColor(down?AMBER:0xff392819);canvas.drawCircle(r.centerX(),lineY,dp(down?4:3),paint);
                paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(.7f));paint.setColor(down?INK:0xffc8a66e);canvas.drawCircle(r.centerX(),lineY,dp(down?4:3),paint);
                float labelY=lineY+labelSize+dp(4);
                text(canvas,PluckedInstrument.name(col,row),r.centerX(),labelY,labelSize,down?0xffffd697:INK,Paint.Align.CENTER);
                if((owner&bit)!=0){paint.setColor(TEAL);canvas.drawCircle(r.centerX()+labelSize*1.22f,labelY-labelSize*.4f,dp(2.3f),paint);}
                if(((guideMask|nextMask)&bit)!=0&&guideColumn(note)==col){boolean current=(guideMask&bit)!=0;paint.setColor(ConsoleTheme.GUIDE);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(current?2:1));canvas.drawRoundRect(r.left+dp(2),r.top+dp(2),r.right-dp(2),r.bottom-dp(2),dp(4),dp(4),paint);text(canvas,current?"弹":"下",r.right-dp(5),lineY-dp(5),Math.min(labelSize,dp(11)),ConsoleTheme.GUIDE,Paint.Align.RIGHT);}
                if((isFocused()&&focusId==id)||accessibilityFocus==id){paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(2));paint.setColor(TEAL);RectF f=new RectF(r);f.inset(dp(2),dp(2));canvas.drawRoundRect(f,dp(4),dp(4),paint);}
            }
        }
        if((isFocused()&&focusId<6)||accessibilityFocus>=0&&accessibilityFocus<6){int id=accessibilityFocus>=0&&accessibilityFocus<6?accessibilityFocus:focusId;paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(2));paint.setColor(TEAL);RectF f=new RectF(targets[id]);f.inset(dp(2),dp(2));canvas.drawRoundRect(f,dp(4),dp(4),paint);}
        float footerSize=Math.min(sp(10),dp(11));
        text(canvas,"高音 ↑  ·  低音 ↓",field.left,woodFrame.bottom-dp(5),footerSize,0xff422c18,Paint.Align.LEFT);
        text(canvas,"触点  ·  "+(sendOnly?"已发送":"设备采用")+"  C3–C5",field.right-dp(2),woodFrame.bottom-dp(5),footerSize,0xff422c18,Paint.Align.RIGHT);
        canvas.restoreToCount(save);paint.setStyle(Paint.Style.STROKE);paint.setColor(0xffc49b60);paint.setStrokeWidth(dp(1));canvas.drawRoundRect(woodFrame,dp(9),dp(9),paint);
    }

    private int hit(float x,float y){for(int id=0;id<42;id++)if(targets[id]!=null&&targets[id].contains(x,y))return id;return NO_ID;}
    private void activate(int id,long eventAt,long now){
        if(id<0||id>=42||!canPlay())return;
        focusId=id;
        if(id<6){instrument.select(id);instrument.strum(false,now);}
        else {int cell=id-6;instrument.pluck(cell%6,cell/6,eventAt,now);}
        invalidate();
    }
    private long sampleAt(long eventTime,long now){long age=SystemClock.uptimeMillis()-eventTime;return age<0?-1:now-age;}
    private void sample(int pointer,float x,float y,long time,long now){
        Contact contact=contacts.get(pointer);if(contact==null||time<contact.eventTime)return;
        long at=sampleAt(time,now);
        if(at<0||now-at>300){
            int id=hit(x,y);
            if(id==NO_ID)contacts.remove(pointer);
            else {contact.x=x;contact.y=y;contact.eventTime=time;contact.lastId=id;}
            // Consume an unobserved path rather than replaying its crossings on
            // the next fresh event. A genuinely new cell crossing can still play.
            return;
        }
        if(time==contact.eventTime&&x==contact.x&&y==contact.y)return;
        // Clip the segment into every crossed row/column cell. Sampling each
        // interval midpoint handles sparse and historical events, including a
        // diagonal, without depending on the platform's event frequency.
        ArrayList<Float> cuts=new ArrayList<>();cuts.add(0f);cuts.add(1f);
        float dx=x-contact.x,dy=y-contact.y;
        if(dx!=0)for(int col=0;col<=6;col++)addCut(cuts,(field.left+field.width()*col/6-contact.x)/dx);
        if(dy!=0){addCut(cuts,(woodFrame.top-contact.y)/dy);for(int row=0;row<=6;row++)addCut(cuts,(field.top+field.height()*row/6-contact.y)/dy);}
        Collections.sort(cuts);
        for(int i=0;i<cuts.size()-1&&contacts.containsKey(pointer)&&!rejected;i++){
            float t=(cuts.get(i)+cuts.get(i+1))*.5f;
            visit(contact,hit(contact.x+dx*t,contact.y+dy*t),time,now);
        }
        if(contacts.containsKey(pointer)&&!rejected){
            // Traverse the valid part of an exit segment first, then disarm this
            // pointer permanently. A later MOVE re-entering cannot resurrect it.
            int endpoint=hit(x,y);
            if(endpoint==NO_ID)contacts.remove(pointer);
            else {visit(contact,endpoint,time,now);contact.x=x;contact.y=y;contact.eventTime=time;}
        }
    }
    private static void addCut(ArrayList<Float> cuts,float value){if(value>0&&value<1)cuts.add(value);}
    private void visit(Contact contact,int id,long time,long now){if(id!=NO_ID&&id!=contact.lastId){contact.lastId=id;activate(id,sampleAt(time,now),now);}}
    @Override public boolean onTouchEvent(MotionEvent event){
        int action=event.getActionMasked(),index=event.getActionIndex();long now=SystemClock.elapsedRealtime();
        if(action==MotionEvent.ACTION_CANCEL){releaseLocal();return true;}
        if(!canPlay()){clear();return true;}
        if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN){
            if(action==MotionEvent.ACTION_DOWN)contacts.clear();
            int pointer=event.getPointerId(index),id=hit(event.getX(index),event.getY(index));
            long at=sampleAt(event.getEventTime(),now);
            if(id!=NO_ID&&at>=0&&now-at<=300){
                requestFocus();parentIntercept(true);
                contacts.put(pointer,new Contact(event.getX(index),event.getY(index),event.getEventTime(),id));
                activate(id,at,now);
            }
        }else if(action==MotionEvent.ACTION_MOVE||action==MotionEvent.ACTION_POINTER_UP||action==MotionEvent.ACTION_UP){
            for(int h=0;h<event.getHistorySize();h++)for(int i=0;i<event.getPointerCount();i++)sample(event.getPointerId(i),event.getHistoricalX(i,h),event.getHistoricalY(i,h),event.getHistoricalEventTime(h),now);
            for(int i=0;i<event.getPointerCount();i++)sample(event.getPointerId(i),event.getX(i),event.getY(i),event.getEventTime(),now);
            if(action==MotionEvent.ACTION_POINTER_UP||action==MotionEvent.ACTION_UP){contacts.remove(event.getPointerId(index));if(action==MotionEvent.ACTION_UP){contacts.clear();parentIntercept(false);performClick();}}
        }
        reschedule();invalidate();return true;
    }
    @Override public boolean performClick(){super.performClick();return true;}
    @Override public boolean onKeyDown(int keyCode,KeyEvent event){
        int row=focusId<6?-1:(focusId-6)/6,col=focusId<6?focusId:(focusId-6)%6;
        if(keyCode==KeyEvent.KEYCODE_DPAD_LEFT)col=Math.max(0,col-1);
        else if(keyCode==KeyEvent.KEYCODE_DPAD_RIGHT)col=Math.min(5,col+1);
        else if(keyCode==KeyEvent.KEYCODE_DPAD_UP)row=Math.max(-1,row-1);
        else if(keyCode==KeyEvent.KEYCODE_DPAD_DOWN)row=Math.min(5,row+1);
        else if(keyCode==KeyEvent.KEYCODE_ENTER||keyCode==KeyEvent.KEYCODE_DPAD_CENTER||keyCode==KeyEvent.KEYCODE_SPACE){if(event.getRepeatCount()==0){long now=SystemClock.elapsedRealtime();activate(focusId,now,now);reschedule();performClick();}return true;}
        else return super.onKeyDown(keyCode,event);
        focusId=row<0?col:6+row*6+col;invalidate();return true;
    }

    @Override public AccessibilityNodeProvider getAccessibilityNodeProvider(){return nodes;}
    private String label(int id){
        if(id<6)return PluckedInstrument.chord(id)+" 和弦，下扫六弦";
        int cell=id-6,col=cell%6,row=cell/6,bit=1<<(PluckedInstrument.note(col,row)-48);
        return PluckedInstrument.chord(col)+" / "+PluckedInstrument.name(col,row)+"，第"+(row+1)+"弦，MIDI "+PluckedInstrument.note(col,row)+
                ((instrument.mask()&bit)!=0?"，本地触点":"")+((owner&bit)!=0?(sendOnly?"，MIDI已发送，未回读设备":"，设备采用"):"");
    }
    @SuppressWarnings("deprecation") // obtain is the supported factory on minimum API26.
    private void event(int id,int type){
        AccessibilityManager manager=(AccessibilityManager)getContext().getSystemService(Context.ACCESSIBILITY_SERVICE);
        if(manager==null||!manager.isEnabled()||getParent()==null)return;
        AccessibilityEvent event=AccessibilityEvent.obtain(type);event.setSource(this,id);event.setClassName("android.widget.Button");event.setPackageName(getContext().getPackageName());event.setContentDescription(label(id));getParent().requestSendAccessibilityEvent(this,event);
    }
    @Override public boolean dispatchHoverEvent(MotionEvent event){
        AccessibilityManager manager=(AccessibilityManager)getContext().getSystemService(Context.ACCESSIBILITY_SERVICE);
        if(manager==null||!manager.isTouchExplorationEnabled())return super.dispatchHoverEvent(event);
        int id=event.getActionMasked()==MotionEvent.ACTION_HOVER_EXIT?NO_ID:hit(event.getX(),event.getY());
        if(id!=hoverId){if(id!=NO_ID)event(id,AccessibilityEvent.TYPE_VIEW_HOVER_ENTER);if(hoverId!=NO_ID)event(hoverId,AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);hoverId=id;}
        return id!=NO_ID;
    }
    @SuppressWarnings("deprecation") // API26 factories and parent bounds remain required on supported old devices.
    private final class SurfaceNodes extends AccessibilityNodeProvider {
        @Override public AccessibilityNodeInfo createAccessibilityNodeInfo(int id){
            if(id==View.NO_ID){AccessibilityNodeInfo node=AccessibilityNodeInfo.obtain(PluckedView.this);onInitializeAccessibilityNodeInfo(node);for(int child=0;child<42;child++)node.addChild(PluckedView.this,child);return node;}
            if(id<0||id>=42||targets[id]==null)return null;
            AccessibilityNodeInfo node=AccessibilityNodeInfo.obtain();node.setSource(PluckedView.this,id);node.setParent(PluckedView.this);node.setPackageName(getContext().getPackageName());node.setClassName("android.widget.Button");node.setContentDescription(label(id));node.setText(label(id));node.setEnabled(canPlay());node.setVisibleToUser(isShown());node.setClickable(true);node.setFocusable(true);node.setFocused(isFocused()&&focusId==id);node.setAccessibilityFocused(accessibilityFocus==id);
            Rect bounds=new Rect();targets[id].roundOut(bounds);node.setBoundsInParent(bounds);int[] location=new int[2];getLocationOnScreen(location);bounds.offset(location[0],location[1]);node.setBoundsInScreen(bounds);
            node.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_CLICK);node.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_FOCUS);node.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_CLEAR_FOCUS);node.addAction(accessibilityFocus==id?AccessibilityNodeInfo.AccessibilityAction.ACTION_CLEAR_ACCESSIBILITY_FOCUS:AccessibilityNodeInfo.AccessibilityAction.ACTION_ACCESSIBILITY_FOCUS);
            return node;
        }
        @Override public AccessibilityNodeInfo findFocus(int type){int id=type==AccessibilityNodeInfo.FOCUS_ACCESSIBILITY?accessibilityFocus:focusId;return id==NO_ID?null:createAccessibilityNodeInfo(id);}
        @Override public boolean performAction(int id,int action,Bundle arguments){
            if(id==View.NO_ID)return performAccessibilityAction(action,arguments);
            if(id<0||id>=42)return false;
            if(action==AccessibilityNodeInfo.ACTION_CLICK){if(!canPlay())return false;long now=SystemClock.elapsedRealtime();activate(id,now,now);reschedule();event(id,AccessibilityEvent.TYPE_VIEW_CLICKED);performClick();return true;}
            if(action==AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS){if(accessibilityFocus==id)return false;if(accessibilityFocus!=NO_ID)event(accessibilityFocus,AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);accessibilityFocus=id;event(id,AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED);invalidate();return true;}
            if(action==AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS){if(accessibilityFocus!=id)return false;accessibilityFocus=NO_ID;event(id,AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);invalidate();return true;}
            if(action==AccessibilityNodeInfo.ACTION_FOCUS){requestFocus();focusId=id;event(id,AccessibilityEvent.TYPE_VIEW_FOCUSED);invalidate();return true;}
            if(action==AccessibilityNodeInfo.ACTION_CLEAR_FOCUS){if(focusId!=id)return false;clearFocus();invalidate();return true;}
            return false;
        }
    }
}
