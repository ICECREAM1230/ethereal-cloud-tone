import cn.fpgasynth.control.core.DemoDevice;
import cn.fpgasynth.control.core.Json;
import cn.fpgasynth.control.core.Transport;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.concurrent.Executors;

/** Local-only HTTP simulator. /__mock__/fault is test infrastructure, never a firmware API. */
public final class MockServer {
    private static volatile long offset,postedAt;
    private static volatile int ownerDelay=40;
    private static volatile boolean frozen,dropNote;
    private static long now(){return System.nanoTime()/1000000+offset;}
    private static final DemoDevice device=new DemoDevice(MockServer::now){
        @Override public synchronized void adopt(){if(!frozen && now()-postedAt>=ownerDelay)super.adopt();}
    };
    static {device.mutableState().put("mock",true);}
    private static void send(HttpExchange x,int status,String body) throws IOException {
        byte[] bytes=body.getBytes(StandardCharsets.UTF_8);
        x.getResponseHeaders().set("Content-Type","application/json; charset=utf-8");
        x.getResponseHeaders().set("Cache-Control","no-store");
        x.sendResponseHeaders(status,bytes.length);x.getResponseBody().write(bytes);x.close();
    }
    private static String read(HttpExchange x)throws IOException {
        ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] b=new byte[128];int n;
        while((n=x.getRequestBody().read(b))!=-1){out.write(b,0,n);if(out.size()>65536)throw new IOException("body too large");}
        return new String(out.toByteArray(),StandardCharsets.US_ASCII);
    }
    private static void handle(HttpExchange x)throws IOException {
        String path=x.getRequestURI().getPath(),method=x.getRequestMethod();String body=method.equals("POST")?read(x):null;
        if(path.equals("/__mock__/fault")&&method.equals("POST")){
            Map<String,Object> f=Json.object(body);
            if(f.containsKey("offset_ms"))offset=Json.number(f.get("offset_ms"));
            if(f.containsKey("owner_delay_ms"))ownerDelay=(int)Json.number(f.get("owner_delay_ms"));
            if(f.containsKey("freeze_owner"))frozen=Json.bool(f.get("freeze_owner"));
            if(f.containsKey("drop_next_note_response"))dropNote=Json.bool(f.get("drop_next_note_response"));
            send(x,200,"{\"mock\":true}");return;
        }
        if(path.equals("/")){send(x,200,"{\"mock\":true,\"audio\":false,\"hint\":\"Android emulator: http://10.0.2.2:"+x.getLocalAddress().getPort()+"\"}");return;}
        if((path.equals("/api/session")||path.equals("/api/command"))&&!method.equals("POST")){send(x,405,"{}");return;}
        if(path.equals("/api/state")&&!method.equals("GET")){send(x,405,"{}");return;}
        if(method.equals("POST") && !"1".equals(x.getRequestHeaders().getFirst("X-FPGA-Control"))){send(x,403,"{\"error\":\"header required\"}");return;}
        Transport.Response r=device.request(path,body);
        System.out.println("HTTP "+x.getRemoteAddress()+" "+method+" "+path+" body="+(body==null?0:body.length())+" status="+r.code);System.out.flush();
        if(path.equals("/api/command")&&r.code==202){
            postedAt=now();
            if(dropNote && body.split(" ")[2].equals("3") && !body.split(" ")[3].equals("0")){
                dropNote=false;x.close();return; // already accepted; no response, never re-send NoteOn
            }
        }
        send(x,r.code,r.body);
    }
    public static void main(String[] args)throws Exception {
        int port=args.length>0?Integer.parseInt(args[0]):8765;
        HttpServer server=HttpServer.create(new InetSocketAddress("127.0.0.1",port),0);
        server.createContext("/",x->{try{handle(x);}catch(Exception e){try{send(x,400,"{\"error\":\"invalid request\"}");}catch(Exception ignored){x.close();}}});
        server.setExecutor(Executors.newCachedThreadPool());server.start();
        System.out.println("MOCK_PORT="+server.getAddress().getPort());System.out.flush();
    }
}
