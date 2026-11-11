package cn.fpgasynth.control.core;
import com.sun.net.httpserver.HttpServer;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
/** Real localhost sockets around the labelled owner model; not a firmware/audio substitute. */
public final class RuntimeHttpTest {
    private static int count;
    private static void check(boolean b,String message){count++;if(!b)throw new AssertionError(message);}
    public static void main(String[] args)throws Exception{
        RuntimeModel model=new RuntimeModel(Paths.get(args[0]));HttpServer server=HttpServer.create(new InetSocketAddress("127.0.0.1",0),0);
        server.createContext("/",ex->{try{
            ByteArrayOutputStream in=new ByteArrayOutputStream();byte[] buffer=new byte[256];int n;while((n=ex.getRequestBody().read(buffer))!=-1)in.write(buffer,0,n);
            boolean post="POST".equals(ex.getRequestMethod());Transport.Response r;
            if(post&&!"1".equals(ex.getRequestHeaders().getFirst("X-FPGA-Control")))r=new Transport.Response(403,"{\"code\":\"header_required\"}");
            else r=model.request(ex.getRequestURI().toString(),post?new String(in.toByteArray(),StandardCharsets.US_ASCII):null);
            byte[] out=r.body.getBytes(StandardCharsets.UTF_8);ex.getResponseHeaders().set("Content-Type","application/json");ex.sendResponseHeaders(r.code,out.length);ex.getResponseBody().write(out);
        }finally{ex.close();}});server.start();
        String endpoint="http://127.0.0.1:"+server.getAddress().getPort();
        Transport http=(path,body)->{
            HttpURLConnection c=(HttpURLConnection)new URL(endpoint+path).openConnection();c.setConnectTimeout(350);c.setReadTimeout(350);c.setUseCaches(false);
            try{if(body!=null){c.setRequestMethod("POST");c.setRequestProperty("X-FPGA-Control","1");c.setRequestProperty("Content-Type","text/plain");c.setDoOutput(true);byte[] bytes=body.getBytes(StandardCharsets.US_ASCII);c.setFixedLengthStreamingMode(bytes.length);try(OutputStream out=c.getOutputStream()){out.write(bytes);}}
                int code=c.getResponseCode();ByteArrayOutputStream bytes=new ByteArrayOutputStream();try(InputStream in=code>=400?c.getErrorStream():c.getInputStream()){byte[] buffer=new byte[512];int n;while((n=in.read(buffer))!=-1){bytes.write(buffer,0,n);if(bytes.size()>65536)throw new IOException("response size");}}return new Transport.Response(code,new String(bytes.toByteArray(),StandardCharsets.UTF_8));
            }finally{c.disconnect();}
        };
        try{
            AppController app=new AppController(null);app.connect(http,false);long now=0;for(int i=0;i<16;i++){app.tick(now);now+=25;}
            check(app.view().fmEditable,"real HTTP v2 discovery/catalog/empty bitmap");app.keys(3,100,now);for(int i=0;i<6;i++){app.tick(now);now+=25;}check(app.view().ownerMask==3,"HTTP key adoption");
            check(app.fmParameter(18,0,0,31),"HTTP parameter queued");for(int i=0;i<6;i++){app.tick(now);now+=25;}check(model.count(7,18)==1,"HTTP full u32 target");
            app.releaseNotes();for(int i=0;i<6;i++){app.tick(now);now+=25;}check(app.view().ownerMask==0,"HTTP release");
            check(app.mode(1),"KS explicit switch");for(int i=0;i<25;i++){app.tick(now);now+=25;}check(app.decay(8000),"KS decay");for(int i=0;i<6;i++){app.tick(now);now+=25;}
            check(app.parameter(7,90),"CC request");for(int i=0;i<6;i++){app.tick(now);now+=25;}
            app.mode(2);for(int i=0;i<25;i++){app.tick(now);now+=25;}check(app.fmPreset(0),"HTTP preset");for(int i=0;i<25;i++){app.tick(now);now+=25;}
            check(app.panic(),"HTTP global stop");for(int i=0;i<25;i++){app.tick(now);now+=25;}
            Set<Long> ops=new HashSet<>();List<Object> vectors=new ArrayList<>();for(long[] w:model.sent){ops.add(w[3]);List<Object> words=new ArrayList<>();StringBuilder body=new StringBuilder();for(long v:w){words.add(v);if(body.length()!=0)body.append(' ');body.append(v);}Map<String,Object> item=new LinkedHashMap<>();item.put("body",body.toString());item.put("words",words);item.put("valid",true);vectors.add(item);}
            check(ops.containsAll(Arrays.asList(2L,3L,4L,5L,6L,7L,8L)),"all seven HTTP command opcodes exercised");
            Files.write(Paths.get(args[1]),(Json.stringify(vectors)+"\n").getBytes(StandardCharsets.UTF_8));
            System.out.println("Runtime localhost HTTP: "+count+" assertions PASS, "+vectors.size()+" actual App encoder vectors (host model, no audio/device)");
        }finally{server.stop(0);}
    }
}
