package cn.fpgasynth.control.core;

/** Relative illustration widths, not physical time units or rendered audio. */
public final class EnvelopeShape {
    private EnvelopeShape(){}
    private static float time(int raw){return 8+254-Math.max(0,Math.min(254,raw));}
    public static float[] widths(int attack,int decay,int release){float a=time(attack),d=time(decay),r=time(release),hold=80,total=a+d+hold+r;return new float[]{a/total,d/total,hold/total,r/total};}
}
