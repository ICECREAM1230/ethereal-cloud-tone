# 图稿离线QA

最终12图全部通过；manifest SHA256：846e00eebfc304086ea9b152b0c74c3c833f3190b6c077269296727a4305d964。

- 每个PDF均为1页，576×360pt（8×5英寸），12/12通过。
- 每个PNG均4800×3000像素、约600dpi，12/12通过。
- 字体全部嵌入并带Unicode映射，中文和轴标签可提取。
- pdfimages对12PDF均未发现image/mask/smask对象，图形为矢量。
- Poppler渲染12PDF，实际查看两张contact sheet；未见乱码、巨大刻度、重叠/遮挡图例、边界截字或空白图。
- KS16音请求和FM8音请求的时域图均完整展示三轮重复触发。
- 全部PDF/PNG SHA与作者manifest一致，源WAV/run/MIDI SHA核对通过，检查前后未变。
- 初始sample巨型字体/legend问题已在最终图修正；qa/sample*仅是过时问题证据，不是交付图。

本QA仅离线读取PDF/PNG和源数据；没有authoring marker、硬件、USB、串口、HTTP、共享锁或图稿修改。

完整记录：qa.json / final/qa.json；缩略图：final/contact-sheet-01-06.png、final/contact-sheet-07-12.png。
