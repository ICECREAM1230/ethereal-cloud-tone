package cn.fpgasynth.control.core;

import java.util.List;
import java.util.Map;

/** Owner acknowledgement is distinct from FPGA/audio completion. */
public final class DeviceState {
    public final Map<String,Object> raw;
    public final String json;
    public final long[] tokens, processed, parameters, stops, keys;
    public final boolean ready, available, valid, switching, recovering, busy;
    public final int requested, applied, decay;
    public final long panic, recoveries, fault;
    public DeviceState(String json) {
        this.json=json; raw=Json.object(json);
        ready=Json.bool(raw.get("ready")); panic=n("panic");
        tokens=array("tokens"); processed=array("processed"); parameters=array("parameter_processed");
        stops=array("stop_processed"); keys=array("keys");
        for(long mask:keys) if(mask>Protocol.MASK_MAX)throw new IllegalArgumentException("invalid keys");
        Map<String,Object> e=raw.containsKey("engine")?Json.map(raw.get("engine")):null;
        available=e!=null && Json.bool(e.get("available")); valid=e!=null && Json.bool(e.get("valid"));
        switching=e!=null && Json.bool(e.get("switching")); recovering=e!=null && Json.bool(e.get("recovering"));
        busy=e!=null && Json.bool(e.get("busy"));
        requested=e==null?0:(int)Json.number(e.get("requested")); applied=e==null?0:(int)Json.number(e.get("applied"));
        fault=e==null?0:Json.number(e.get("fault")); recoveries=e==null?0:Json.number(e.get("recoveries"));
        if(requested<0||requested>1||applied<0||applied>1)throw new IllegalArgumentException("unknown engine");
        decay=raw.containsKey("ks_decay_ms")?(int)n("ks_decay_ms"):3000;
    }
    private long[] array(String key) {
        List<Object> a=Json.list(raw.get(key)); if(a.size()!=4)throw new IllegalArgumentException("invalid owner array");
        long[] out=new long[4]; for(int i=0;i<4;i++){out[i]=Json.number(a.get(i)); if(out[i]<0||out[i]>Protocol.UINT_MAX)throw new IllegalArgumentException("uint32 overflow");} return out;
    }
    public long n(String key) { return Json.number(raw.get(key)); }
    public int a(String key,int i) { return (int)Json.number(Json.list(raw.get(key)).get(i)); }
    public int a(String key,int i,int j) { return (int)Json.number(Json.list(Json.list(raw.get(key)).get(i)).get(j)); }
    public boolean playable() {
        // Firmware retains fault as the last historical fault even after a safe recovery.
        return ready && !switching && !recovering && requested==applied
                && (valid || (!available && requested==0));
    }
}
