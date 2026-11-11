import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {startV2Server} from './v2-server.mjs';
import {AdditiveDevice} from './additive-device.mjs';
export async function startAdditiveServer(options={}){return startV2Server({...options,device:options.device||new AdditiveDevice(options)});}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  const at=process.argv.indexOf('--port'),live=await startAdditiveServer({port:at<0?4175:Number(process.argv[at+1])});
  console.log('加法合成器模拟预览（不产生设备声音）: '+live.url);
  for(const signal of ['SIGINT','SIGTERM'])process.on(signal,async()=>{await live.close();process.exit(0);});
}
