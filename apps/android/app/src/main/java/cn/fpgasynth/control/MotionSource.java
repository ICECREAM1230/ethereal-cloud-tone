package cn.fpgasynth.control;
/** Device capability and lifecycle seam; the production implementation is MotionSensors. */
public interface MotionSource {
    boolean available();boolean running();boolean start(int displayRotation);void stop();String source();
}
