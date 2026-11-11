package cn.fpgasynth.control;

import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.content.res.ColorStateList;

/** Instrument panel tokens shared by controls and feedback. */
final class ConsoleTheme {
    static final int BG=Color.rgb(21,29,37),PANEL=Color.rgb(32,44,54),WELL=Color.rgb(14,22,29),
            INK=Color.rgb(240,238,230),MUTED=Color.rgb(178,191,199),TEAL=Color.rgb(118,215,196),
            AMBER=Color.rgb(240,185,105),GUIDE=Color.rgb(193,173,255),BORDER=Color.rgb(91,111,125),ERROR=Color.rgb(244,142,132);
    static GradientDrawable panel(int fill,int stroke,float radius){GradientDrawable g=new GradientDrawable();g.setColor(fill);g.setCornerRadius(radius);if(stroke!=0)g.setStroke(1,stroke);return g;}
    static RippleDrawable button(int fill,float radius){return new RippleDrawable(ColorStateList.valueOf(0x4476D7C4),panel(fill,BORDER,radius),null);}
    private ConsoleTheme(){}
}
