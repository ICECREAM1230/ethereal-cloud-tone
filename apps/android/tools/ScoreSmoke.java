package cn.fpgasynth.control.tests;

import android.app.Activity;
import android.app.Dialog;
import android.app.Instrumentation;
import android.content.ContentValues;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.pdf.PdfDocument;
import android.net.Uri;
import android.os.Bundle;
import android.os.SystemClock;
import android.provider.MediaStore;
import android.view.KeyEvent;
import android.widget.TextView;
import java.io.*;
import java.lang.reflect.*;
import java.net.*;
import java.util.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.atomic.AtomicReference;

/** Real Android files, document decoder, content resolver and UI. HTTPS responses are fixtures. */
public final class ScoreSmoke extends Instrumentation {
    Activity activity;Bundle args;int assertions;Object store,panel;Class<?> entryType;byte[] png,pdf;Uri media;
    interface Action{void run()throws Exception;}interface Condition{boolean get()throws Exception;}
    void check(boolean value,String why){assertions++;if(!value)throw new AssertionError(why);}
    Object field(Object target,String name)throws Exception{Field f=target.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(target);}
    Object invoke(Object target,String name,Class<?>[] types,Object... args)throws Exception{Method m=target.getClass().getDeclaredMethod(name,types);m.setAccessible(true);try{return m.invoke(target,args);}catch(InvocationTargetException e){Throwable t=e.getCause();if(t instanceof Exception)throw (Exception)t;throw e;}}
    Object construct(Class<?> type,Class<?>[] types,Object... args)throws Exception{Constructor<?> c=type.getDeclaredConstructor(types);c.setAccessible(true);return c.newInstance(args);}
    void ui(Action a){AtomicReference<Throwable> e=new AtomicReference<>();runOnMainSync(()->{try{a.run();}catch(Throwable x){e.set(x);}});if(e.get()!=null)throw new RuntimeException(e.get());}
    void waitFor(Condition c,String why)throws Exception{long end=SystemClock.elapsedRealtime()+8000;while(SystemClock.elapsedRealtime()<end){if(c.get())return;SystemClock.sleep(20);}throw new AssertionError(why);}
    String activePackage(){android.view.accessibility.AccessibilityNodeInfo node=getUiAutomation().getRootInActiveWindow();if(node==null)return "";String pkg=String.valueOf(node.getPackageName());node.recycle();return pkg;}
    List<?> list()throws Exception{return (List<?>)invoke(store,"list",new Class[]{});}
    Object save(byte[] data,String name)throws Exception{return invoke(store,"importStream",new Class[]{InputStream.class,String.class},new ByteArrayInputStream(data),name);}
    void rejected(Action a,String why)throws Exception{try{a.run();throw new AssertionError(why);}catch(IOException expected){assertions++;}}
    void shot(String name)throws Exception{Bitmap b=getUiAutomation().takeScreenshot();try(FileOutputStream out=new FileOutputStream(new File(getTargetContext().getExternalFilesDir(null),"score-"+args.getString("profile","tablet")+"-"+name+".png"))){b.compress(Bitmap.CompressFormat.PNG,100,out);}b.recycle();}
    byte[] image()throws Exception{Bitmap b=Bitmap.createBitmap(900,1200,Bitmap.Config.ARGB_8888);b.eraseColor(Color.WHITE);Canvas c=new Canvas(b);Paint p=new Paint();p.setColor(Color.BLACK);p.setTextSize(42);c.drawText("Practice fixture - 1=C 4/4",70,100,p);for(int i=0;i<7;i++)c.drawText("1  2  3  4  |  5  6  7  1",70,220+i*120,p);ByteArrayOutputStream bytes=new ByteArrayOutputStream();b.compress(Bitmap.CompressFormat.PNG,100,bytes);b.recycle();return bytes.toByteArray();}
    byte[] document()throws Exception{PdfDocument d=new PdfDocument();for(int i=0;i<2;i++){PdfDocument.Page page=d.startPage(new PdfDocument.PageInfo.Builder(600,800,i+1).create());Paint p=new Paint();p.setTextSize(24);page.getCanvas().drawText("Original test score / page "+(i+1),40,80,p);d.finishPage(page);}ByteArrayOutputStream out=new ByteArrayOutputStream();d.writeTo(out);d.close();return out.toByteArray();}
    void read(Object entry,boolean document)throws Exception{
        AtomicReference<Dialog> dialog=new AtomicReference<>();
        ui(()->{Object r=construct(Class.forName("cn.fpgasynth.control.ScoreReader"),new Class[]{Activity.class,entryType,ExecutorService.class},activity,entry,field(panel,"worker"));dialog.set((Dialog)r);dialog.get().show();});
        Object reader=dialog.get();waitFor(()->field(field(reader,"pageView"),"bitmap")!=null,"native decoder supplies bitmap");check((Integer)field(reader,"pageCount")== (document?2:1),"correct page count");
        Object canvas=field(reader,"pageView");float before=(Float)field(canvas,"scale");ui(()->invoke(canvas,"zoom",new Class[]{float.class},1.25f));check((Float)field(canvas,"scale")>before,"zoom really changes scale");
        if(document){ui(()->invoke(reader,"load",new Class[]{int.class},1));waitFor(()->!(Boolean)field(reader,"busy"),"second PDF page loads");check((Integer)field(reader,"page")==1,"PDF page navigation");}
        SystemClock.sleep(250);shot(document?"pdf":"image");ui(()->dialog.get().dismiss());waitFor(()->field(canvas,"bitmap")==null,"dismiss releases displayed bitmap");check(true,"dismiss releases displayed bitmap");
    }
    static final class FixtureConnection extends HttpURLConnection {
        static byte[] data;
        FixtureConnection(URL url){super(url);}public void disconnect(){}public boolean usingProxy(){return false;}public void connect(){}
        @Override public int getResponseCode(){return url.getPath().equals("/redirect")||url.getPath().equals("/downgrade")?302:200;}
        @Override public String getHeaderField(String name){if(!name.equals("Location"))return null;return url.getPath().equals("/downgrade")?"http://fixture.invalid/score":"/score";}
        @Override public long getContentLengthLong(){return url.getPath().equals("/oversize")?21L*1024*1024:-1;}
        @Override public InputStream getInputStream(){return new ByteArrayInputStream(url.getPath().equals("/html")?"<html>login</html>".getBytes():data);}
    }
    @Override public void onCreate(Bundle a){args=a;start();}
    @Override public void onStart(){Bundle result=new Bundle();try{
        Intent launch=getTargetContext().getPackageManager().getLaunchIntentForPackage("cn.fpgasynth.control");launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK);activity=startActivitySync(launch);panel=field(activity,"scoreLibrary");
        waitFor(()->!(Boolean)field(panel,"busy"),"initial library ready");ui(()->invoke(activity,"select",new Class[]{int.class},3));
        store=construct(Class.forName("cn.fpgasynth.control.ScoreFiles"),new Class[]{android.content.Context.class},getTargetContext());entryType=Class.forName("cn.fpgasynth.control.ScoreFiles$Entry");png=image();pdf=document();
        int original=list().size();Object image=save(png,"../../fixture.png"),document=save(pdf,"fixture.pdf");check(list().size()==original+2,"two complete files saved");check(field(image,"name").equals("fixture.png"),"safe display name");
        File directory=new File(getTargetContext().getFilesDir(),"scores");String orphanId=UUID.randomUUID().toString();File orphan=new File(directory,orphanId+".png");try(FileOutputStream out=new FileOutputStream(orphan)){out.write(png);}File interrupted=new File(directory,"incoming-987654321.part");try(FileOutputStream out=new FileOutputStream(interrupted)){out.write(1);}
        store=construct(Class.forName("cn.fpgasynth.control.ScoreFiles"),new Class[]{android.content.Context.class},getTargetContext());boolean recovered=false;for(Object entry:list())if(((File)field(entry,"file")).equals(orphan))recovered=true;check(recovered,"interrupted commit without index remains visible and removable");check(!interrupted.exists(),"stale incoming temporary file cleaned on next open");invoke(store,"remove",new Class[]{entryType},list().stream().filter(e->{try{return ((File)field(e,"file")).equals(orphan);}catch(Exception x){throw new RuntimeException(x);}}).findFirst().get());
        ByteArrayOutputStream exported=new ByteArrayOutputStream();invoke(store,"export",new Class[]{entryType,OutputStream.class},image,exported);check(Arrays.equals(png,exported.toByteArray()),"export exact original bytes");
        rejected(()->save("<html>login</html>".getBytes(),"fake.pdf"),"HTML rejected");rejected(()->save(Arrays.copyOf(png,18),"broken.png"),"magic without decodable image rejected");check(list().size()==original+2,"bad imports preserve all saved entries");
        read(image,false);read(document,true);
        FixtureConnection.data=png;URL.setURLStreamHandlerFactory(protocol->protocol.equals("https")?new URLStreamHandler(){protected URLConnection openConnection(URL url){return new FixtureConnection(url);}}:null);
        Object download=invoke(store,"download",new Class[]{String.class},"https://fixture.invalid/redirect");check(((File)field(download,"file")).length()==png.length,"actual downloader saves successful redirect response");
        rejected(()->invoke(store,"download",new Class[]{String.class},"https://fixture.invalid/html"),"HTML URL not saved as score");rejected(()->invoke(store,"download",new Class[]{String.class},"https://fixture.invalid/downgrade"),"HTTPS downgrade rejected");rejected(()->invoke(store,"download",new Class[]{String.class},"https://fixture.invalid/oversize"),"large declared response rejected");
        ContentValues values=new ContentValues();values.put(MediaStore.Images.Media.DISPLAY_NAME,"saf-owned-fixture.png");values.put(MediaStore.Images.Media.MIME_TYPE,"image/png");media=getTargetContext().getContentResolver().insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI,values);check(media!=null,"owned content provider fixture inserted");try(OutputStream out=getTargetContext().getContentResolver().openOutputStream(media)){out.write(png);}
        int before=list().size();ui(()->invoke(activity,"onActivityResult",new Class[]{int.class,int.class,Intent.class},6201,Activity.RESULT_OK,new Intent().setData(media)));waitFor(()->!(Boolean)field(panel,"busy"),"content URI import finishes");check(list().size()==before+1,"activity routes real content resolver import");
        ui(()->invoke(panel,"onActivityResult",new Class[]{int.class,int.class,Intent.class},6201,Activity.RESULT_CANCELED,null));check(((TextView)field(panel,"status")).getText().toString().contains("取消"),"SAF cancellation visible");
        // Launch the actual Android picker, then cancel. No physical phone is targeted.
        ui(()->invoke(panel,"chooseImport",new Class[]{}));waitFor(()->activePackage().contains("documentsui"),"actual Android document picker becomes active");sendKeyDownUpSync(KeyEvent.KEYCODE_BACK);waitFor(()->activePackage().equals("cn.fpgasynth.control")&&activity.hasWindowFocus(),"return from actual document picker");SystemClock.sleep(250);
        ui(()->invoke(panel,"refresh",new Class[]{}));waitFor(()->!(Boolean)field(panel,"busy"),"refresh after picker");shot("library");
        invoke(store,"remove",new Class[]{entryType},download);check(!((File)field(download,"file")).exists(),"delete removes chosen owned file");
        File[] remnants=new File(getTargetContext().getFilesDir(),"scores").listFiles((dir,name)->name.endsWith(".part"));check(remnants!=null&&remnants.length==0,"failed operations leave no partial files");
        result.putString("result","PASS");result.putInt("assertions",assertions);result.putString("network","fixture HttpURLConnection; real Android decoder/store/reader/content URI/SAF");
    }catch(Throwable e){StringWriter out=new StringWriter();e.printStackTrace(new PrintWriter(out));result.putString("result","FAIL");result.putString("error",out.toString());}finally{if(media!=null)getTargetContext().getContentResolver().delete(media,null,null);if(activity!=null)ui(()->activity.finish());}finish(0,result);}
}
