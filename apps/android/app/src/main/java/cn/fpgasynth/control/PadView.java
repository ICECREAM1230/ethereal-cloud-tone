package cn.fpgasynth.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import cn.fpgasynth.control.core.TouchNotes;
import cn.fpgasynth.control.core.PerformanceFeedback;
import java.util.HashSet;
import java.util.Set;

/** Sixteen note triggers C3-D#4; no drum sound engine or unsupported MIDI channel is implied. */
final class PadView extends View {
    private final KeyboardView.Listener listener;
    private final TouchNotes touch=new TouchNotes();
    private final Set<Integer> armed=new HashSet<>();
    private final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF[] pads=new RectF[16];
    private boolean playable,sendOnly;
    private int owner,guideMask,nextMask;
    private long epoch=-1;
    private int columns=4;
    PadView(Context context,KeyboardView.Listener listener){super(context);this.listener=listener;setFocusable(true);setContentDescription("16个音符打击垫，C3至D升4；支持多指。边框为本地触点，采用字样为设备确认。");}
    private float dp(float n){return n*getResources().getDisplayMetrics().density;}
    void columns(int count){if(count!=4&&count!=8)throw new IllegalArgumentException();if(columns==count)return;clear();columns=count;layoutPads(getWidth(),getHeight());invalidate();}
    private void layoutPads(int w,int h){int rows=16/columns;for(int i=0;i<16;i++){int col=i%columns,row=rows-1-i/columns;float gap=dp(4);pads[i]=new RectF(col*w/(float)columns+gap,row*h/(float)rows+gap,(col+1)*w/(float)columns-gap,(row+1)*h/(float)rows-gap);}}
    @Override protected void onSizeChanged(int w,int h,int ow,int oh){layoutPads(w,h);}
    void state(boolean enabled,int owner,long epoch){state(enabled,owner,epoch,false);}
    void state(boolean enabled,int owner,long epoch,boolean directMidi){if(sendOnly!=directMidi){sendOnly=directMidi;setContentDescription(directMidi?"16个音符打击垫，C3至D升4；支持多指。边框为本地触点，已发送字样表示MIDI写入成功，未回读设备。":"16个音符打击垫，C3至D升4；支持多指。边框为本地触点，采用字样为设备确认。");}if(this.epoch!=epoch||!enabled)clear();this.epoch=epoch;this.owner=enabled?owner:0;playable=enabled;setAlpha(enabled?1:.5f);invalidate();}
    void clear(){touch.clear();armed.clear();invalidate();}
    void guide(int current,int next){current&=0xffff;next&=0xffff;if(guideMask==current&&nextMask==next)return;guideMask=current;nextMask=next;invalidate();}
    private int hit(float x,float y){for(int i=0;i<16;i++)if(pads[i]!=null&&pads[i].contains(x,y))return i;return -1;}
    @Override protected void onDraw(Canvas c){for(int i=0;i<16;i++){RectF r=pads[i];if(r==null)continue;boolean down=(touch.mask()&(1<<i))!=0,adopted=(owner&(1<<i))!=0;
        p.setStyle(Paint.Style.FILL);p.setColor(adopted?0xff294e49:ConsoleTheme.WELL);c.drawRoundRect(r,dp(10),dp(10),p);
        p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(dp(down?3:1));p.setColor(down?ConsoleTheme.AMBER:adopted?ConsoleTheme.TEAL:ConsoleTheme.BORDER);c.drawRoundRect(r,dp(10),dp(10),p);
        p.setStyle(Paint.Style.FILL);p.setTextAlign(Paint.Align.CENTER);p.setColor(ConsoleTheme.INK);p.setTextSize(dp(columns==8?16:18));c.drawText(PerformanceFeedback.notes(1<<i),r.centerX(),r.centerY()+dp(columns==8?-2:4),p);
        p.setTextSize(dp(columns==8?9:10));p.setColor(adopted?ConsoleTheme.TEAL:ConsoleTheme.MUTED);c.drawText(adopted?(sendOnly?"已发送":"已采用"):down?"触控":"音符 "+(48+i),r.centerX(),r.bottom-dp(9),p);
        if(((guideMask|nextMask)&(1<<i))!=0){boolean current=(guideMask&(1<<i))!=0;p.setColor(ConsoleTheme.GUIDE);p.setTextAlign(Paint.Align.LEFT);p.setTextSize(dp(11));c.drawText(current?"弹":"下一个",r.left+dp(7),r.top+dp(16),p);}
    }}
    private void send(){if(!listener.keys(touch.mask(),SystemClock.elapsedRealtime()))clear();invalidate();}
    @Override public boolean onTouchEvent(MotionEvent e){int action=e.getActionMasked(),index=e.getActionIndex();
        if(action==MotionEvent.ACTION_CANCEL){clear();listener.keys(0,SystemClock.elapsedRealtime());return true;}
        if(!playable){clear();return true;}
        if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN){getParent().requestDisallowInterceptTouchEvent(true);int id=e.getPointerId(index);armed.add(id);touch.move(id,hit(e.getX(index),e.getY(index)));send();}
        else if(action==MotionEvent.ACTION_MOVE){for(int i=0;i<e.getPointerCount();i++){int id=e.getPointerId(i);if(armed.contains(id))touch.move(id,hit(e.getX(i),e.getY(i)));}send();}
        else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP){int id=e.getPointerId(index);armed.remove(id);touch.up(id);send();if(action==MotionEvent.ACTION_UP)performClick();}
        return true;
    }
    @Override public boolean performClick(){super.performClick();return true;}
}
