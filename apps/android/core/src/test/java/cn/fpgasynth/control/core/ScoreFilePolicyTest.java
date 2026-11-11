package cn.fpgasynth.control.core;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

public final class ScoreFilePolicyTest {
    private static int checks;
    private interface Checked {void run() throws Exception;}
    private static void check(boolean value,String message){checks++;if(!value)throw new AssertionError(message);}
    private static void bad(Checked action){checks++;try{action.run();throw new AssertionError("expected IOException");}catch(IOException expected){}catch(Exception e){throw new AssertionError(e);}}
    public static void main(String[] args)throws Exception{
        check(ScoreFilePolicy.https(" https://example.org/a.png?q=1 ").getHost().equals("example.org"),"https URL");
        for(String url:new String[]{"http://example.org/a","file:///tmp/a","javascript:alert(1)","https://u:p@example.org/a","https:///missing","https://example.org:99999/a","https://example.org/a\nBAD"})bad(()->ScoreFilePolicy.https(url));
        check("png".equals(ScoreFilePolicy.kind(new byte[]{(byte)137,80,78,71,13,10,26,10})),"png magic");
        check("jpg".equals(ScoreFilePolicy.kind(new byte[]{(byte)255,(byte)216,(byte)255,0})),"jpeg magic");
        check("webp".equals(ScoreFilePolicy.kind(new byte[]{82,73,70,70,0,0,0,0,87,69,66,80})),"webp magic");
        check("pdf".equals(ScoreFilePolicy.kind("%PDF-1.7\n".getBytes(StandardCharsets.US_ASCII))),"pdf magic");
        for(byte[] bytes:new byte[][]{new byte[0],"<html>login</html>".getBytes(StandardCharsets.US_ASCII),new byte[]{(byte)137,80,78},"%PDF-INVALID".getBytes(StandardCharsets.US_ASCII)})bad(()->ScoreFilePolicy.kind(bytes));
        check(ScoreFilePolicy.name("../../evil\nscore.pdf").equals("evilscore.pdf"),"safe name");
        check(ScoreFilePolicy.name("C:\\folder\\score.png").equals("score.png"),"windows basename");
        check(ScoreFilePolicy.name("\u202eabc.pdf").equals("abc.pdf"),"bidi stripped");
        check(ScoreFilePolicy.name(null).equals("曲谱"),"default name");
        check(ScoreFilePolicy.name(new String(new char[300]).replace('\0','a')).length()<=120,"name bounded");
        ByteArrayOutputStream out=new ByteArrayOutputStream();
        check(ScoreFilePolicy.copy(new ByteArrayInputStream(new byte[]{1,2,3}),out,3)==3&&out.size()==3,"exact limit");
        bad(()->ScoreFilePolicy.copy(new ByteArrayInputStream(new byte[4]),new ByteArrayOutputStream(),3));
        bad(()->ScoreFilePolicy.copy(new ByteArrayInputStream(new byte[0]),new ByteArrayOutputStream(),-1));
        Thread.currentThread().interrupt();bad(()->ScoreFilePolicy.copy(new ByteArrayInputStream(new byte[1]),new ByteArrayOutputStream(),3));Thread.interrupted();
        ScoreFilePolicy.capacity(49,ScoreFilePolicy.MAX_LIBRARY_BYTES-1,1);checks++;
        bad(()->ScoreFilePolicy.capacity(50,0,1));bad(()->ScoreFilePolicy.capacity(0,ScoreFilePolicy.MAX_LIBRARY_BYTES,1));
        bad(()->ScoreFilePolicy.capacity(0,0,ScoreFilePolicy.MAX_FILE_BYTES+1));bad(()->ScoreFilePolicy.capacity(-1,0,1));bad(()->ScoreFilePolicy.capacity(0,-1,1));bad(()->ScoreFilePolicy.capacity(0,0,0));
        System.out.println("ScoreFilePolicyTest PASS "+checks+" checks");
    }
}
