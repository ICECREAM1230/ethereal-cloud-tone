package cn.fpgasynth.control.core;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

/** Bounded request/response port over an injected BLE link. Never replays failed writes. */
public final class BleControlTransport implements ControlPort {
    public interface Receiver {void received(byte[] packet);void failed(IOException error);}
    public interface Link {
        void setReceiver(Receiver receiver);
        void open()throws IOException;
        int packetLimit();
        void write(byte[] packet)throws IOException;
        void close();
    }
    private final Link link;
    private final long timeoutMs;
    private final Object state=new Object(),requests=new Object();
    private boolean closed,opened;
    private long nextId=1;
    private BleControlFrame.Assembler assembler;
    private byte[] response;
    private IOException failure;
    public BleControlTransport(Link link){this(link,1000);}
    public BleControlTransport(Link link,long timeoutMs){
        if(link==null||timeoutMs<1||timeoutMs>10000)throw new IllegalArgumentException();
        this.link=link;this.timeoutMs=timeoutMs;
        link.setReceiver(new Receiver(){
            public void received(byte[] packet){synchronized(state){
                if(closed||assembler==null||failure!=null)return;
                try{byte[] result=assembler.accept(packet);if(result!=null)response=result;}
                catch(IOException e){failure=e;}state.notifyAll();
            }}
            public void failed(IOException error){synchronized(state){if(!closed){failure=error;state.notifyAll();}}}
        });
    }
    @Override public Mode mode(){return Mode.BLUETOOTH;}
    private void requireLive()throws IOException{if(closed)throw new IOException("蓝牙控制端口已关闭，请重新连接。");if(failure!=null)throw failure;}
    @Override public Response request(String path,String body)throws IOException{
        ControlPort.validateRequest(path,body);
        synchronized(requests){
            try{
                synchronized(state){requireLive();}
                if(!opened){link.open();opened=true;}
                long id;
                synchronized(state){requireLive();if(nextId>Protocol.UINT_MAX)throw new IOException("蓝牙请求序号耗尽。");
                    id=nextId++;assembler=new BleControlFrame.Assembler(BleControlFrame.RESPONSE,id);response=null;}
                Map<String,Object> m=new LinkedHashMap<>();m.put("path",path);m.put("body",body);
                byte[] payload=Json.stringify(m).getBytes(StandardCharsets.UTF_8);
                long deadline=System.nanoTime()+TimeUnit.MILLISECONDS.toNanos(timeoutMs);
                for(byte[] packet:BleControlFrame.encode(BleControlFrame.REQUEST,id,payload,link.packetLimit())){
                    synchronized(state){requireLive();}
                    if(System.nanoTime()>=deadline)throw new IOException("蓝牙控制发送超时。");
                    link.write(packet);
                    if(System.nanoTime()>=deadline)throw new IOException("蓝牙控制发送超时；请求未重发。");
                }
                byte[] received;
                synchronized(state){
                    while(response==null){requireLive();long left=deadline-System.nanoTime();
                        if(left<=0)throw new IOException("蓝牙控制响应超时；请求未重发。");
                        try{TimeUnit.NANOSECONDS.timedWait(state,left);}catch(InterruptedException e){Thread.currentThread().interrupt();throw new IOException("蓝牙请求已取消。",e);}
                    }
                    requireLive();if(System.nanoTime()>=deadline)throw new IOException("蓝牙控制响应超时；请求未重发。");
                    received=response;assembler=null;response=null;
                }
                String text;
                try{text=StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                        .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(received)).toString();}
                catch(CharacterCodingException e){throw new IOException("蓝牙响应不是有效UTF-8。",e);}
                Map<String,Object> envelope=Json.object(text);
                long code=Json.number(envelope.get("code"));
                if(envelope.size()!=2||code<100||code>599||!(envelope.get("body") instanceof String))throw new IOException("蓝牙响应格式无效。");
                if(System.nanoTime()>=deadline)throw new IOException("蓝牙控制响应超时；请求未重发。");
                return new Response((int)code,(String)envelope.get("body"));
            }catch(IOException|IllegalArgumentException e){close();if(e instanceof IOException)throw (IOException)e;throw new IOException("蓝牙响应协议无效。",e);}
        }
    }
    @Override public void close(){
        synchronized(state){if(closed)return;closed=true;assembler=null;response=null;state.notifyAll();}
        link.close();
    }
}
