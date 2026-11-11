package cn.fpgasynth.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.View;
import cn.fpgasynth.control.core.DeviceState;
import cn.fpgasynth.control.core.PatchControls;
import cn.fpgasynth.control.core.EnvelopeShape;
import java.util.Arrays;

/** Parameter illustration only: not a sampled waveform or measured frequency response. */
final class SynthSketch extends View {
    private final String kind;
    private final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    private int[] values;
    SynthSketch(Context context,String kind){super(context);this.kind=kind;setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
    private float dp(float n){return n*getResources().getDisplayMetrics().density;}
    void state(DeviceState state,boolean enabled){
        int[] next=null;if(state!=null)try{
            if(kind.startsWith("osc")){int osc=kind.endsWith("2")?1:0;next=new int[]{state.a("osc",osc,0),state.a("osc",osc,3)};}
            else if(kind.equals("amp")||kind.equals("mod")){String type=kind.equals("amp")?"amp":"mod";next=new int[]{state.a(type,0),state.a(type,1),state.a(type,2),state.a(type,3)};}
            else if(kind.startsWith("lfo")){int i=kind.endsWith("2")?1:0;next=new int[]{state.a("lfo",i,0),state.a("lfo",i,1)};}
            else next=new int[]{state.a("filter",2),PatchControls.read(74,state),PatchControls.read(71,state)};
        }catch(RuntimeException ignored){}
        if(!Arrays.equals(values,next)){values=next;invalidate();}setAlpha(enabled?1:.38f);
    }
    @Override protected void onDraw(Canvas c){float w=getWidth(),h=getHeight(),g=dp(8),top=dp(8),bottom=h-dp(20),span=bottom-top;
        p.setColor(ConsoleTheme.WELL);p.setStyle(Paint.Style.FILL);c.drawRoundRect(0,0,w,h,dp(6),dp(6),p);
        p.setColor(0xff344550);p.setStrokeWidth(1);for(int i=1;i<8;i++)c.drawLine(w*i/8,top,w*i/8,bottom,p);for(int i=0;i<3;i++)c.drawLine(g,top+span*i/2,w-g,top+span*i/2,p);
        if(values==null)return;
        Path path=new Path();
        if(kind.equals("amp")||kind.equals("mod")){
            float[] duration=EnvelopeShape.widths(values[0],values[1],values[3]);float sustain=Math.max(0,Math.min(1,values[2]/254f));
            path.moveTo(g,bottom);path.lineTo(g+(w-2*g)*duration[0],top);path.lineTo(g+(w-2*g)*(duration[0]+duration[1]),bottom-span*sustain);path.lineTo(g+(w-2*g)*(duration[0]+duration[1]+duration[2]),bottom-span*sustain);path.lineTo(w-g,bottom);
        }else{
            for(int i=0;i<=180;i++){double x=i/180.0,y;
                if(kind.equals("filter")){double cut=.08+.82*values[1]/127.0,low=1/(1+Math.exp((x-cut)*18));y=values[0]==0?low:values[0]==2?1-low:Math.exp(-Math.pow((x-cut)*5,2));y=Math.min(1,y+.12*values[2]/127.0*Math.exp(-Math.pow((x-cut)*20,2)));y=1-2*y;}
                else{double phase=(x*(kind.startsWith("lfo")?2:2.5))%1,duty=kind.startsWith("osc")?.5-values[1]/16777216.0:.5;
                    y=values[0]==0?2*phase-1:values[0]==1?(phase<duty?-1:1):values[0]==2?1-4*Math.abs(phase-.5):Math.sin(phase*Math.PI*2);}
                float px=g+(w-2*g)*(float)x,py=top+span*(float)(.5+.43*y);if(i==0)path.moveTo(px,py);else path.lineTo(px,py);
            }
        }
        p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(dp(1.7f));p.setColor(kind.equals("mod")?ConsoleTheme.AMBER:ConsoleTheme.TEAL);c.drawPath(path,p);p.setStyle(Paint.Style.FILL);
    }
}
