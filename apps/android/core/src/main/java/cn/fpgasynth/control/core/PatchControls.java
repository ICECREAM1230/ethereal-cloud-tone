package cn.fpgasynth.control.core;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** Inverse mappings verified against frozen voice_alloc.c, never raw Hz/ms guesses. */
public final class PatchControls {
    public static final String[] WAVES={"锯齿波","脉冲波","三角波","正弦波"};
    public static final class Control {
        public final int cc; public final String group,label; public final String[] choices; public final int[] values;
        Control(int cc,String group,String label,String[] choices,int[] values){this.cc=cc;this.group=group;this.label=label;this.choices=choices;this.values=values;}
        public int value(DeviceState s){return read(cc,s);}
        public String display(DeviceState s){return describe(cc,s);}
    }
    public static List<Control> all(){
        List<Control> out=new ArrayList<>();
        add(out,7,"混合与空间","主音量");add(out,10,"混合与空间","左右声像");
        add(out,24,"混合与空间","振荡器混合");add(out,27,"混合与空间","齐奏失谐");add(out,28,"混合与空间","立体声宽度");
        pick(out,26,"混合与空间","声部结构",new String[]{"双振荡器","7 + 1 齐奏","4 + 4 齐奏"},new int[]{0,43,86});
        for(int osc=0;osc<2;osc++){
            String g="振荡器 "+(osc+1);pick(out,20+osc,g,"波形",WAVES,new int[]{0,32,64,96});
            add(out,osc==0?14:22,g,"半音");add(out,osc==0?15:23,g,"微调");add(out,osc==0?25:85,g,"脉冲宽度");
        }
        pick(out,29,"滤波","类型",new String[]{"低通 LP","带通 BP","高通 HP"},new int[]{0,43,86});
        pick(out,30,"滤波","斜率",new String[]{"12 dB/oct","24 dB/oct"},new int[]{0,64});
        add(out,74,"滤波","截止频率 · 粗调");add(out,106,"滤波","截止频率 · 细调");add(out,71,"滤波","共振");add(out,31,"滤波","键盘跟踪");
        String[] adsr={"起音 Attack","衰减 Decay","保持 Sustain","释放 Release"};
        int[] amp={73,75,79,72},mod={102,103,104,105};
        for(int i=0;i<4;i++)add(out,amp[i],"音量包络",adsr[i]);
        for(int i=0;i<4;i++)add(out,mod[i],"调制包络",adsr[i]);
        add(out,107,"调制包络","滤波调制深度");
        for(int i=0;i<2;i++){
            String g="LFO "+(i+1);pick(out,i==0?113:111,g,"波形",WAVES,new int[]{0,32,64,96});
            add(out,i==0?76:109,g,"速度");add(out,i==0?77:110,g,"深度");
        }
        pick(out,112,"LFO 2","调制目标",new String[]{"脉冲宽度","共振","音高","截止频率"},new int[]{0,32,64,96});
        add(out,86,"力度响应","力度 → 音量包络");add(out,87,"力度响应","力度 → 调制包络");
        return out;
    }
    private static void add(List<Control> a,int cc,String g,String l){a.add(new Control(cc,g,l,null,null));}
    private static void pick(List<Control> a,int cc,String g,String l,String[] c,int[] v){a.add(new Control(cc,g,l,c,v));}
    public static int clamp(double v){return Math.max(0,Math.min(127,(int)Math.round(v)));}
    public static int read(int cc,DeviceState s){
        switch(cc){
            case 7:return clamp(s.n("volume")/2.0);case 10:return clamp(s.n("pan")+64);
            case 14:case 22:return clamp(Math.ceil((s.a("osc",cc==14?0:1,1)+12)*128.0/25));
            case 15:case 23:return clamp(64+s.a("osc",cc==15?0:1,2)*1.5);
            case 20:case 21:return s.a("osc",cc-20,0)*32;
            case 24:return clamp(s.n("mix")+64);
            case 25:case 85:return clamp(-127*Math.log10(Math.max(.0001,1-s.a("osc",cc==25?0:1,3)/8388608.0)));
            case 26:return new int[]{0,43,86}[(int)s.n("voice")];case 27:return clamp(s.n("detune")*4);
            case 28:return clamp(s.n("spread"));case 29:return new int[]{0,43,86}[s.a("filter",2)];
            case 30:return s.a("filter",3)*64;case 31:return clamp(s.a("filter",4));
            case 71:return clamp((s.a("filter",1)+s.n("resonance_offset"))*127.0/5632);
            case 74:return clamp((s.n("cutoff_offset")+8192)>>7);case 106:return (int)((s.n("cutoff_offset")+8192)&127);
            case 73:return clamp(127-s.a("amp",0)/2.0);case 75:return clamp(127-s.a("amp",1)/2.0);
            case 79:return clamp(s.a("amp",2)/2.0);case 72:return clamp(127-s.a("amp",3)/2.0);
            case 102:return clamp(127-s.a("mod",0)/2.0);case 103:return clamp(127-s.a("mod",1)/2.0);
            case 104:return clamp(s.a("mod",2)/2.0);case 105:return clamp(127-s.a("mod",3)/2.0);
            case 107:return clamp(64+Math.signum(s.n("mod_depth"))*Math.sqrt(Math.abs(s.n("mod_depth"))/4.0));
            case 113:case 111:return s.a("lfo",cc==113?0:1,0)*32;
            case 76:case 109:return clamp(127*Math.log(Math.max(1,s.a("lfo",cc==76?0:1,1))*48000.0/16777216/.03)/Math.log(1000));
            case 77:return clamp(s.a("lfo",0,2)/4.0);
            case 110:int dest=s.a("lfo",1,3);return clamp(s.a("lfo",1,2)/(dest==1||dest==3?32.0:dest==2?4.0:16.0));
            case 112:return s.a("lfo",1,3)*32;case 86:case 87:return clamp(s.a("velocity",cc-86));
            default:throw new IllegalArgumentException("unknown control");
        }
    }
    private static String f(String format,double value){return String.format(Locale.CHINA,format,value);}
    public static String describe(int cc,DeviceState s){
        int v=read(cc,s);
        switch(cc){
            case 7:return f("%.0f%% · 相对音量",s.n("volume")/254.0*100);
            case 10:return s.n("pan")==0?"居中":(s.n("pan")<0?"左 ":"右 ")+Math.round(Math.abs(s.n("pan"))/64.0*100)+"%";
            case 14:case 22:return s.a("osc",cc==14?0:1,1)+" 半音";
            case 15:case 23:return f("%+.1f 音分",s.a("osc",cc==15?0:1,2)*1200.0/1024);
            case 25:case 85:return f("%.1f%% 占空比",100*(.5-s.a("osc",cc==25?0:1,3)/16777216.0));
            case 27:return f("%.1f 音分 / 齐奏步",s.n("detune")*1200.0/1024);
            case 74:return f("%+.2f 八度偏移",s.n("cutoff_offset")/1024.0);
            case 106:return "细调刻度 "+v;
            case 31:return f("%.0f%% 跟踪",s.a("filter",4)/64.0*100);
            case 107:return f("%+.2f 八度",s.n("mod_depth")/1024.0);
            case 77:return f("%.1f 音分",s.a("lfo",0,2)*1200.0/1024);
            case 110:return s.a("lfo",1,3)==2?f("%.1f 音分",s.a("lfo",1,2)*1200.0/1024):"相对深度 "+v+" / 127";
            case 20:case 21:case 113:case 111:return WAVES[v/32];
            case 26:return new String[]{"双振荡器","7 + 1 齐奏","4 + 4 齐奏"}[(int)s.n("voice")];
            case 29:return new String[]{"低通 LP","带通 BP","高通 HP"}[s.a("filter",2)];
            case 30:return s.a("filter",3)==0?"12 dB/oct":"24 dB/oct";
            case 112:return new String[]{"脉冲宽度","共振","音高","截止频率"}[s.a("lfo",1,3)];
            default:return Math.round(v/127.0*100)+"% · 相对刻度";
        }
    }
}
