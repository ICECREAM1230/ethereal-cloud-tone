package cn.fpgasynth.control.core;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** Candidate App control framing v1. It is separate from the standard BLE MIDI service. */
public final class BleControlFrame {
    public static final int HEADER=12,MAX_MESSAGE=65535,REQUEST=1,RESPONSE=2;
    private BleControlFrame(){}
    public static List<byte[]> encode(int type,long id,byte[] data,int limit)throws IOException{
        if((type!=REQUEST&&type!=RESPONSE)||id<1||id>Protocol.UINT_MAX||data==null||
                data.length<1||data.length>MAX_MESSAGE||limit<HEADER+1||limit>514)
            throw new IOException("蓝牙控制帧范围无效。");
        List<byte[]> packets=new ArrayList<>();
        for(int offset=0;offset<data.length;offset+=limit-HEADER){
            int count=Math.min(limit-HEADER,data.length-offset);byte[] p=new byte[HEADER+count];
            p[0]=83;p[1]=1;p[2]=(byte)type;p[3]=(byte)((offset==0?1:0)|(offset+count==data.length?2:0));
            for(int i=0;i<4;i++)p[4+i]=(byte)(id>>(24-i*8));
            p[8]=(byte)(offset>>8);p[9]=(byte)offset;p[10]=(byte)(data.length>>8);p[11]=(byte)data.length;
            System.arraycopy(data,offset,p,HEADER,count);packets.add(p);
        }
        return packets;
    }
    public static final class Assembler {
        private final int type;private final long id;
        private byte[] data;private int at;private boolean complete,failed;
        public Assembler(int type,long id){
            if((type!=REQUEST&&type!=RESPONSE)||id<1||id>Protocol.UINT_MAX)throw new IllegalArgumentException();
            this.type=type;this.id=id;
        }
        private IOException bad(){failed=true;return new IOException("蓝牙分片顺序或长度无效。");}
        public byte[] accept(byte[] p)throws IOException{
            if(failed)throw bad();
            if(p==null||p.length<=HEADER||p.length>514||p[0]!=83||p[1]!=1)throw bad();
            long packetId=0;for(int i=4;i<8;i++)packetId=(packetId<<8)|(p[i]&255);
            if(packetId!=id)return null;
            int flags=p[3]&255,offset=(p[8]&255)*256+(p[9]&255),total=(p[10]&255)*256+(p[11]&255),n=p.length-HEADER;
            if(complete||p[2]!=type||(flags&~3)!=0||total<1||offset+n>total||offset!=at||
                    ((flags&1)!=0)!=(data==null)||((flags&1)!=0&&offset!=0))throw bad();
            if(data==null)data=new byte[total];
            if(data.length!=total||((flags&2)!=0)!=(offset+n==total))throw bad();
            System.arraycopy(p,HEADER,data,at,n);at+=n;
            if((flags&2)!=0){complete=true;return data;}return null;
        }
    }
}
