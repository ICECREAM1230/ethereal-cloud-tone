#!/usr/bin/env python3
"""Isolated BLE-MIDI JVM tests and GATT SDK compile; never opens a radio or device."""
import os
import subprocess
from toolchain import ROOT, JDK_BIN, ANDROID

out = ROOT / 'build/ble-midi-transport'
classes = out / 'classes'
classes.mkdir(parents=True, exist_ok=True)
sourcepath = os.pathsep.join(str(ROOT / path) for path in (
    'core/src/main/java', 'core/src/generated/java'))
test = ROOT / 'core/src/test/java/cn/fpgasynth/control/core/BleMidiPortTest.java'
subprocess.run([str(JDK_BIN / 'javac'), '-encoding', 'UTF-8', '--release', '8', '-Xlint:all',
                '-sourcepath', sourcepath, '-d', str(classes), str(test)], check=True)
subprocess.run([str(JDK_BIN / 'java'), '-ea', '-cp', str(classes),
                'cn.fpgasynth.control.core.BleMidiPortTest'], check=True)
android_classes = out / 'android-classes'
android_classes.mkdir(parents=True, exist_ok=True)
subprocess.run([str(JDK_BIN / 'javac'), '-encoding', 'UTF-8', '--release', '8',
                '-classpath', os.pathsep.join((str(ANDROID), str(classes))),
                '-sourcepath', sourcepath, '-d', str(android_classes),
                str(ROOT / 'app/src/main/java/cn/fpgasynth/control/BleGattLink.java')], check=True)
print('BleGattLink Android API 36 compile PASS (min API 26 branches retained)')

# The real SDK above checks API signatures. These small platform doubles below
# exercise our callback decisions without Android runtime, permissions or radio I/O.
platform = {
    'android/content/Context.java': '''package android.content;
public class Context {
    public Context getApplicationContext(){return this;}
    public <T>T getSystemService(Class<T> type){return null;}
}''',
    'android/os/Build.java': '''package android.os;
public final class Build {public static final class VERSION {public static int SDK_INT=33;}}''',
    'android/os/SystemClock.java': '''package android.os;
public final class SystemClock {public static long elapsedRealtime(){return System.nanoTime()/1000000;}}''',
    'android/bluetooth/BluetoothGattCallback.java': '''package android.bluetooth;
public abstract class BluetoothGattCallback {
    public void onConnectionStateChange(BluetoothGatt g,int status,int state){}
    public void onServicesDiscovered(BluetoothGatt g,int status){}
    public void onMtuChanged(BluetoothGatt g,int mtu,int status){}
    public void onDescriptorWrite(BluetoothGatt g,BluetoothGattDescriptor d,int status){}
    public void onCharacteristicWrite(BluetoothGatt g,BluetoothGattCharacteristic c,int status){}
    public void onCharacteristicChanged(BluetoothGatt g,BluetoothGattCharacteristic c,byte[] bytes){}
    public void onCharacteristicChanged(BluetoothGatt g,BluetoothGattCharacteristic c){}
}''',
    'android/bluetooth/BluetoothGattCharacteristic.java': '''package android.bluetooth;
import java.util.*;
public class BluetoothGattCharacteristic {
    private final UUID uuid;private final int properties;
    private final Map<UUID,BluetoothGattDescriptor> descriptors=new HashMap<>();
    private byte[] value;public int writeType;
    public BluetoothGattCharacteristic(UUID uuid,int properties,int permissions){this.uuid=uuid;this.properties=properties;}
    public UUID getUuid(){return uuid;}
    public int getProperties(){return properties;}
    public BluetoothGattDescriptor getDescriptor(UUID uuid){return descriptors.get(uuid);}
    public boolean addDescriptor(BluetoothGattDescriptor d){d.characteristic=this;descriptors.put(d.getUuid(),d);return true;}
    public boolean setValue(byte[] bytes){value=bytes;return true;}
    public byte[] getValue(){return value;}
    public void setWriteType(int type){writeType=type;}
}''',
    'android/bluetooth/BluetoothGattDescriptor.java': '''package android.bluetooth;
import java.util.UUID;
public class BluetoothGattDescriptor {
    public static final byte[] ENABLE_NOTIFICATION_VALUE={1,0},ENABLE_INDICATION_VALUE={2,0};
    private final UUID uuid;BluetoothGattCharacteristic characteristic;
    public BluetoothGattDescriptor(UUID uuid,int permissions){this.uuid=uuid;}
    public UUID getUuid(){return uuid;}
    public BluetoothGattCharacteristic getCharacteristic(){return characteristic;}
    public boolean setValue(byte[] value){return true;}
}''',
    'android/bluetooth/BluetoothGattService.java': '''package android.bluetooth;
import java.util.*;
public class BluetoothGattService {
    public final UUID uuid;private final Map<UUID,BluetoothGattCharacteristic> characteristics=new HashMap<>();
    public BluetoothGattService(UUID uuid,int type){this.uuid=uuid;}
    public boolean addCharacteristic(BluetoothGattCharacteristic c){characteristics.put(c.getUuid(),c);return true;}
    public BluetoothGattCharacteristic getCharacteristic(UUID uuid){return characteristics.get(uuid);}
}''',
    'android/bluetooth/BluetoothGatt.java': '''package android.bluetooth;
import java.util.*;
public class BluetoothGatt {
    public BluetoothGattCallback callback;public int mtuRequests,subscriptions,descriptorWrites,writes,closes,disconnects,writeType;
    public boolean mtuStarted;public int writeStatus;public byte[] packet;
    private final Map<UUID,BluetoothGattService> services=new HashMap<>();
    public void addService(BluetoothGattService service){services.put(service.uuid,service);}
    public BluetoothGattService getService(UUID uuid){return services.get(uuid);}
    public boolean discoverServices(){return true;}
    public boolean requestMtu(int mtu){mtuRequests++;return mtuStarted;}
    public boolean setCharacteristicNotification(BluetoothGattCharacteristic c,boolean enabled){subscriptions++;return true;}
    public int writeDescriptor(BluetoothGattDescriptor d,byte[] bytes){descriptorWrites++;callback.onDescriptorWrite(this,d,0);return 0;}
    public boolean writeDescriptor(BluetoothGattDescriptor d){writeDescriptor(d,new byte[]{1,0});return true;}
    public int writeCharacteristic(BluetoothGattCharacteristic c,byte[] bytes,int type){
        writes++;packet=bytes.clone();writeType=type;callback.onCharacteristicWrite(this,c,writeStatus);return 0;
    }
    public boolean writeCharacteristic(BluetoothGattCharacteristic c){writeCharacteristic(c,c.getValue(),c.writeType);return true;}
    public void disconnect(){disconnects++;}
    public void close(){closes++;}
}''',
    'cn/fpgasynth/control/BleGattLinkBehaviorTest.java': '''package cn.fpgasynth.control;
import android.bluetooth.*;
import android.content.Context;
import cn.fpgasynth.control.core.*;
import java.io.IOException;
import java.lang.reflect.Field;
import java.util.*;
public final class BleGattLinkBehaviorTest {
    private static int count;
    private static void check(boolean ok,String why){count++;if(!ok)throw new AssertionError(why);}
    private interface IO {void run()throws IOException;}
    private static void rejects(IO action,String why)throws Exception{
        try{action.run();}catch(IOException expected){count++;return;}throw new AssertionError(why);
    }
    private static final class Fixture {
        final BluetoothGatt gatt=new BluetoothGatt();final BleGattLink link;int failures,received;
        Fixture(boolean midi)throws Exception{
            link=midi?new BleGattLink(new Context(),"mock"):new BleGattLink(new Context(),"mock",new BleControlProfile());
            link.setReceiver(new BleControlTransport.Receiver(){
                public void received(byte[] packet){received++;}public void failed(IOException e){failures++;}
            });
            Field field=BleGattLink.class.getDeclaredField("gatt");field.setAccessible(true);field.set(link,gatt);
            field=BleGattLink.class.getDeclaredField("callbacks");field.setAccessible(true);gatt.callback=(BluetoothGattCallback)field.get(link);
        }
        void discover(){gatt.callback.onServicesDiscovered(gatt,0);}
    }
    private static BluetoothGattService midiService(int properties){
        BluetoothGattService service=new BluetoothGattService(UUID.fromString("03b80e5a-ede8-4b33-a751-6ce34ec4c700"),0);
        service.addCharacteristic(new BluetoothGattCharacteristic(UUID.fromString("7772e5db-3868-4112-a1a9-f2669d106bf3"),properties,0));
        return service;
    }
    private static void standard()throws Exception{
        Fixture f=new Fixture(true);BluetoothGattService service=midiService(0x1e);f.gatt.addService(service);
        f.gatt.callback.onServicesDiscovered(new BluetoothGatt(),0);
        check(f.failures==0&&f.gatt.closes==0,"foreign GATT callback cannot fail current link");
        f.discover();f.link.open();
        check(f.gatt.mtuRequests==0&&f.gatt.subscriptions==0&&f.gatt.descriptorWrites==0&&f.gatt.writes==0,
                "standard discovery is ready without MTU, CCCD, notification or protocol request");
        check(f.link.packetLimit()==20,"standard mode retains minimum ATT payload");
        byte[] packet={(byte)0x80,(byte)0x80,(byte)0x80,60,0};f.link.write(packet);
        check(f.gatt.writes==1&&f.gatt.writeType==2&&Arrays.equals(f.gatt.packet,packet),"standard write uses ATT acknowledgement and exact MIDI bytes");
        f.gatt.callback.onMtuChanged(f.gatt,517,0);
        check(f.link.packetLimit()==20&&f.gatt.subscriptions==0,"unsolicited MTU callback cannot start a subscription");
        BluetoothGattCharacteristic midi=service.getCharacteristic(UUID.fromString("7772e5db-3868-4112-a1a9-f2669d106bf3"));midi.setValue(new byte[]{1});
        if(android.os.Build.VERSION.SDK_INT>=33)f.gatt.callback.onCharacteristicChanged(f.gatt,midi,new byte[]{1});
        else f.gatt.callback.onCharacteristicChanged(f.gatt,midi);
        check(f.received==0,"standard send-only path ignores notifications");
        f.gatt.callback.onConnectionStateChange(f.gatt,133,0);
        check(f.failures==1&&f.gatt.closes==1&&f.gatt.disconnects==1,"standard disconnect reports failure and closes current GATT");
        rejects(()->f.link.write(packet),"disconnected standard link cannot write");f.link.close();
        check(f.gatt.closes==1,"link close remains idempotent");
        Fixture missing=new Fixture(true);missing.discover();rejects(missing.link::open,"missing standard service rejects connection");
        check(missing.failures==1&&missing.gatt.closes==1,"missing service closes without negotiation");
        Fixture readonly=new Fixture(true);readonly.gatt.addService(midiService(0x12));readonly.discover();
        rejects(readonly.link::open,"standard characteristic must support acknowledged writes");
        check(readonly.failures==1&&readonly.gatt.writes==0,"read/notify-only characteristic cannot become playable");
        Fixture ack=new Fixture(true);ack.gatt.addService(midiService(0x08));ack.discover();ack.link.open();ack.gatt.writeStatus=133;
        rejects(()->ack.link.write(packet),"ATT write failure reaches caller");
        check(ack.gatt.writes==1&&ack.gatt.closes==1,"failed physical write closes without retry");
    }
    private static void legacy(boolean mtuStarted)throws Exception{
        Fixture f=new Fixture(false);f.gatt.mtuStarted=mtuStarted;
        BluetoothGattService service=new BluetoothGattService(UUID.fromString(BleControlProfile.SERVICE),0);
        BluetoothGattCharacteristic tx=new BluetoothGattCharacteristic(UUID.fromString(BleControlProfile.REQUEST),0x08,0);
        BluetoothGattCharacteristic rx=new BluetoothGattCharacteristic(UUID.fromString(BleControlProfile.RESPONSE),0x10,0);
        rx.addDescriptor(new BluetoothGattDescriptor(UUID.fromString("00002902-0000-1000-8000-00805f9b34fb"),0));
        service.addCharacteristic(tx);service.addCharacteristic(rx);f.gatt.addService(service);f.discover();
        if(mtuStarted)f.gatt.callback.onMtuChanged(f.gatt,517,0);f.link.open();
        check(f.gatt.mtuRequests==1&&f.gatt.subscriptions==1&&f.gatt.descriptorWrites==1,
                "legacy private mode retains MTU request and CCCD subscription");
        check(f.link.packetLimit()==(mtuStarted?514:20),"legacy negotiated/fallback packet limit preserved");
        rx.setValue(new byte[]{1});
        if(android.os.Build.VERSION.SDK_INT>=33)f.gatt.callback.onCharacteristicChanged(f.gatt,rx,new byte[]{1});
        else f.gatt.callback.onCharacteristicChanged(f.gatt,rx);
        check(f.received==1,"legacy private responses still reach receiver");
        f.link.close();check(f.gatt.closes==1,"legacy physical close preserved");
    }
    public static void main(String[] args)throws Exception{
        for(int sdk:new int[]{26,33}){android.os.Build.VERSION.SDK_INT=sdk;standard();legacy(false);legacy(true);}
        System.out.println("BleGattLinkBehaviorTest PASS ("+count+" checks; API26/33 fake Android platform, no radio)");}
}''',
}
fake_root = out / 'android-platform'
fake_sources = []
for relative, source in platform.items():
    target = fake_root / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(source + '\n', encoding='utf-8')
    fake_sources.append(str(target))
fake_classes = out / 'android-platform-classes'
fake_classes.mkdir(parents=True, exist_ok=True)
subprocess.run([str(JDK_BIN / 'javac'), '-encoding', 'UTF-8', '--release', '8', '-classpath',
                os.pathsep.join((str(ANDROID), str(classes), str(android_classes))),
                '-d', str(fake_classes), *fake_sources], check=True)
subprocess.run([str(JDK_BIN / 'java'), '-ea', '-cp',
                os.pathsep.join(map(str, (fake_classes, android_classes, classes, ANDROID))),
                'cn.fpgasynth.control.BleGattLinkBehaviorTest'], check=True, timeout=15)
