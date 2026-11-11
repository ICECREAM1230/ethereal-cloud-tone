package cn.fpgasynth.control;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.pdf.PdfRenderer;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
import cn.fpgasynth.control.core.ScoreFilePolicy;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InterruptedIOException;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;

/** Internal score copies. All methods are worker-only; no network or MIDI state is shared. */
final class ScoreFiles {
    static final class Entry {
        final String id,name,kind;final long bytes;final File file;
        Entry(String id,String name,String kind,long bytes,File file){this.id=id;this.name=name;this.kind=kind;this.bytes=bytes;this.file=file;}
        String mime(){return kind.equals("pdf")?"application/pdf":kind.equals("jpg")?"image/jpeg":"image/"+kind;}
    }
    private final File directory;
    private static final Object COMMIT=new Object();
    private static final Set<String> ACTIVE=new HashSet<>();
    ScoreFiles(Context context)throws IOException{
        directory=new File(context.getFilesDir(),"scores");
        if(!directory.isDirectory()&&!directory.mkdirs())throw new IOException("无法创建离线谱夹。");
        synchronized(COMMIT){File[] files=directory.listFiles();if(files==null)throw new IOException("无法读取谱夹。");for(File f:files){String name=f.getName();if((name.matches("incoming-[-0-9]+\\.part")||name.matches("[0-9a-f-]{36}\\.json\\.part"))&&!ACTIVE.contains(f.getAbsolutePath()))f.delete();}}
    }
    List<Entry> list()throws IOException{
        File[] files=directory.listFiles();if(files==null)throw new IOException("无法读取谱夹。");
        List<Entry> result=new ArrayList<>();
        synchronized(COMMIT){for(File document:files)if(document.getName().matches("[0-9a-f-]{36}\\.(png|jpg|webp|pdf)")&&document.isFile()&&document.length()>0){
            String filename=document.getName(),id=filename.substring(0,36),kind=filename.substring(37),name="恢复的曲谱."+kind;
            File meta=new File(directory,id+".json");
            try(FileInputStream in=new FileInputStream(meta)){
                ByteArrayOutputStream bytes=new ByteArrayOutputStream();ScoreFilePolicy.copy(in,bytes,2048);
                JSONObject value=new JSONObject(new String(bytes.toByteArray(),StandardCharsets.UTF_8));
                if(kind.equals(value.getString("kind")))name=ScoreFilePolicy.name(value.getString("name"));
            }catch(Exception ignored){/* Interrupted/malformed metadata must not hide owned files or their capacity. */}
            result.add(new Entry(id,name,kind,document.length(),document));
        }}
        Collections.sort(result,Comparator.comparingLong((Entry e)->e.file.lastModified()).reversed());return result;
    }
    Entry importStream(InputStream source,String name)throws IOException{
        if(source==null)throw new IOException("文件无法打开。");
        File temporary;synchronized(COMMIT){temporary=File.createTempFile("incoming-",".part",directory);ACTIVE.add(temporary.getAbsolutePath());}
        try{
            try(FileOutputStream out=new FileOutputStream(temporary)){ScoreFilePolicy.copy(source,out,ScoreFilePolicy.MAX_FILE_BYTES);out.getFD().sync();}
            String kind=validate(temporary);
            if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("已取消。");
            synchronized(COMMIT){
                int count=0;long used=0;File[] all=directory.listFiles();if(all==null)throw new IOException("无法读取谱夹。");
                for(File f:all)if(f.isFile()&&f.getName().matches("[0-9a-f-]{36}\\.(png|jpg|webp|pdf)")){count++;used+=f.length();}
                ScoreFilePolicy.capacity(count,used,temporary.length());
                String id=UUID.randomUUID().toString(),display=ScoreFilePolicy.name(name);File target=new File(directory,id+"."+kind),meta=new File(directory,id+".json"),pending=new File(directory,id+".json.part");
                try{
                    JSONObject object=new JSONObject();object.put("name",display);object.put("kind",kind);
                    try(FileOutputStream out=new FileOutputStream(pending)){out.write(object.toString().getBytes(StandardCharsets.UTF_8));out.getFD().sync();}
                    if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("已取消。");
                    if(!temporary.renameTo(target))throw new IOException("保存曲谱失败。");
                    if(!pending.renameTo(meta)){target.delete();throw new IOException("保存曲谱索引失败。");}
                    return new Entry(id,display,kind,target.length(),target);
                }catch(org.json.JSONException e){throw new IOException("曲谱信息无效。",e);}finally{pending.delete();}
            }
        }finally{synchronized(COMMIT){ACTIVE.remove(temporary.getAbsolutePath());temporary.delete();}}
    }
    Entry download(String raw)throws IOException{
        URL url=ScoreFilePolicy.https(raw);long deadline=SystemClock.elapsedRealtime()+30000;
        for(int redirects=0;redirects<=4;redirects++){
            if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("已取消。");
            long remaining=deadline-SystemClock.elapsedRealtime();if(remaining<=0)throw new IOException("下载超时，请稍后重试。");
            HttpURLConnection connection=(HttpURLConnection)url.openConnection();
            try{
                connection.setInstanceFollowRedirects(false);connection.setConnectTimeout((int)Math.min(6000,remaining));connection.setReadTimeout((int)Math.min(4000,remaining));connection.setUseCaches(false);
                connection.setRequestProperty("Accept","image/png,image/jpeg,image/webp,application/pdf");connection.setRequestProperty("User-Agent","FPGA-Synth-Score/1.0");
                int status=connection.getResponseCode();
                if(SystemClock.elapsedRealtime()>=deadline)throw new IOException("下载超时，请稍后重试。");
                if(status>=300&&status<=399){
                    String location=connection.getHeaderField("Location");if(location==null||redirects==4)throw new IOException("文件链接重定向过多。");
                    url=ScoreFilePolicy.https(new URL(url,location).toExternalForm());continue;
                }
                if(status!=200)throw new IOException("下载失败，HTTP "+status+"；可在原站保存后导入。");
                if(connection.getContentLengthLong()>ScoreFilePolicy.MAX_FILE_BYTES)throw new IOException("曲谱超过 20 MB。");
                final long end=deadline;
                try(InputStream source=new FilterInputStream(connection.getInputStream()){
                    @Override public int read(byte[] bytes,int offset,int length)throws IOException{
                        long left=end-SystemClock.elapsedRealtime();if(left<=0)throw new IOException("下载超时，请稍后重试。");
                        connection.setReadTimeout((int)Math.min(4000,left));int n=super.read(bytes,offset,length);
                        if(SystemClock.elapsedRealtime()>=end)throw new IOException("下载超时，请稍后重试。");return n;
                    }
                }){return importStream(source,url.getPath());}
            }finally{connection.disconnect();}
        }
        throw new IOException("无法下载文件。");
    }
    void export(Entry entry,OutputStream target)throws IOException{
        if(target==null)throw new IOException("保存位置无法打开。");
        try(FileInputStream source=new FileInputStream(owned(entry))){ScoreFilePolicy.copy(source,target,ScoreFilePolicy.MAX_FILE_BYTES);target.flush();}
    }
    void remove(Entry entry)throws IOException{
        synchronized(COMMIT){File document=owned(entry);if(document.exists()&&!document.delete())throw new IOException("删除曲谱失败。");File meta=new File(directory,entry.id+".json");if(meta.exists()&&!meta.delete())throw new IOException("删除曲谱索引失败。");}
    }
    private File owned(Entry entry)throws IOException{
        if(entry==null||!entry.id.matches("[0-9a-f-]{36}")||!entry.kind.matches("png|jpg|webp|pdf"))throw new IOException("无效曲谱。");
        File f=new File(directory,entry.id+"."+entry.kind);if(!f.getCanonicalFile().getParentFile().equals(directory.getCanonicalFile()))throw new IOException("无效文件位置。");return f;
    }
    private String validate(File file)throws IOException{
        byte[] header=new byte[16];int count;try(FileInputStream in=new FileInputStream(file)){count=in.read(header);}
        if(count<0)throw new IOException("文件为空。");String kind=ScoreFilePolicy.kind(java.util.Arrays.copyOf(header,count));
        if(kind.equals("pdf")){
            try(ParcelFileDescriptor descriptor=ParcelFileDescriptor.open(file,ParcelFileDescriptor.MODE_READ_ONLY);PdfRenderer renderer=new PdfRenderer(descriptor)){
                if(renderer.getPageCount()<1||renderer.getPageCount()>500)throw new IOException("PDF 页数应为 1–500 页。");
                try(PdfRenderer.Page page=renderer.openPage(0)){
                    if(page.getWidth()<=0||page.getHeight()<=0)throw new IOException("PDF 页面无效。");
                    Bitmap preview=Bitmap.createBitmap(64,64,Bitmap.Config.ARGB_8888);
                    try{page.render(preview,null,null,PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY);}finally{preview.recycle();}
                }
            }catch(SecurityException|IllegalArgumentException e){throw new IOException("PDF 无法读取，可能已加密或损坏。",e);}
        }else{
            BitmapFactory.Options bounds=new BitmapFactory.Options();bounds.inJustDecodeBounds=true;BitmapFactory.decodeFile(file.getPath(),bounds);
            if(bounds.outWidth<=0||bounds.outHeight<=0||(long)bounds.outWidth*bounds.outHeight>40000000L||bounds.outWidth>20000||bounds.outHeight>20000)throw new IOException("图片损坏或超过 4000 万像素。");
            BitmapFactory.Options sample=new BitmapFactory.Options();sample.inSampleSize=1;while((long)(bounds.outWidth/sample.inSampleSize)*(bounds.outHeight/sample.inSampleSize)>1000000L)sample.inSampleSize*=2;
            Bitmap bitmap=BitmapFactory.decodeFile(file.getPath(),sample);if(bitmap==null)throw new IOException("图片无法解码。");bitmap.recycle();
        }
        return kind;
    }
}
