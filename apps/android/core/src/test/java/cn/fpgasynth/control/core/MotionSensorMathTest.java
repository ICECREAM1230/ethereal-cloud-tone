package cn.fpgasynth.control.core;
public final class MotionSensorMathTest {
    private static int count;
    private static void near(double a,double b){count++;if(Math.abs(a-b)>.001)throw new AssertionError(a+" != "+b);}
    public static void main(String[] args){
        for(int r=0;r<4;r++){double[] a=MotionSensorMath.angles(9.81,0,0,r);near(a[0],new double[]{90,0,-90,0}[r]);near(a[1],new double[]{0,90,0,-90}[r]);}
        double[] flat=MotionSensorMath.angles(0,0,9.81,0);near(flat[0],0);near(flat[1],0);
        near(MotionSensorMath.magnitude(3,4,0),5);
        MotionSensorMath.GravityFilter f=new MotionSensorMath.GravityFilter();double[] a=f.sample(0,0,9.81,1000);near(a[3],0);a=f.sample(0,0,9.81,1020);near(a[3],0);
        a=f.sample(8,0,9.81,1040);count++;if(a[3]<=4)throw new AssertionError("movement separated from gravity");
        count++;if(f.sample(0,0,9.81,1039)!=null)throw new AssertionError("ignore out of order");
        a=f.sample(0,0,9.81,2000);near(a[3],0);
        for(double invalid:new double[]{Double.NaN,Double.POSITIVE_INFINITY}){count++;boolean rejected=false;try{f.sample(invalid,0,9.81,2020);}catch(IllegalArgumentException e){rejected=true;}if(!rejected)throw new AssertionError("invalid acceleration must propagate a fault, distinct from an old sample");}
        f.reset();near(f.sample(3,4,8,3000)[3],0);System.out.println("Motion sensor math: "+count+" assertions PASS");
    }
}
