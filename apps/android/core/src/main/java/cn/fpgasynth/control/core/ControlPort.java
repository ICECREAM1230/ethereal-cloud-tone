package cn.fpgasynth.control.core;

import java.io.IOException;
import java.nio.charset.StandardCharsets;

/** An inactive constructor, one selected control channel, and explicit resource closure. */
public interface ControlPort extends Transport {
    enum Mode {WIFI, BLUETOOTH}
    Mode mode();
    void close();

    static void validateRequest(String path,String body)throws IOException{
        if(path==null||path.length()>256||!path.startsWith("/api/")||path.contains("..")||
                path.contains("//")||!path.matches("/[A-Za-z0-9_./?=&%+-]+"))
            throw new IOException("控制接口路径无效。");
        if(body!=null&&body.getBytes(StandardCharsets.UTF_8).length>4096)
            throw new IOException("控制请求超过4096字节。");
    }
}
