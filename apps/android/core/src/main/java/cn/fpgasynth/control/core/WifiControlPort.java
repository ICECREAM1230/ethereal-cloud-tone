package cn.fpgasynth.control.core;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/** HTTP port; Android injects Network.openConnection to pin every request to Wi-Fi. */
public final class WifiControlPort implements ControlPort {
    public interface Connections {HttpURLConnection open(URL url)throws IOException;}
    private final String endpoint;
    private final Connections connections;
    private final Object state=new Object(),requests=new Object();
    private boolean closed;
    private HttpURLConnection current;
    public WifiControlPort(String origin,Connections connections){
        endpoint=Protocol.endpoint(origin);if(connections==null)throw new IllegalArgumentException();this.connections=connections;
    }
    @Override public Mode mode(){return Mode.WIFI;}
    @Override public Response request(String path,String body)throws IOException{
        ControlPort.validateRequest(path,body);
        if(body!=null)for(int i=0;i<body.length();i++)if(body.charAt(i)>127)throw new IOException("Wi-Fi控制命令必须使用原始ASCII格式。");
        synchronized(requests){
            synchronized(state){if(closed)throw new IOException("Wi-Fi控制端口已关闭。");}
            HttpURLConnection c=connections.open(new URL(endpoint+path));
            synchronized(state){if(closed){c.disconnect();throw new IOException("Wi-Fi控制端口已关闭。");}current=c;}
            try{
                c.setConnectTimeout(350);c.setReadTimeout(350);c.setInstanceFollowRedirects(false);c.setUseCaches(false);
                c.setRequestProperty("Accept","application/json");
                if(body!=null){
                    byte[] data=body.getBytes(StandardCharsets.US_ASCII);c.setRequestMethod("POST");
                    c.setRequestProperty("X-FPGA-Control","1");c.setRequestProperty("Content-Type","text/plain");
                    c.setDoOutput(true);c.setFixedLengthStreamingMode(data.length);
                    try(java.io.OutputStream out=c.getOutputStream()){out.write(data);}
                }
                int code=c.getResponseCode();InputStream input=code>=400?c.getErrorStream():c.getInputStream();
                ByteArrayOutputStream out=new ByteArrayOutputStream();
                if(input!=null)try(InputStream in=input){byte[] buf=new byte[2048];int n;
                    while((n=in.read(buf))!=-1){if(out.size()+n>65536)throw new IOException("设备响应过大。");out.write(buf,0,n);}}
                synchronized(state){if(closed)throw new IOException("Wi-Fi响应来自已关闭的端口。");}
                return new Response(code,new String(out.toByteArray(),StandardCharsets.UTF_8));
            }finally{synchronized(state){if(current==c)current=null;}c.disconnect();}
        }
    }
    @Override public void close(){
        HttpURLConnection c;synchronized(state){if(closed)return;closed=true;c=current;current=null;}
        if(c!=null)c.disconnect();
    }
}
