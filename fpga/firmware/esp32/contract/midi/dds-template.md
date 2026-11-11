# DDS performance template ABI v1 (2026-10-16)

地址是16位WORD地址，现有SPI Mode0/32bit data保持。DDS独占0x8000..0x81ff。外部统一写接口：perf_write_enable, perf_write_addr[15:0], perf_write_data[31:0]，均为SPI最终提交沿的sclk域信号。

S3继续编译patch/预置，不因每个MIDI音符执行renderer。S3对8个unison element编译以下模板（voice结构、detune、mix、pan、spread、makeup与原voice_program完全一致）：

- 对u=0..7，base=0x8000+u*8。
- base+0：int32 signed pitch_offset = SEMI_LSB(osc.coarse)+osc.fine+unison_detune。**未加入note、未钳位**；当前patch合法范围须落signed18 (-131072..131071)，高位正确符号扩展。
- base+1：原DUTY低24位，其余0。
- base+2：原filter.resonance（低14位未移位），其余0。
- base+3：原GAIN字低19位（双侧UQ4.4 volume、filter.dual/type），即原voice_program在vel127时生成的静态GAIN。velocity不作用GAIN。
- base+4：低2位wave，bit8=此element有效（2plain的其余6个为0）。其余0。无效element其它字可0。
- base+5..7：保留，写0可接受。
- 0x8040：filter.key_track（0..128，对应0..200%）；FPGA精确执行 `fc = pitch(60)+trunc((pitch(note)-pitch(60))*key_track/64)+512`，先32bit signed计算再钳位0..16383。
- 0x8041：bend_range（半音，低7位，当前DDS g_patch.bend_range）；FPGA按原 `trunc((bend14-8192)*range/96)` 计算每source/channel offset。全局pitch bus继续供patch/LFO，S3在performance启用时不再把MIDI bend写bus2或全局cut bus。
- 0x8100+velocity（0..127）：amp_depth_for(velocity)，低18位unsigned，编码与原producer DEPTH相同。
- 0x8180+velocity（0..127）：mod_depth_for(velocity)，低18位signed two's complement，编码与原producer DEPTH相同。
- 0x80ff：COMMIT，非零u32递增generation。前述shadow完整上传后一次写入。必须等待提交ACK后才开始下一轮shadow修改；不允许COMMIT尚忙期间修改shadow（bundled-data CDC协议）。FPGA原子锁存完整shadow到active template，并刷新HELD声部静态context与depth；RELEASING保持上次静态context/depth。新NoteOn只采用已提交template；首次COMMIT前runtime精确mute且不接受NoteOn。

S3仍通过原element/producers/livebus SPI ABI初始化静态producer rates、routing、master gain BASE、LFO、全局patch参数。performance仅覆盖：每voice pitch/filter base、精确mute、CSP gate80+v、amp producer32+v及mod producer64+2v的depth。S3停止按MIDI做voice_alloc、gate/element动态音符渲染；模板改变时先完成旧静态bank/producer提交，再COMMIT模板，避免新模板与旧rates瞬时混合。

顶层接线端口保持root所给midi_enable, midi_event, midi_event_valid/ready, midi_flush[3:0]及perf_write_*。增加建议输出：`midi_template_status[31:0]`（最后已原子锁存COMMIT generation），由root映射0x07xx只读状态供S3等ACK；若不加入读回，S3无法可靠判断CDC提交完成，不能用固定延迟代替ACK。本模块使用小模板控制寄存器及既有官方ect_bus_sum_ram wrapper存放runtime元素context，不新造厂商IP。

行为：4source*16channel独立sustain/bend，同音FIFO Off，vel0=Off，32voice LRU idle→release→held。disconnect flush按source release，disable清空runtime并硬mute，On前gate低跨两个CSP完成pass。release尾音用CSP实际ADSR归一化level<=1/1024反馈精确mute。

## 量化与钳位顺序（源码oracle核对）

必须保留旧C/RTL的两层钳位：1) MIDI pitch使用 `floor(note*1024/12)`（旧octave/remainder实现完全等价），加未钳位osc coarse/fine/unison offset，然后钳位0..16383，写runtime context。2) 该base进入原effective_params，叠加旧global pitch/LFO与本voice MIDI bend后再次钳位0..16383。filter同理先按旧key_track公式计算并钳位，再由原effective_params叠加cutoff bus和bend并钳位FC_MAX。不能把base+bend合并成一次clamp。literal：note127、offset+7000得base16383，负满轮range2得到bend=-170，最终16213；note12、offset-7000得base0，正满轮bend170，最终170。此顺序对照 voice_alloc.c:485–487、778–785 与 effective_params.sv:26–30，属既有声音语义。
