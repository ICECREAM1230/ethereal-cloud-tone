package cn.fpgasynth.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.view.View;
import cn.fpgasynth.control.core.PerformanceFeedback;

/** The large live panel displays control truth; absent audio never generates a decorative VU. */
final class PerformanceView extends View {
    private final PerformanceFeedback model;
    private final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    PerformanceView(Context context,PerformanceFeedback model){super(context);this.model=model;setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);}
    private float dp(float n){return n*getResources().getDisplayMetrics().density;}
    void changed(){PerformanceFeedback.Scene s=model.snapshot(SystemClock.elapsedRealtime());setContentDescription((s.simulation?"模拟演奏，":"演奏反馈，")+"本地触点"+s.localCount+(s.sendOnly?"，已发送":"，设备采用")+s.adoptedCount+"，"+(s.audioAvailable?"音频数据已接入":"音频数据未接入"));invalidate();}
    private void text(Canvas c,String s,float x,float y,float size,int color){p.setStyle(Paint.Style.FILL);p.setColor(color);p.setTextSize(dp(size));p.setTypeface(Typeface.DEFAULT);p.setTextAlign(Paint.Align.LEFT);c.drawText(s,x,y,p);}
    @Override protected void onDraw(Canvas c){
        PerformanceFeedback.Scene s=model.snapshot(SystemClock.elapsedRealtime());float w=getWidth(),h=getHeight(),g=dp(14),keysY=dp(70),keyH=dp(35);
        p.setColor(ConsoleTheme.WELL);c.drawRoundRect(0,0,w,h,dp(12),dp(12),p);
        if(h<dp(100)){
            String names=s.localNotes.isEmpty()?s.adoptedNotes:s.localNotes;
            text(c,"触控 "+s.localCount+(s.sendOnly?" / 发送 ":" / 采用 ")+(s.playable?s.adoptedCount:"—")+"    "+names,g,dp(19),12,ConsoleTheme.INK);
            p.setTextAlign(Paint.Align.RIGHT);p.setColor(ConsoleTheme.MUTED);p.setTextSize(dp(10));c.drawText(s.audioAvailable?"音频":"音频 —",w-g,dp(19),p);
            float unit=(w-2*g)/25f;for(int i=0;i<25;i++){p.setColor((s.adoptedMask&(1<<i))!=0?ConsoleTheme.TEAL:ConsoleTheme.PANEL);c.drawRect(g+i*unit+1,dp(27),g+(i+1)*unit-2,dp(33),p);if((s.localMask&(1<<i))!=0){p.setColor(ConsoleTheme.AMBER);c.drawRect(g+i*unit+1,dp(27),g+(i+1)*unit-2,dp(29),p);}}
            return;
        }
        text(c,s.simulation?"模拟演奏反馈":"演奏反馈",g,dp(25),13,ConsoleTheme.MUTED);
        String state=!s.playable?"等待连接":(s.pendingOn|s.pendingOff)!=0?(s.sendOnly?"等待发送":"等待采用"):s.adoptedCount>0?(s.sendOnly?"已发送":"设备已采用"):"可以演奏";
        p.setTextAlign(Paint.Align.RIGHT);p.setTextSize(dp(12));p.setColor((s.pendingOn|s.pendingOff)!=0?ConsoleTheme.AMBER:ConsoleTheme.TEAL);c.drawText(state,w-g,dp(25),p);
        String notes=s.localNotes.isEmpty()?s.adoptedNotes:s.localNotes;if(notes.isEmpty())notes="C3 — C5";
        text(c,notes,g,dp(53),Math.min(20,Math.max(12,28-notes.length()/3f)),s.playable?ConsoleTheme.INK:ConsoleTheme.MUTED);
        float unit=(w-2*g)/25f;
        for(int i=0;i<25;i++){boolean down=(s.localMask&(1<<i))!=0,adopted=(s.adoptedMask&(1<<i))!=0;float x=g+i*unit;
            p.setColor(adopted?ConsoleTheme.TEAL:ConsoleTheme.PANEL);c.drawRoundRect(x+1,keysY,x+unit-2,keysY+keyH,dp(2),dp(2),p);
            if(down){p.setColor(ConsoleTheme.AMBER);c.drawRect(x+1,keysY,x+unit-2,keysY+dp(5),p);}
        }
        text(c,"触控 "+s.localCount+(s.sendOnly?"     发送 ":"     采用 ")+s.adoptedCount+"     力度 "+s.velocity,g,dp(129),12,ConsoleTheme.MUTED);
        text(c,s.audioAvailable?"设备音频 / RMS · Peak":"音频电平  —  等待设备数据",g,dp(153),11,ConsoleTheme.MUTED);
        if(s.audioAvailable){float top=dp(166),barWidth=w-2*g;double[] rms={s.leftRms,s.rightRms};
            for(int i=0;i<2;i++){p.setColor(ConsoleTheme.PANEL);c.drawRect(g,top+i*dp(10),g+barWidth,top+dp(5)+i*dp(10),p);p.setColor(ConsoleTheme.TEAL);c.drawRect(g,top+i*dp(10),g+(float)(barWidth*rms[i]),top+dp(5)+i*dp(10),p);}postInvalidateDelayed(50);
        }
    }
}
