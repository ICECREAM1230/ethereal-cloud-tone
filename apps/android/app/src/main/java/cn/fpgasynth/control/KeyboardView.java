package cn.fpgasynth.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import cn.fpgasynth.control.core.TouchNotes;
import java.util.HashSet;
import java.util.Set;

/** Native piano surface: 25 keys, stable pointer IDs, glissando and same-key contacts. */
final class KeyboardView extends View {
    interface Listener {boolean keys(int mask,long now);}
    private final Listener listener;
    private final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF[] keys=new RectF[25];
    private final TouchNotes touch=new TouchNotes();
    private final Set<Integer> armed=new HashSet<>();
    private static final int[] BLACK={1,3,6,8,10};
    private static final String[] NAMES={"C","C♯","D","D♯","E","F","F♯","G","G♯","A","A♯","B"};
    private boolean playable,sendOnly;
    private int owner,guideMask,nextMask;
    private long epoch=-1;
    KeyboardView(Context c,Listener listener){super(c);this.listener=listener;setFocusable(true);setContentDescription("C3 到 C5 多指演奏键盘；琥珀色表示本地触控，底部圆点表示设备采用。");}
    private float dp(float v){return v*getResources().getDisplayMetrics().density;}
    private boolean black(int key){for(int b:BLACK)if(key%12==b)return true;return false;}
    @Override protected void onSizeChanged(int w,int h,int ow,int oh){
        float unit=w/15f;int white=0;
        for(int i=0;i<25;i++){
            if(black(i))keys[i]=new RectF(white*unit-unit*.31f,0,white*unit+unit*.31f,h*.63f);
            else {keys[i]=new RectF(white*unit+1,0,(white+1)*unit-1,h);white++;}
        }
    }
    void state(boolean enabled,int owner,long epoch){state(enabled,owner,epoch,false);}
    void state(boolean enabled,int owner,long epoch,boolean directMidi){
        if(sendOnly!=directMidi){sendOnly=directMidi;setContentDescription(directMidi?"C3 到 C5 多指演奏键盘；琥珀色表示本地触控，底部圆点表示MIDI已发送，未回读设备。":"C3 到 C5 多指演奏键盘；琥珀色表示本地触控，底部圆点表示设备采用。");}
        if(this.epoch!=epoch||!enabled){touch.clear();armed.clear();}
        this.epoch=epoch;playable=enabled;this.owner=owner;invalidate();
    }
    void clear(){touch.clear();armed.clear();invalidate();}
    void guide(int current,int next){if(guideMask==current&&nextMask==next)return;guideMask=current;nextMask=next;invalidate();}
    private int hit(float x,float y){
        for(int i=0;i<25;i++)if(black(i)&&keys[i].contains(x,y))return i;
        for(int i=0;i<25;i++)if(!black(i)&&keys[i].contains(x,y))return i;
        return -1;
    }
    @Override protected void onDraw(Canvas c){
        for(int layer=0;layer<2;layer++)for(int i=0;i<25;i++)if(black(i)==(layer==1)){
            RectF r=keys[i];boolean pressed=(touch.mask()&(1<<i))!=0;
            p.setColor(pressed?ConsoleTheme.AMBER:black(i)?ConsoleTheme.WELL:Color.rgb(225,234,229));
            p.setAlpha(playable?255:140);c.drawRoundRect(r,dp(4),dp(4),p);p.setAlpha(255);
            p.setColor(black(i)?Color.rgb(173,193,197):Color.rgb(60,81,73));p.setTextAlign(Paint.Align.CENTER);
            p.setTextSize(dp(black(i)?8:10));
            String name=NAMES[i%12]+(3+i/12);c.drawText(name,r.centerX(),r.bottom-dp(12),p);
            if((owner&(1<<i))!=0){p.setColor(Color.rgb(24,103,88));c.drawCircle(r.centerX(),r.bottom-dp(29),dp(3.5f),p);}
            if(((guideMask|nextMask)&(1<<i))!=0){boolean now=(guideMask&(1<<i))!=0;
                p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(dp(now?3:1.5f));p.setColor(ConsoleTheme.GUIDE);c.drawRoundRect(r.left+dp(2),r.top+dp(2),r.right-dp(2),r.bottom-dp(2),dp(3),dp(3),p);p.setStyle(Paint.Style.FILL);
                p.setColor(black(i)?ConsoleTheme.GUIDE:Color.rgb(85,51,135));p.setTextSize(dp(black(i)?9:11));c.drawText(now?"弹":"下",r.centerX(),r.top+dp(17),p);
            }
        }
    }
    @Override public boolean onTouchEvent(MotionEvent e){
        int action=e.getActionMasked(),index=e.getActionIndex();
        if(action==MotionEvent.ACTION_CANCEL){touch.clear();armed.clear();listener.keys(0,SystemClock.elapsedRealtime());invalidate();return true;}
        if(!playable){clear();return true;}
        if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN){
            getParent().requestDisallowInterceptTouchEvent(true);int id=e.getPointerId(index);
            armed.add(id);touch.move(id,hit(e.getX(index),e.getY(index)));send();
        }else if(action==MotionEvent.ACTION_MOVE){
            for(int i=0;i<e.getPointerCount();i++){int id=e.getPointerId(i);if(armed.contains(id))touch.move(id,hit(e.getX(i),e.getY(i)));}send();
        }else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP){
            int id=e.getPointerId(index);armed.remove(id);touch.up(id);send();if(action==MotionEvent.ACTION_UP)performClick();
        }
        return true;
    }
    private void send(){if(!listener.keys(touch.mask(),SystemClock.elapsedRealtime()))clear();invalidate();}
    @Override public boolean performClick(){super.performClick();return true;}
}
