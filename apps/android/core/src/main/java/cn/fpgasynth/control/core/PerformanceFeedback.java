package cn.fpgasynth.control.core;

/** Control adoption is observable; physical audio is shown only after an explicit fresh frame. */
public final class PerformanceFeedback {
    private static final int MAX=(1<<25)-1;
    private static final String[] NAMES={"C","C♯","D","D♯","E","F","F♯","G","G♯","A","A♯","B"};
    private long epoch=-1,audioAt=-1;
    private boolean playable,simulation,sendOnly;
    private int local,adopted,velocity=100;
    private double leftRms,rightRms,leftPeak,rightPeak;
    public static final class Scene {
        public final int localMask,adoptedMask,pendingOn,pendingOff,localCount,adoptedCount,velocity;
        public final String localNotes,adoptedNotes;
        public final boolean playable,simulation,sendOnly,audioAvailable;
        public final double leftRms,rightRms,leftPeak,rightPeak;
        private Scene(PerformanceFeedback f,long now){
            localMask=f.playable?f.local:0;adoptedMask=f.playable?f.adopted:0;
            pendingOn=localMask&~adoptedMask;pendingOff=adoptedMask&~localMask;
            localCount=Integer.bitCount(localMask);adoptedCount=Integer.bitCount(adoptedMask);velocity=f.velocity;
            localNotes=notes(localMask);adoptedNotes=notes(adoptedMask);playable=f.playable;simulation=f.simulation;sendOnly=f.sendOnly;
            audioAvailable=f.playable&&!f.simulation&&!f.sendOnly&&f.audioAt>=0&&now>=f.audioAt&&now-f.audioAt<=300;
            leftRms=audioAvailable?f.leftRms:0;rightRms=audioAvailable?f.rightRms:0;
            leftPeak=audioAvailable?f.leftPeak:0;rightPeak=audioAvailable?f.rightPeak:0;
        }
    }
    public synchronized void update(long generation,boolean eligible,boolean demo,int localMask,int ownerMask,int strength,long now){
        update(generation,eligible,demo,localMask,ownerMask,strength,now,false);
    }
    public synchronized void update(long generation,boolean eligible,boolean demo,int localMask,int ownerMask,int strength,long now,boolean directMidi){
        if(localMask<0||ownerMask<0||(localMask&~MAX)!=0||(ownerMask&~MAX)!=0||strength<1||strength>127)throw new IllegalArgumentException("Invalid performance state");
        if(epoch!=generation||!eligible||demo)audioAt=-1;
        epoch=generation;playable=eligible;simulation=demo;sendOnly=directMidi;local=localMask;adopted=ownerMask;velocity=strength;
    }
    /** receivedAt uses the receiving client's monotonic clock, never a device wall-clock timestamp. */
    public synchronized boolean audio(long generation,long receivedAt,double lr,double rr,double lp,double rp){
        if(generation!=epoch||!playable||simulation||sendOnly||receivedAt<0||receivedAt<audioAt||!level(lr)||!level(rr)||!level(lp)||!level(rp)||lr>lp||rr>rp)return false;
        audioAt=receivedAt;leftRms=lr;rightRms=rr;leftPeak=lp;rightPeak=rp;return true;
    }
    private static boolean level(double n){return !Double.isNaN(n)&&!Double.isInfinite(n)&&n>=0&&n<=1;}
    public synchronized Scene snapshot(long now){return new Scene(this,now);}
    public static String notes(int mask){
        if(mask<0||(mask&~MAX)!=0)throw new IllegalArgumentException("Invalid key mask");
        StringBuilder s=new StringBuilder();for(int i=0;i<25;i++)if((mask&(1<<i))!=0){if(s.length()>0)s.append("  ");s.append(NAMES[i%12]).append(3+i/12);}return s.toString();
    }
}
