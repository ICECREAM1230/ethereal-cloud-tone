package cn.fpgasynth.control.core;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** Standard BLE-MIDI packets containing complete three-byte channel messages. */
public final class BleMidiCodec {
    private BleMidiCodec(){}
    public static List<byte[]> encode(List<MidiMessage> messages,long now,int limit)throws IOException{
        if(messages==null||limit<5||limit>514)throw new IOException("BLE-MIDI消息或分包范围无效。");
        List<MidiMessage> batch=new ArrayList<>(messages);
        for(MidiMessage message:batch)if(message==null)throw new IOException("BLE-MIDI消息为空。");
        List<byte[]> packets=new ArrayList<>();
        int timestamp=(int)(now&8191),capacity=(limit-1)/4;
        for(int offset=0;offset<batch.size();){
            int count=Math.min(capacity,batch.size()-offset);byte[] packet=new byte[1+4*count];
            packet[0]=(byte)(0x80|(timestamp>>7));
            for(int i=0;i<count;i++){
                MidiMessage message=batch.get(offset+i);int at=1+4*i;
                packet[at]=(byte)(0x80|(timestamp&127));
                packet[at+1]=(byte)message.status;packet[at+2]=(byte)message.data1;packet[at+3]=(byte)message.data2;
            }
            packets.add(packet);offset+=count;
        }
        return packets;
    }
}
