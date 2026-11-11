package cn.fpgasynth.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.SeekBar;

/** Relative vertical gesture: touching a dial never jumps its value to the touch angle. */
final class ConsoleKnob extends View {
    interface Listener {void begin();void change(int value);void end(boolean cancelled);}
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Listener listener;
    private int value=64,pointer=-1,start;
    private float downY;
    private boolean dragging,pending,known;
    ConsoleKnob(Context context,Listener listener){super(context);this.listener=listener;setFocusable(true);setClickable(true);setMinimumHeight(dp(88));}
    private int dp(float n){return Math.round(n*getResources().getDisplayMetrics().density);}
    int value(){return value;}
    boolean editing(){return dragging;}
    void value(int n,boolean waiting){if(!dragging)value=Math.max(0,Math.min(127,n));known=true;pending=waiting;invalidate();}
    void unknown(){if(known){known=false;invalidate();}}
    void cancel(){if(dragging){dragging=false;pointer=-1;listener.end(true);}invalidate();}
    @Override public void setEnabled(boolean enabled){if(!enabled)cancel();super.setEnabled(enabled);setAlpha(enabled?1f:.45f);invalidate();}
    @Override protected void onDraw(Canvas c){
        float radius=Math.min(getWidth()*.30f,getHeight()*.34f),x=getWidth()/2f,y=getHeight()/2f;
        RectF arc=new RectF(x-radius,y-radius,x+radius,y+radius);paint.setStyle(Paint.Style.STROKE);paint.setStrokeCap(Paint.Cap.ROUND);paint.setStrokeWidth(dp(4));
        paint.setColor(ConsoleTheme.BORDER);c.drawArc(arc,135,270,false,paint);if(known){paint.setColor(pending?ConsoleTheme.AMBER:ConsoleTheme.TEAL);c.drawArc(arc,135,270*value/127f,false,paint);}
        paint.setStyle(Paint.Style.FILL);paint.setColor(ConsoleTheme.WELL);c.drawCircle(x,y,radius-dp(7),paint);
        double angle=Math.toRadians(135+270*value/127f);paint.setStrokeWidth(dp(3));paint.setColor(isEnabled()?ConsoleTheme.INK:ConsoleTheme.MUTED);
        if(known)c.drawLine(x+(float)Math.cos(angle)*radius*.37f,y+(float)Math.sin(angle)*radius*.37f,x+(float)Math.cos(angle)*radius*.67f,y+(float)Math.sin(angle)*radius*.67f,paint);
        if(hasFocus()){paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(1));paint.setColor(ConsoleTheme.TEAL);c.drawRoundRect(2,2,getWidth()-2,getHeight()-2,dp(6),dp(6),paint);paint.setStyle(Paint.Style.FILL);}
    }
    @Override public boolean onTouchEvent(MotionEvent e){
        if(!isEnabled())return false;
        int action=e.getActionMasked();
        if(action==MotionEvent.ACTION_DOWN){pointer=e.getPointerId(0);downY=e.getY();start=value;dragging=true;listener.begin();getParent().requestDisallowInterceptTouchEvent(true);invalidate();return true;}
        if(!dragging)return true;
        if(action==MotionEvent.ACTION_MOVE){int index=e.findPointerIndex(pointer);if(index<0){cancel();return true;}
            int n=Math.max(0,Math.min(127,start+Math.round((downY-e.getY(index))*127f/dp(200))));
            if(n!=value){value=n;known=true;listener.change(n);invalidate();}return true;}
        if(action==MotionEvent.ACTION_CANCEL){cancel();return true;}
        if(action==MotionEvent.ACTION_UP||(action==MotionEvent.ACTION_POINTER_UP&&e.getPointerId(e.getActionIndex())==pointer)){
            dragging=false;pointer=-1;listener.end(false);getParent().requestDisallowInterceptTouchEvent(true);performClick();invalidate();return true;}
        return true;
    }
    void step(int amount){if(!isEnabled())return;int next=Math.max(0,Math.min(127,value+amount));if(next==value)return;listener.begin();value=next;known=true;listener.change(value);listener.end(false);sendAccessibilityEvent(AccessibilityEvent.TYPE_VIEW_SELECTED);invalidate();}
    @Override public boolean onKeyDown(int code,KeyEvent e){if(code==KeyEvent.KEYCODE_DPAD_UP||code==KeyEvent.KEYCODE_DPAD_RIGHT){step(1);return true;}if(code==KeyEvent.KEYCODE_DPAD_DOWN||code==KeyEvent.KEYCODE_DPAD_LEFT){step(-1);return true;}return super.onKeyDown(code,e);}
    @Override public boolean performClick(){super.performClick();return true;}
    @Override public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info){super.onInitializeAccessibilityNodeInfo(info);info.setClassName(SeekBar.class.getName());
        if(known)info.setRangeInfo(AccessibilityNodeInfo.RangeInfo.obtain(AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_INT,0,127,value));
        if(isEnabled()){info.addAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD);info.addAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD);}}
    @Override public boolean performAccessibilityAction(int action,Bundle args){if(isEnabled()&&(action==AccessibilityNodeInfo.ACTION_SCROLL_FORWARD||action==AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD)){step(action==AccessibilityNodeInfo.ACTION_SCROLL_FORWARD?1:-1);return true;}return super.performAccessibilityAction(action,args);}
}
