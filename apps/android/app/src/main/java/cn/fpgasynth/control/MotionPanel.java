package cn.fpgasynth.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import cn.fpgasynth.control.core.MotionInstrument;
import cn.fpgasynth.control.core.PerformanceFeedback;

/** A compact live sensor instrument; controls never imply that hardware audio was heard. */
public final class MotionPanel extends LinearLayout {
    interface Listener {void calibrate();void toggle();void mapping();}
    final Button calibrate,start,mapping;private final TextView status;private final Meter meter;private MotionInstrument.Frame frame;
    private String midiPreview="MIDI —";private int outputNote=60;
    MotionPanel(Context context,Listener listener){
        super(context);setOrientation(VERTICAL);setPadding(dp(4),0,dp(4),0);
        LinearLayout bar=new LinearLayout(context);bar.setGravity(Gravity.CENTER_VERTICAL);
        status=new TextView(context);status.setTextColor(ConsoleTheme.MUTED);status.setTextSize(12);status.setMaxLines(2);bar.addView(status,new LayoutParams(0,dp(48),1));
        mapping=button(context,"MIDI",listener::mapping);calibrate=button(context,"校准",listener::calibrate);start=button(context,"开始",listener::toggle);bar.addView(mapping,new LayoutParams(dp(72),dp(48)));bar.addView(calibrate,new LayoutParams(dp(80),dp(48)));bar.addView(start,new LayoutParams(dp(80),dp(48)));addView(bar);
        meter=new Meter(context);addView(meter,new LayoutParams(-1,0,1));setContentDescription("体感演奏：校准后开始，左右倾斜选音，挥动手机拨弦");
    }
    private int dp(float n){return Math.round(n*getResources().getDisplayMetrics().density);}
    private Button button(Context context,String label,Runnable action){Button b=new Button(context);b.setText(label);b.setAllCaps(false);b.setTextSize(14);b.setTextColor(ConsoleTheme.TEAL);b.setBackground(ConsoleTheme.button(ConsoleTheme.WELL,dp(7)));b.setPadding(dp(5),0,dp(5),0);b.setOnClickListener(v->action.run());return b;}
    void update(MotionInstrument.Frame state,boolean available,boolean eligible,String source,boolean supported,String midi,int note){
        frame=state;midiPreview=midi;outputNote=note;status.setText(!available?"传感器不可用":!supported?"映射不可用":!state.fresh&&"请校准".equals(state.status)?"等待传感器":state.status);
        status.setContentDescription(status.getText()+"，"+source);
        calibrate.setEnabled(available&&state.fresh&&state.force<1.5);
        start.setEnabled(state.armed||(available&&eligible&&supported&&state.fresh&&state.calibrated&&state.force<1.5));start.setText(state.armed?"停止":"开始");meter.invalidate();
    }
    private final class Meter extends View {
        private final Paint p=new Paint(3);
        Meter(Context context){super(context);setFocusable(true);}
        @Override protected void onDraw(Canvas c){
            float w=getWidth(),h=getHeight(),sp=getResources().getDisplayMetrics().scaledDensity;
            p.setColor(ConsoleTheme.WELL);c.drawRoundRect(0,dp(2),w,h,dp(8),dp(8),p);
            p.setTextAlign(Paint.Align.LEFT);p.setTypeface(Typeface.DEFAULT_BOLD);p.setTextSize(Math.min(23*sp,Math.max(dp(16),h*.4f)));p.setColor(frame!=null&&frame.mask!=0?ConsoleTheme.TEAL:ConsoleTheme.INK);
            String note=frame!=null&&frame.fresh?PerformanceFeedback.notes(1<<(outputNote-48)):"—";c.drawText(note,dp(14),Math.max(dp(30),h*.46f),p);
            p.setTypeface(Typeface.DEFAULT);p.setTextSize(11*sp);p.setColor(ConsoleTheme.MUTED);
            String reading=frame!=null&&frame.fresh?String.format(java.util.Locale.ROOT,"%+.0f°  ·  %.1f m/s²",frame.roll,frame.force):"倾斜选音 · 挥动拨弦";
            c.drawText(h>dp(120)?reading:midiPreview,dp(14),Math.max(dp(49),h*.68f),p);if(h>dp(120))c.drawText(midiPreview,dp(14),h*.84f,p);
            float left=w*.48f,right=w-dp(18),y=h*.5f;p.setColor(ConsoleTheme.BORDER);p.setStrokeWidth(dp(2));c.drawLine(left,y,right,y,p);
            for(int i=0;i<15;i++)c.drawCircle(left+(right-left)*i/14,y,dp(2),p);
            if(frame!=null&&frame.fresh){float x=left+(right-left)*(float)Math.max(0,Math.min(1,(frame.roll+35)/70));p.setColor(frame.armed?ConsoleTheme.TEAL:ConsoleTheme.AMBER);c.drawCircle(x,y,dp(7),p);}
        }
    }
}
