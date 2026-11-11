package cn.fpgasynth.control.core;

import java.util.List;
import java.util.Map;

/** Local display conversion for the existing 38 CCs; this does not read device state. */
public final class LocalPatchState {
    private LocalPatchState(){}
    private static void set(Map<String,Object> state,String key,int index,long value){Json.list(state.get(key)).set(index,value);}
    private static List<Object> array(Map<String,Object> state,String key,int index){return Json.list(Json.list(state.get(key)).get(index));}
    private static void nested(Map<String,Object> state,String key,int index,int field,long value){array(state,key,index).set(field,value);}
    private static int n(Map<String,Object> state,String key,int index,int field){return (int)Json.number(array(state,key,index).get(field));}
    /** Returns false for an unmapped raw CC without changing the local patch. */
    public static boolean applyCC(Map<String,Object> state,int cc,int v){
        if(cc<0||cc>127||v<0||v>127)throw new IllegalArgumentException("CC outside 0..127");
        switch(cc){
            case 7:state.put("volume",(long)v*2);break;case 10:state.put("pan",(long)v-64);break;
            case 14:case 22:nested(state,"osc",cc==14?0:1,1,v*25/128-12);break;
            case 15:case 23:nested(state,"osc",cc==15?0:1,2,(v-64)*2/3);break;
            case 20:case 21:nested(state,"osc",cc-20,0,v>>5);break;
            case 24:state.put("mix",(long)v-64);break;
            case 25:case 85:nested(state,"osc",cc==25?0:1,3,(long)((.5-.5*Math.pow(10,-v/127.0))*16777216));break;
            case 26:state.put("voice",(long)((v*3)>>7));break;case 27:state.put("detune",(long)(v>>2));break;
            case 28:state.put("spread",(long)v);break;case 29:Json.list(state.get("filter")).set(2,(long)((v*3)>>7));break;
            case 30:Json.list(state.get("filter")).set(3,v>=64?1L:0L);break;case 31:Json.list(state.get("filter")).set(4,(long)v);break;
            case 71:state.put("resonance_offset",v*5632L/127-Json.number(Json.list(state.get("filter")).get(1)));break;
            case 74:state.put("cutoff_offset",v*128L+((Json.number(state.get("cutoff_offset"))+8192)&127)-8192);break;
            case 106:state.put("cutoff_offset",((Json.number(state.get("cutoff_offset"))+8192)&~127L)+v-8192);break;
            case 73:case 75:case 79:case 72:{int i=cc==73?0:cc==75?1:cc==79?2:3;set(state,"amp",i,(i==2?v:127-v)*2L);break;}
            case 102:case 103:case 104:case 105:set(state,"mod",cc-102,(cc==104?v:127-v)*2L);break;
            case 107:state.put("mod_depth",(v-64)*Math.abs(v-64)*4L);break;
            case 113:case 111:nested(state,"lfo",cc==113?0:1,0,v>>5);break;
            case 76:case 109:nested(state,"lfo",cc==76?0:1,1,Math.round(.03*Math.pow(1000,v/127.0)*16777216/48000));break;
            case 77:nested(state,"lfo",0,2,v*4);break;
            case 110:int d=n(state,"lfo",1,3);nested(state,"lfo",1,2,v*(d==1||d==3?32:d==2?4:16));break;
            case 112:nested(state,"lfo",1,3,v*4>>7);break;
            case 86:case 87:set(state,"velocity",cc-86,v);break;
            default:return false;
        }
        return true;
    }
}
