import {calibrationWords} from './fdc.mjs';

export function mountFdcPanel(root,owner){
  const by=id=>root.querySelector('#'+id);let notice='';
  function update(){
    const fdc=owner.fdc,state=fdc.state;
    const tag=state?.status_word>>>16;
    const engine=tag===0xfdc1?'减法合成器':tag===0xfdc2?'FM 电钢琴':null;
    by('fdcStatus').textContent=notice||(owner.apiMajor===1?'此固件尚未提供 FDC 控制；原有演奏仍可使用。':fdc.message);
    by('fdcEnable').disabled=!fdc.can(1);by('fdcDisable').disabled=!fdc.can(0);
    by('fdcRestart').disabled=!fdc.can(2);by('fdcApply').disabled=!fdc.can(3);
    by('fdcRefresh').disabled=!fdc.connected||fdc.polling;
    by('fdcCalibration').disabled=!fdc.can(3);
    by('fdcObserved').textContent=!state?.valid||!state.fresh?'尚无新鲜有效的设备状态。':!state.available?'设备未提供 FDC 能力，原有演奏仍可使用。':
      '当前观察：'+(state.driver_enabled?'控制已启用':'控制已关闭')+'；'+(state.requested_enable?'设备有启用请求':'设备无启用请求')+'；'+(state.busy?'正在停止、配置或重启排空':'无操作等待排空')+'。';
    by('fdcHealth').textContent=!state?.available?'':
      (!engine?'当前设备尚未提供匹配的特雷门通路。':state.eligible?engine+'通路可用。':'等待'+engine+'通路就绪后可启用。')+
      (state.driver_fault?' 传感器或驱动故障；空闲时可请求重启恢复。':'')+(state.local_error?' 最近操作有本地错误，请核对配置。':'')+
      (state.user_config_active?' 设备曾采用用户配置，当前天线仍需实际验证。':' 设备使用初始配置，尚无用户配置采用记录。');
    const voice=root.ownerDocument?.getElementById('thereminVoice');
    if(voice)voice.textContent=state?.valid&&state.fresh&&engine?engine+(tag===0xfdc1?' · 当前减法音色':'')+' · 持续发声':'等待配套特雷门固件';
    const raw=by('fdcRaw'),t=state?.raw;
    if(raw)raw.textContent=!t?.supported?'当前固件尚未提供原始数据。':!state?.fresh||!t.valid?'原始数据失鲜，等待新的读回。':
      'CH0 音高：'+(t.sample_valid&&Number.isInteger(t.ch0)?t.ch0:'—')+'  ·  CH1 音量：'+(t.sample_valid&&Number.isInteger(t.ch1)?t.ch1:'—')+
      '  ·  增益：'+(Number.isInteger(t.gain_q16)?t.gain_q16+'/65536':'—')+
      (t.sample_fresh&&t.fresh?'':'  ·  样本失鲜')+(t.muted?'  ·  静音':'');
  }
  function send(op,words){notice='';try{if(!owner.fdc.queue(op,words))notice='设备或会话尚未就绪，请先刷新状态。';}catch(error){notice=error.message;}update();}
  by('fdcEnable').addEventListener('click',()=>send(1));
  by('fdcDisable').addEventListener('click',()=>send(0));
  by('fdcRestart').addEventListener('click',()=>send(2));
  by('fdcRefresh').addEventListener('click',()=>{notice='';owner.fdc.poll({explicit:true});});
  by('fdcApply').addEventListener('click',()=>{
    notice='';try{
      const values={};for(const name of ['pitch_far','pitch_near','volume_far','volume_near','pitch_sigma','volume_sigma']){
        const input=root.querySelector('[name="'+name+'"]');
        if(!input.value.trim())throw Error('请填写完整的六个校准值。');values[name]=Number(input.value);
      }
      send(3,calibrationWords(values));
    }catch(error){notice=error.message;update();}
  });
  return {update};
}
