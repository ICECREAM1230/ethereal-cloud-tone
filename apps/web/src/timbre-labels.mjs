// Display labels only: catalog keys, preset IDs and storage paths stay unchanged.
const FM_PRESET_LABELS = Object.freeze({
  diagnostic_sine: ['正弦测试音', '单个载波发声的正弦测试音，便于检查音频通路。'],
  two_op_keys: ['双算子键盘', '双算子键盘音色。'],
  four_op_bell: ['四算子钟声', '四算子钟声音色。'],
  electric_piano_tine: ['电钢琴', '四算子电钢琴音色，温暖的主体配合短促明亮的击槌起音。'],
  metal_bell: ['金属钟声', '四算子金属钟声，金属敲击起音配合较长的自然衰减。']
});
const SUB_PRESET_LABELS = Object.freeze({
  'Patches/Factory/Pads/Supersaw.toml': '铺底 / 超级锯齿',
  'Patches/Factory/Pads/Galaxy.toml': '铺底 / 银河',
  'Patches/User/Bass/Synth Bass.toml': '贝斯 / 合成贝斯',
  'Patches/User/Leads/Synth Lead.toml': '主奏 / 合成主奏',
  'Patches/User/Pads/Warm Pad.toml': '铺底 / 温暖铺底'
});

export function fmPresetName(preset) {
  return FM_PRESET_LABELS[preset?.name]?.[0] ?? preset?.name ?? '';
}
export function fmPresetDescription(preset) {
  return FM_PRESET_LABELS[preset?.name]?.[1] ?? preset?.description ?? '';
}
export function subPresetName(path) {
  // Only the exact supplied presets are translated; custom user names survive.
  return SUB_PRESET_LABELS[path] ?? path.split('/').slice(2).join(' / ').replace(/\.toml$/i, '');
}
