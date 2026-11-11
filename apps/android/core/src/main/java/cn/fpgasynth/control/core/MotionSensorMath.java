package cn.fpgasynth.control.core;

/** Display-coordinate gravity tilt and a resettable accelerometer fallback. */
public final class MotionSensorMath {
    private MotionSensorMath(){}
    public static double magnitude(double x,double y,double z){return Math.sqrt(x*x+y*y+z*z);}
    public static double[] angles(double x,double y,double z,int rotation){
        if(!Double.isFinite(x)||!Double.isFinite(y)||!Double.isFinite(z)||magnitude(x,y,z)<.01||rotation<0||rotation>3)throw new IllegalArgumentException("gravity");
        double sx=rotation==0?x:rotation==1?y:rotation==2?-x:-y;
        double sy=rotation==0?y:rotation==1?-x:rotation==2?-y:x;
        return new double[]{Math.toDegrees(Math.atan2(sx,Math.sqrt(sy*sy+z*z))),Math.toDegrees(Math.atan2(-sy,Math.sqrt(sx*sx+z*z)))};
    }
    public static final class GravityFilter {
        private long at=-1;private double gx,gy,gz;
        public void reset(){at=-1;}
        public double[] sample(double x,double y,double z,long time){
            if(!Double.isFinite(x)||!Double.isFinite(y)||!Double.isFinite(z))throw new IllegalArgumentException("invalid acceleration");
            if(time<0||time<=at)return null;
            if(at<0||time-at>250){gx=x;gy=y;gz=z;}else{double a=1-Math.exp(-(time-at)/180.0);gx+=a*(x-gx);gy+=a*(y-gy);gz+=a*(z-gz);}
            at=time;return new double[]{gx,gy,gz,magnitude(x-gx,y-gy,z-gz)};
        }
    }
}
