package cn.fpgasynth.control.core;

import java.io.IOException;
import java.io.InputStream;
import java.io.InterruptedIOException;
import java.io.OutputStream;
import java.net.URI;
import java.net.URL;

/** Bounds and file signatures only. Android must additionally decode the document. */
public final class ScoreFilePolicy {
    public static final long MAX_FILE_BYTES=20L*1024*1024,MAX_LIBRARY_BYTES=100L*1024*1024;
    public static final int MAX_FILES=50;
    public static URL https(String raw)throws IOException{
        if(raw==null||raw.length()>4096)throw new IOException("请输入 HTTPS 图片或 PDF 文件链接。");
        String value=raw.trim();
        try{
            URI uri=new URI(value);int port=uri.getPort();
            if(!"https".equalsIgnoreCase(uri.getScheme())||uri.getHost()==null||uri.getHost().isEmpty()||uri.getUserInfo()!=null||port>65535||port==0)
                throw new IOException("仅支持不含账号密码的 HTTPS 文件链接。");
            return uri.toURL();
        }catch(java.net.URISyntaxException|IllegalArgumentException e){throw new IOException("文件链接无效。",e);}
    }
    public static String kind(byte[] h)throws IOException{
        if(h==null)throw new IOException("文件为空。");
        if(h.length>=8&&(h[0]&255)==137&&h[1]==80&&h[2]==78&&h[3]==71&&h[4]==13&&h[5]==10&&h[6]==26&&h[7]==10)return "png";
        if(h.length>=3&&(h[0]&255)==255&&(h[1]&255)==216&&(h[2]&255)==255)return "jpg";
        if(h.length>=12&&h[0]==82&&h[1]==73&&h[2]==70&&h[3]==70&&h[8]==87&&h[9]==69&&h[10]==66&&h[11]==80)return "webp";
        if(h.length>=8&&h[0]==37&&h[1]==80&&h[2]==68&&h[3]==70&&h[4]==45&&(h[5]==49||h[5]==50)&&h[6]==46&&h[7]>=48&&h[7]<=57)return "pdf";
        throw new IOException("不是支持的 PNG、JPEG、WebP 图片或 PDF 文件；网页请在原站保存后导入。");
    }
    public static String name(String raw){
        if(raw==null)return "曲谱";
        String value=raw.replace('\\','/');value=value.substring(value.lastIndexOf('/')+1);StringBuilder clean=new StringBuilder();
        for(int i=0;i<value.length()&&clean.length()<120;i++){
            char c=value.charAt(i);int type=Character.getType(c);
            if(!Character.isISOControl(c)&&type!=Character.FORMAT&&c!='/'&&c!='\\')clean.append(c);
        }
        String result=clean.toString().trim();return result.isEmpty()||result.equals(".")||result.equals("..")?"曲谱":result;
    }
    public static long copy(InputStream in,OutputStream out,long limit)throws IOException{
        if(limit<0)throw new IOException("无效文件大小限制。");
        byte[] block=new byte[16384];long count=0;
        while(true){
            if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("已取消。");
            int n=in.read(block);if(n<0)return count;if(n==0)continue;
            if(n>limit-count)throw new IOException("文件超过允许大小。");
            out.write(block,0,n);count+=n;
        }
    }
    public static void capacity(int count,long bytes,long incoming)throws IOException{
        if(count<0||bytes<0||incoming<=0||incoming>MAX_FILE_BYTES)throw new IOException("单份曲谱应为 1 字节至 20 MB。");
        if(count>=MAX_FILES||bytes>MAX_LIBRARY_BYTES-incoming)throw new IOException("谱夹最多 50 份、100 MB，请先删除不需要的曲谱。");
    }
    private ScoreFilePolicy(){}
}
