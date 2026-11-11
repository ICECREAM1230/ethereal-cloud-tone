package cn.fpgasynth.control.core;

import java.util.UUID;

/** Proposed private service; a future device must implement it before App control works. */
public final class BleControlProfile {
    public static final String SERVICE="7da0a3c0-24f9-4f0c-9ac6-8b52b2b7d2d0";
    public static final String REQUEST="7da0a3c1-24f9-4f0c-9ac6-8b52b2b7d2d0";
    public static final String RESPONSE="7da0a3c2-24f9-4f0c-9ac6-8b52b2b7d2d0";
    public final UUID service,request,response;
    public BleControlProfile(){this(SERVICE,REQUEST,RESPONSE);}
    public BleControlProfile(String service,String request,String response){
        this.service=parse(service);this.request=parse(request);this.response=parse(response);
        if(this.service.equals(this.request)||this.service.equals(this.response)||this.request.equals(this.response))
            throw new IllegalArgumentException("服务和收发特征UUID必须各不相同。");
    }
    private static UUID parse(String value){
        if(value==null||!value.matches("[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}"))throw new IllegalArgumentException("请输入完整UUID。");
        if(value.equalsIgnoreCase("03b80e5a-ede8-4b33-a751-6ce34ec4c700")||value.equalsIgnoreCase("7772e5db-3868-4112-a1a9-f2669d106bf3"))
            throw new IllegalArgumentException("私有控制接口不能使用标准BLE MIDI UUID。");
        return UUID.fromString(value);
    }
}
