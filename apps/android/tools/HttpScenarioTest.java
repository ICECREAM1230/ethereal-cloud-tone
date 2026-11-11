import cn.fpgasynth.control.core.ControlSession;
import cn.fpgasynth.control.core.DeviceState;
import cn.fpgasynth.control.core.Json;
import cn.fpgasynth.control.core.Protocol;
import cn.fpgasynth.control.core.Transport;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Map;

public final class HttpScenarioTest {
    static String origin;
    static Transport.Response http(String path,String body,boolean header)throws IOException {
        HttpURLConnection c=(HttpURLConnection)new URL(origin+path).openConnection();c.setConnectTimeout(350);c.setReadTimeout(350);c.setInstanceFollowRedirects(false);
        try{
            if(body!=null){byte[] b=body.getBytes(StandardCharsets.US_ASCII);c.setRequestMethod("POST");c.setDoOutput(true);c.setFixedLengthStreamingMode(b.length);
                c.setRequestProperty("Content-Type","text/plain");if(header)c.setRequestProperty("X-FPGA-Control","1");c.getOutputStream().write(b);c.getOutputStream().close();}
            int code=c.getResponseCode();InputStream in=code>=400?c.getErrorStream():c.getInputStream();ByteArrayOutputStream out=new ByteArrayOutputStream();
            if(in!=null)try(InputStream s=in){byte[] b=new byte[2048];int n;while((n=s.read(b))!=-1)out.write(b,0,n);}
            return new Transport.Response(code,new String(out.toByteArray(),StandardCharsets.UTF_8));
        }finally{c.disconnect();}
    }
    static void check(boolean b,String m){if(!b)throw new AssertionError(m);}
    static long now(){return System.nanoTime()/1000000;}
    static void until(ControlSession c,java.util.function.Predicate<ControlSession.View> predicate)throws Exception {
        long end=now()+4000;while(!predicate.test(c.view())&&now()<end){c.tick(now());Thread.sleep(25);}check(predicate.test(c.view()),"deadline: "+c.view().message);
    }
    public static void main(String[] args)throws Exception {
        origin=args[0];
        check(http("/api/session","",false).code==403,"missing header");
        check(http("/api/session","x",true).code==400,"nonempty session");
        ControlSession c=new ControlSession(null);c.connect((p,b)->http(p,b,true),false);until(c,v->v.playable);
        check(c.view().state.n("volume")==207,"read-only initial patch");
        long token=c.view().token;
        for(String invalid:new String[]{token+" 2 3 1 100\n",token+" 2 3 1 100 ",token+" 2 2 64 127",token+" 2 4294967299 1 100",token+" 2 3 -1 100",token+" 2 3 1 100\u0000"})
            check(http("/api/command",invalid,true).code==400,"invalid body rejected");
        check(http("/api/command",Protocol.encode(token,1,3,0,100),true).code==409,"stale shared sequence");
        check(c.keys(1,100,now()),"press");c.keys(0,100,now());
        until(c,v->v.sequence>=3&&v.ownerMask==0&&v.queueSize==0);
        check(c.view().sequence>=3,"quick gesture serialized over real HTTP");
        check(c.parameter(7,20),"explicit CC");until(c,v->v.state.n("volume")==40);
        check(c.mode(1),"KS request");until(c,v->v.playable&&v.state.applied==1);
        check(c.decay(8000),"decay request");until(c,v->v.state.decay==8000);
        c.keys(1,100,now());until(c,v->v.ownerMask==1);
        http("/__mock__/fault","{\"drop_next_note_response\":true}",false);
        c.keys(3,100,now());c.tick(now());check(c.view().localMask==0&&!c.view().playable,"ambiguous failure clears notes");
        for(int i=0;i<5;i++){c.tick(now());Thread.sleep(25);}
        DeviceState after=new DeviceState(http("/api/state",null,true).body);check(after.keys[0]==0,"safety zero converged after lost receipt");
        http("/__mock__/fault","{\"offset_ms\":12000}",false);
        check(http("/api/command",Protocol.encode(token,100,3,0,100),true).code==410,"session expires without commands");
        long[] sessions=new long[4];
        for(int i=0;i<4;i++){Map<String,Object> s=Json.object(http("/api/session","",true).body);sessions[i]=Json.number(s.get("token"));}
        check(http("/api/session","",true).code==503,"four slots only");
        System.out.println("RESULT real HTTP scenarios passed: strict/header/sequence/owner/fast-touch/patch/KS/ambiguity/expiry/capacity");
    }
}
