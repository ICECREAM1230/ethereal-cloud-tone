package cn.fpgasynth.control;

import android.bluetooth.*;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import cn.fpgasynth.control.core.BleControlProfile;
import cn.fpgasynth.control.core.BleControlTransport;
import cn.fpgasynth.control.core.BleMidiPort;
import java.io.IOException;
import java.util.UUID;

/** Physical BLE client only. Construction is inert; all callbacks belong to this instance. */
final class BleGattLink implements BleControlTransport.Link {
    private static final UUID CCCD=UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private static final UUID MIDI_SERVICE=UUID.fromString(BleMidiPort.SERVICE),MIDI_CHARACTERISTIC=UUID.fromString(BleMidiPort.CHARACTERISTIC);
    private final Context context;
    private final String address;
    private final BleControlProfile profile;
    private final boolean standardMidi;
    private final Object lock=new Object();
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic tx,rx;
    private HandlerThread thread;
    private BleControlTransport.Receiver receiver;
    private boolean ready,closed,writing;
    private int limit=20;
    private IOException failure;
    BleGattLink(Context context,String address){this(context,address,null,true);}
    BleGattLink(Context context,String address,BleControlProfile profile){
        this(context,address,profile,false);
    }
    private BleGattLink(Context context,String address,BleControlProfile profile,boolean standardMidi){
        this.context=context.getApplicationContext();this.address=address;this.profile=profile;this.standardMidi=standardMidi;
    }
    @Override public void setReceiver(BleControlTransport.Receiver receiver){synchronized(lock){this.receiver=receiver;}}
    private boolean current(BluetoothGatt value){return !closed&&gatt==value;}
    private void live()throws IOException{if(failure!=null)throw failure;if(closed)throw new IOException("蓝牙连接已关闭。");}
    private void await(long end)throws IOException{
        live();long left=end-SystemClock.elapsedRealtime();if(left<=0)throw new IOException("蓝牙连接或写入超时。");
        try{lock.wait(left);}catch(InterruptedException e){Thread.currentThread().interrupt();throw new IOException("蓝牙操作已取消。",e);}
    }
    private void fail(String message){
        BleControlTransport.Receiver sink;IOException error=new IOException(message);
        synchronized(lock){if(closed)return;failure=error;sink=receiver;lock.notifyAll();}
        if(sink!=null)sink.failed(error);close();
    }
    @Override public void open()throws IOException{
        try{
            synchronized(lock){
                live();if(ready)return;
                if(gatt==null){
                    BluetoothManager manager=context.getSystemService(BluetoothManager.class);
                    BluetoothAdapter adapter=manager==null?null:manager.getAdapter();
                    if(adapter==null||!adapter.isEnabled())throw new IOException("蓝牙不可用或尚未开启。");
                    thread=new HandlerThread("synth-ble-callbacks");thread.start();
                    gatt=adapter.getRemoteDevice(address).connectGatt(context,false,callbacks,
                            BluetoothDevice.TRANSPORT_LE,BluetoothDevice.PHY_LE_1M_MASK,new Handler(thread.getLooper()));
                    if(gatt==null)throw new IOException("无法创建蓝牙连接。");
                }
                long end=SystemClock.elapsedRealtime()+8000;
                while(!ready){await(end);}live();
            }
        }catch(SecurityException|IllegalArgumentException e){close();throw new IOException("蓝牙权限或设备地址无效。",e);}
        catch(IOException e){close();throw e;}
    }
    private void subscribe(BluetoothGatt value){
        try{
            BluetoothGattDescriptor descriptor=rx.getDescriptor(CCCD);
            if(descriptor==null||!value.setCharacteristicNotification(rx,true)){fail("设备缺少控制响应通知接口。");return;}
            byte[] enable=(rx.getProperties()&BluetoothGattCharacteristic.PROPERTY_NOTIFY)!=0?
                    BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE:BluetoothGattDescriptor.ENABLE_INDICATION_VALUE;
            boolean started;
            if(Build.VERSION.SDK_INT>=33)started=value.writeDescriptor(descriptor,enable)==BluetoothStatusCodes.SUCCESS;
            else{descriptor.setValue(enable);started=value.writeDescriptor(descriptor);}
            if(!started)fail("无法订阅蓝牙控制响应。");
        }catch(SecurityException e){fail("蓝牙权限已撤回。");}
    }
    private final BluetoothGattCallback callbacks=new BluetoothGattCallback(){
        @Override public void onConnectionStateChange(BluetoothGatt value,int status,int state){
            synchronized(lock){if(!current(value))return;}
            if(status!=BluetoothGatt.GATT_SUCCESS||state==BluetoothProfile.STATE_DISCONNECTED){fail("蓝牙连接中断（"+status+"），请重新连接。");return;}
            if(state==BluetoothProfile.STATE_CONNECTED)try{if(!value.discoverServices())fail("蓝牙服务发现未启动。");}
            catch(SecurityException e){fail("蓝牙权限已撤回。");}
        }
        @Override public void onServicesDiscovered(BluetoothGatt value,int status){
            synchronized(lock){if(!current(value))return;}
            if(status!=BluetoothGatt.GATT_SUCCESS){fail("蓝牙服务发现失败。");return;}
            if(standardMidi){
                BluetoothGattService service=value.getService(MIDI_SERVICE);
                BluetoothGattCharacteristic midi=service==null?null:service.getCharacteristic(MIDI_CHARACTERISTIC);
                if(midi==null||(midi.getProperties()&BluetoothGattCharacteristic.PROPERTY_WRITE)==0){
                    fail("设备缺少可确认写入的标准BLE-MIDI特征。");return;
                }
                synchronized(lock){if(current(value)){tx=midi;limit=20;ready=true;lock.notifyAll();}}
                return;
            }
            BluetoothGattService service=value.getService(profile.service);
            if(service==null){fail("设备尚未实现App控制服务；不能使用标准BLE MIDI接口代替。");return;}
            tx=service.getCharacteristic(profile.request);rx=service.getCharacteristic(profile.response);
            if(tx==null||rx==null||(tx.getProperties()&BluetoothGattCharacteristic.PROPERTY_WRITE)==0||
                    (rx.getProperties()&(BluetoothGattCharacteristic.PROPERTY_NOTIFY|BluetoothGattCharacteristic.PROPERTY_INDICATE))==0){
                fail("设备控制特征或收发权限不匹配。");return;
            }
            try{if(!value.requestMtu(185))subscribe(value);}catch(SecurityException e){fail("蓝牙权限已撤回。");}
        }
        @Override public void onMtuChanged(BluetoothGatt value,int mtu,int status){
            synchronized(lock){if(!current(value)||ready||standardMidi)return;limit=status==BluetoothGatt.GATT_SUCCESS?Math.max(20,Math.min(514,mtu-3)):20;}
            subscribe(value);
        }
        @Override public void onDescriptorWrite(BluetoothGatt value,BluetoothGattDescriptor descriptor,int status){
            synchronized(lock){if(!current(value)||rx==null||descriptor.getCharacteristic()!=rx||!descriptor.getUuid().equals(CCCD))return;}
            if(status!=BluetoothGatt.GATT_SUCCESS){fail("蓝牙通知订阅失败。");return;}
            synchronized(lock){if(current(value)){ready=true;lock.notifyAll();}}
        }
        @Override public void onCharacteristicWrite(BluetoothGatt value,BluetoothGattCharacteristic c,int status){
            synchronized(lock){if(!current(value)||c!=tx||!writing)return;writing=false;
                if(status!=BluetoothGatt.GATT_SUCCESS)failure=new IOException("蓝牙写入未成功（"+status+"）。");lock.notifyAll();}
        }
        private void notifyBytes(BluetoothGatt value,BluetoothGattCharacteristic c,byte[] bytes){
            BleControlTransport.Receiver sink;
            synchronized(lock){if(!current(value)||!ready||standardMidi||c!=rx)return;sink=receiver;}
            if(bytes==null){fail("蓝牙控制响应为空。");return;}
            if(sink!=null)sink.received(bytes.clone());
        }
        @Override public void onCharacteristicChanged(BluetoothGatt value,BluetoothGattCharacteristic c,byte[] bytes){notifyBytes(value,c,bytes);}
        @SuppressWarnings("deprecation") @Override public void onCharacteristicChanged(BluetoothGatt value,BluetoothGattCharacteristic c){
            if(Build.VERSION.SDK_INT<33)notifyBytes(value,c,c.getValue());
        }
    };
    @Override public int packetLimit(){synchronized(lock){return limit;}}
    @SuppressWarnings("deprecation") @Override public void write(byte[] packet)throws IOException{
        try{
            synchronized(lock){
                live();if(!ready||writing)throw new IOException("蓝牙控制端口尚未就绪。");writing=true;
                boolean started;
                if(Build.VERSION.SDK_INT>=33)started=gatt.writeCharacteristic(tx,packet.clone(),BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)==BluetoothStatusCodes.SUCCESS;
                else{tx.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);tx.setValue(packet.clone());started=gatt.writeCharacteristic(tx);}
                if(!started){writing=false;throw new IOException("蓝牙写请求未被接受。");}
                long end=SystemClock.elapsedRealtime()+500;
                while(writing){await(end);}live();
            }
        }catch(SecurityException e){close();throw new IOException("蓝牙权限已撤回。",e);}
        catch(IOException e){close();throw e;}
    }
    @Override public void close(){
        BluetoothGatt old;HandlerThread worker;
        synchronized(lock){if(closed)return;closed=true;ready=false;writing=false;old=gatt;gatt=null;worker=thread;thread=null;lock.notifyAll();}
        if(old!=null){try{old.disconnect();}catch(SecurityException ignored){}finally{try{old.close();}catch(SecurityException ignored){}}}
        if(worker!=null)worker.quitSafely();
    }
}
