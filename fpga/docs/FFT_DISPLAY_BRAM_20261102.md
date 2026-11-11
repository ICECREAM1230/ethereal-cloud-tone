# FFT 显示缓存改用 BRAM（2026-11-02）

当前：已直接修改AE350主工程、功能验证与新正式综合验收PASS。主PnR pnr_20261102T132447Z已后台启动，PID1284385/viewer1284392，按AGENTS结束本轮，不监控新PnR/STA/实板。唯一综合检查已删除。

## 最终实现

唯一产品文件src/display/fft/spectrum_display.sv，SHA94e64a906ed510346a41611f91e6487d726ca9d26ed690ac206f0d8ed1891825。其余349构建输入不变；高云FFT、10kHz轴、AB13切换、粒子/音频/CPU/FM/CH569与CST/SDC保持。

目标值256×10bit单bank；动画状态256×32bit单bank，打包[29:16]bar_q4、[15:6]peak、[5:0]peak_hold。helper同文件，无FileList变动。活动扫描读像素，vblank读动画，COMMIT禁读写回；新快照保留两级选择，移除旧target多读存储实例。复位后两bank并行逐地址清零256clk，不暂停raster；首绘图区前完成。同步读输出吸收原stage1，保持6拍视频和5phase/1280clk更新/ACK。

## 已验收资源（主SYN131423）

| 项目 | 原350基线 | 当前 |
|---|---:|---:|
| 显示本体SSRAM | 560 | 0 |
| 显示含粒子SSRAM | 582 | 22 |
| 显示BSRAM | 18 | 3 |
| 整机SSRAM | 763 | 203 |
| 整机BSRAM | 294 | 279 |
| 整机LUT | 73661 | 72644 |
| 整机Register | 56760 | 56466 |
| 整机ALU | 10555 | 10563 |

两个bank实际均SDPB，target位宽16、metadata32，READ_MODE0，无旧DPB mode2。显示剩余1个BSRAM为原有存储；22个SSRAM属于粒子。数据访问与复位辅助资源被合并，未降低FFT计算精度或显示帧率。

## 验证与保全

sim/results/fft_bram_20261102/summary.json记录130201791 RGB对照/130201761六拍检查/781210746 RAM检查、复位/峰衰减/粒子；主树安装后同SHA10.4M短测再绑定PASS、0warning，前后实际RTL PNG逐字节一致。临时仿真230文件648538027B清理；TB/config/重要结果保留。

修改前350相干输入、369文件前像及856项843MB旧impl完整副本在/media/ice/D;/PROJECT_improve/fft_bram_20261102T214111/baseline/。原SYN125509因GUI重新打开而rawexit2的网表/session另保全，未篡改退出状态或helper。用户关闭GUI后新SYN131423以真实exit0/accepted receipt通过，资源及350当前身份相干。

新验收记录：/media/ice/D;/PROJECT_improve/fft_bram_20261102T214111/project/colour_validation/evidence/main-synthesis-check-syn_20261102T131423Z.json；PnR启动记录同目录main-pnr-startup.json。资源下降不代表STA闭合；本轮没有设备写入或Git暂存/提交。
