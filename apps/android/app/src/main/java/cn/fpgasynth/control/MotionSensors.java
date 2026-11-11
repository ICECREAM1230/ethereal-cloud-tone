package cn.fpgasynth.control;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Handler;
import android.os.SystemClock;
import cn.fpgasynth.control.core.MotionSensorMath;

/** Reads only while the visible motion instrument owns input; no background service. */
public final class MotionSensors implements MotionSource,SensorEventListener {
    public interface Listener {void sample(double roll,double pitch,double force,long sampledAt,long now);}
    private final SensorManager manager;private final Sensor orientation,forceSensor;private final Handler handler;private final Listener listener;
    private final MotionSensorMath.GravityFilter gravity=new MotionSensorMath.GravityFilter();
    private boolean active;private int rotation;private long orientationAt=-1,forceAt=-1,emittedAt=-1;private double roll,pitch,force;
    MotionSensors(Context context,Handler handler,Listener listener){
        this.handler=handler;this.listener=listener;manager=context.getSystemService(SensorManager.class);
        Sensor rv=get(Sensor.TYPE_GAME_ROTATION_VECTOR);if(rv==null)rv=get(Sensor.TYPE_ROTATION_VECTOR);
        Sensor acc=get(Sensor.TYPE_ACCELEROMETER),linear=get(Sensor.TYPE_LINEAR_ACCELERATION);
        orientation=rv!=null?rv:acc;forceSensor=linear!=null?linear:acc;
    }
    private Sensor get(int type){return manager==null?null:manager.getDefaultSensor(type);}
    public boolean available(){return manager!=null&&orientation!=null&&forceSensor!=null;}
    public boolean running(){return active;}
    public String source(){return !available()?"传感器不可用":orientation.getType()==Sensor.TYPE_ACCELEROMETER?"加速度估计":"姿态传感器";}
    public boolean start(int displayRotation){
        if(active&&rotation==displayRotation)return true;stop();if(!available())return false;rotation=displayRotation;
        try{
            boolean ok=manager.registerListener(this,orientation,20000,0,handler);
            if(forceSensor!=orientation)ok=manager.registerListener(this,forceSensor,20000,0,handler)&&ok;
            if(!ok){stop();return false;}active=true;return true;
        }catch(RuntimeException e){stop();return false;}
    }
    public void stop(){active=false;if(manager!=null)manager.unregisterListener(this);gravity.reset();orientationAt=forceAt=emittedAt=-1;}
    @Override public void onAccuracyChanged(Sensor sensor,int accuracy){}
    @Override public void onSensorChanged(SensorEvent event){
        if(!active||event.values.length<3)return;
        long now=SystemClock.elapsedRealtime(),at=event.timestamp/1000000L;
        if(at>now){listener.sample(Double.NaN,0,0,at,now);return;}
        if(now-at>250)return;
        try{
            int type=event.sensor.getType();
            if(type==Sensor.TYPE_GAME_ROTATION_VECTOR||type==Sensor.TYPE_ROTATION_VECTOR){
                float[] matrix=new float[9];SensorManager.getRotationMatrixFromVector(matrix,event.values);
                double[] angles=MotionSensorMath.angles(matrix[6],matrix[7],matrix[8],rotation);
                if(at>orientationAt){roll=angles[0];pitch=angles[1];orientationAt=at;}
            }else if(type==Sensor.TYPE_ACCELEROMETER){
                double[] g=gravity.sample(event.values[0],event.values[1],event.values[2],at);if(g==null)return;
                if(orientation.getType()==Sensor.TYPE_ACCELEROMETER){double[] angles=MotionSensorMath.angles(g[0],g[1],g[2],rotation);roll=angles[0];pitch=angles[1];orientationAt=at;}
                if(forceSensor.getType()==Sensor.TYPE_ACCELEROMETER){force=g[3];forceAt=at;}
            }else if(type==Sensor.TYPE_LINEAR_ACCELERATION&&at>forceAt){force=MotionSensorMath.magnitude(event.values[0],event.values[1],event.values[2]);forceAt=at;}
            long compositeAt=Math.min(orientationAt,forceAt);
            if(compositeAt>emittedAt&&orientationAt>=0&&forceAt>=0&&now-orientationAt<=250&&now-forceAt<=250){emittedAt=compositeAt;listener.sample(roll,pitch,force,compositeAt,now);}
        }catch(IllegalArgumentException e){listener.sample(Double.NaN,0,0,at,now);}
    }
}
