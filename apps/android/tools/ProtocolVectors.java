import cn.fpgasynth.control.core.Protocol;
public final class ProtocolVectors {
    public static void main(String[] args){
        long seq=1;
        for(int cc:Protocol.CCS)for(int value:new int[]{0,1,32,64,96,127})System.out.println(Protocol.encode(Protocol.UINT_MAX,seq++,2,cc,value));
        for(int mask:new int[]{0,1,2,3,1<<12,1<<24,Protocol.MASK_MAX})for(int velocity:new int[]{1,64,127})System.out.println(Protocol.encode(1,seq++,3,mask,velocity));
        System.out.println(Protocol.encode(1,seq++,4,0,0));
        for(int mode:new int[]{0,1})System.out.println(Protocol.encode(1,seq++,5,mode,0));
        for(int ms:new int[]{200,201,3000,7999,8000})System.out.println(Protocol.encode(1,seq++,6,ms,0));
    }
}
