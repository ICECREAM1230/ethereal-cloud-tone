// Inverse mappings verified against frozen voice_alloc.c; outputs are patch values.
export const clampCC = value => Math.max(0, Math.min(127, Math.round(value)));
const percent = value => Math.round(value / 127 * 100) + '%';
const signed = value => (value > 0 ? '+' : '') + value;
export const WAVES = [[0,'锯齿波'],[32,'脉冲波'],[64,'三角波'],[96,'正弦波']];
const waveName = value => WAVES[value]?.[1] || '未知波形';
const list = (cc, group, label, read, options, hint = '') => ({cc,group,label,read,options,hint});
const slider = (cc, group, label, read, hint = '') => list(cc,group,label,read,null,hint);
const rateCC = value => clampCC(127 * Math.log(Math.max(1,value) * 48000 / 16777216 / .03) / Math.log(1000));
const rateText = value => (value * 48000 / 16777216).toFixed(2) + ' Hz';
export const CONTROLS = [
  slider(7,'master','主音量',s=>[s.volume/2,Math.round(s.volume/254*100)+'%'],'相对音量'),
  slider(10,'master','左右声像',s=>[s.pan+64,s.pan===0?'居中':(s.pan<0?'左 ':'右 ')+Math.round(Math.abs(s.pan)/(s.pan<0?64:63)*100)+'%']),
  list(20,'osc','振荡器 1',s=>[s.osc[0][0]*32,waveName(s.osc[0][0])],WAVES),
  list(21,'osc','振荡器 2',s=>[s.osc[1][0]*32,waveName(s.osc[1][0])],WAVES),
  list(26,'osc','声部结构',s=>[[0,43,86][s.voice],['双振荡器','7 + 1 齐奏','4 + 4 齐奏'][s.voice]],[[0,'双振荡器'],[43,'7 + 1 齐奏'],[86,'4 + 4 齐奏']]),
  slider(24,'osc','振荡器混合',s=>[s.mix+64,'OSC 2 · '+percent(s.mix+64)]),
  slider(27,'osc','齐奏失谐',s=>[s.detune*4,(s.detune*1200/1024).toFixed(1)+' 音分 / 步']),
  slider(28,'osc','立体声宽度',s=>[s.spread,percent(s.spread)]),
  list(29,'filter','滤波类型',s=>[[0,43,86][s.filter[2]],['低通','带通','高通'][s.filter[2]]],[[0,'低通 LP'],[43,'带通 BP'],[86,'高通 HP']]),
  list(30,'filter','滤波斜率',s=>[s.filter[3]*64,s.filter[3]?'24 dB/oct':'12 dB/oct'],[[0,'12 dB/oct'],[64,'24 dB/oct']]),
  slider(74,'filter','明暗 · 粗调',s=>[(s.cutoff_offset+8192)>>7,signed((s.cutoff_offset/1024).toFixed(2))+' 八度'],'相对截止频率偏移'),
  slider(106,'filter','明暗 · 细调',s=>[(s.cutoff_offset+8192)&127,((s.cutoff_offset+8192)&127)+' / 127'],'保留粗调，微调当前明暗'),
  slider(71,'filter','共振',s=>[(s.filter[1]+s.resonance_offset)*127/5632,Math.round((s.filter[1]+s.resonance_offset)/5632*100)+'%'],'相对强度'),
  slider(31,'filter','键盘跟踪',s=>[s.filter[4],Math.round(s.filter[4]/64*100)+'%']),
  slider(107,'mod','调制深度',s=>[64+Math.sign(s.mod_depth)*Math.sqrt(Math.abs(s.mod_depth)/4),signed((s.mod_depth/1024).toFixed(2))+' 八度'],'调制包络 → 截止频率'),
  slider(86,'amp','力度响应',s=>[s.velocity[0],Math.round(s.velocity[0]/64*100)+'%'],'力度 → 音量包络'),
  slider(87,'mod','力度响应',s=>[s.velocity[1],Math.round(s.velocity[1]/64*100)+'%'],'力度 → 调制包络'),
  list(113,'lfo1','波形',s=>[s.lfo[0][0]*32,waveName(s.lfo[0][0])],WAVES),
  slider(76,'lfo1','速度',s=>[rateCC(s.lfo[0][1]),rateText(s.lfo[0][1])],'依据 48 kHz 相位增量换算'),
  slider(77,'lfo1','深度',s=>[s.lfo[0][2]/4,(s.lfo[0][2]*1200/1024).toFixed(1)+' 音分']),
  list(111,'lfo2','波形',s=>[s.lfo[1][0]*32,waveName(s.lfo[1][0])],WAVES),
  list(112,'lfo2','调制目标',s=>[s.lfo[1][3]*32,['脉冲宽度','共振','音高','截止频率'][s.lfo[1][3]]],[[0,'脉冲宽度'],[32,'共振'],[64,'音高'],[96,'截止频率']]),
  slider(109,'lfo2','速度',s=>[rateCC(s.lfo[1][1]),rateText(s.lfo[1][1])],'依据 48 kHz 相位增量换算'),
  slider(110,'lfo2','深度',s=>[s.lfo[1][2]/[16,32,4,32][s.lfo[1][3]],s.lfo[1][3]===2?(s.lfo[1][2]*1200/1024).toFixed(1)+' 音分':s.lfo[1][3]===0?Math.round(s.lfo[1][2]/1024*100)+'% · 占空比幅度':(s.lfo[1][2]/1024).toFixed(2)+' 八度'])
];
for (let oscillator = 0; oscillator < 2; oscillator++) {
  const group = 'tune'+(oscillator+1);
  CONTROLS.push(
    slider([14,22][oscillator],group,'半音',s=>[Math.ceil((s.osc[oscillator][1]+12)*128/25),signed(s.osc[oscillator][1])+' 半音']),
    slider([15,23][oscillator],group,'微调',s=>[64+Math.sign(s.osc[oscillator][2])*Math.ceil(Math.abs(s.osc[oscillator][2])*1.5),signed((s.osc[oscillator][2]*1200/1024).toFixed(1))+' 音分']),
    slider([25,85][oscillator],group,'脉冲宽度',s=>[-127*Math.log10(Math.max(.0001,1-s.osc[oscillator][3]/8388608)),(100*(.5-s.osc[oscillator][3]/16777216)).toFixed(1)+'%'],'仅脉冲波可听到宽度变化')
  );
}
for (const [group,field,ccs] of [['amp','amp',[73,75,79,72]],['mod','mod',[102,103,104,105]]]) {
  ['起音 Attack','衰减 Decay','保持 Sustain','释音 Release'].forEach((label,index)=>{
    CONTROLS.push(slider(ccs[index],group,label,s=>[index===2?s[field][index]/2:127-s[field][index]/2,percent(index===2?s[field][index]/2:127-s[field][index]/2)],index===2?'相对保持电平':'相对时间档位，向右更长'));
  });
}
export function readControl(control,state) {
  const [value,text] = control.read(state);
  return { value:clampCC(value), text:String(text) };
}
