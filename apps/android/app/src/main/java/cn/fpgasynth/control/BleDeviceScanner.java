package cn.fpgasynth.control;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.*;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import cn.fpgasynth.control.core.BleControlProfile;
import cn.fpgasynth.control.core.BleMidiPort;
import java.io.IOException;
import java.util.Arrays;

/** Known candidates plus a bounded private-service scan; selection never opens a GATT session. */
final class BleDeviceScanner {
    interface Listener {void found(String name,String address,String source);void ended(String message);}
    private final Handler ui=new Handler(Looper.getMainLooper());
    private BluetoothLeScanner scanner;private ScanCallback callback;private long generation;
    private void known(BluetoothDevice device,String source,Listener listener){
        if(device.getType()==BluetoothDevice.DEVICE_TYPE_CLASSIC)return;
        String name=device.getName();listener.found(name==null?"蓝牙设备":name,device.getAddress(),source);
    }
    void start(BluetoothManager manager,BluetoothAdapter adapter,Listener listener)throws IOException{
        stop();final long epoch=++generation;
        try{
            for(BluetoothDevice device:adapter.getBondedDevices())known(device,"已配对 · 待验证",listener);
            if(manager!=null)for(BluetoothDevice device:manager.getConnectedDevices(BluetoothProfile.GATT))known(device,"系统已连接 · 待验证",listener);
            scanner=adapter.getBluetoothLeScanner();if(scanner==null)throw new IOException("蓝牙扫描不可用，请先开启蓝牙。");
            callback=new ScanCallback(){
                @Override public void onScanResult(int type,ScanResult result){
                    if(epoch!=generation||result.getScanRecord()==null)return;
                    String address=result.getDevice().getAddress(),name=result.getScanRecord().getDeviceName();
                    listener.found(name==null?"蓝牙MIDI设备":name,address,"已广播MIDI/控制服务");
                }
                @Override public void onScanFailed(int code){if(epoch==generation){stop();listener.ended("扫描失败（"+code+"）。");}}
            };
            scanner.startScan(Arrays.asList(new ScanFilter.Builder().setServiceUuid(ParcelUuid.fromString(BleMidiPort.SERVICE)).build(),new ScanFilter.Builder().setServiceUuid(ParcelUuid.fromString(BleControlProfile.SERVICE)).build()),
                    new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),callback);
            ui.postDelayed(()->{if(epoch==generation){stop();listener.ended("扫描结束；配对/系统连接仍需验证控制服务。未发现时请核对ESP32固件、UUID及设备占用。");}},10000);
        }catch(SecurityException e){stop();throw new IOException("缺少蓝牙扫描权限。",e);}
    }
    void stop(){
        generation++;ui.removeCallbacksAndMessages(null);BluetoothLeScanner old=scanner;ScanCallback oldCallback=callback;scanner=null;callback=null;
        if(old!=null&&oldCallback!=null)try{old.stopScan(oldCallback);}catch(SecurityException ignored){}
    }
}
