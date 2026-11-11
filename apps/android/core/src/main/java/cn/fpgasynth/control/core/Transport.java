package cn.fpgasynth.control.core;

import java.io.IOException;

public interface Transport {
    Response request(String path, String body) throws IOException;
    final class Response {
        public final int code;
        public final String body;
        public Response(int code,String body){this.code=code;this.body=body;}
    }
}
