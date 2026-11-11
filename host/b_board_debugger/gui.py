"""Chinese Tk instrument panel. Every Tk operation runs on the main thread."""
from __future__ import annotations

import copy
import json
import tkinter as tk
import tkinter.font as tkfont
from tkinter import filedialog, ttk

from .config import (DEFAULT_CONFIG, ENCODER_GPIO_WHITELIST, RANGES, ConfigError,
                     effective_notes, import_config, validate_config, velocity_for)
from .controller import Controller
from .transport import list_ports

BG = '#0f172a'
SURFACE = '#152238'
FIELD = '#0b1424'
FG = '#f1f5f9'
MUTED = '#bac8dc'
BORDER = '#40516a'
TEAL = '#5eead4'
AMBER = '#fbbf6a'
RED = '#fda4af'


def note_name(note):
    return '静音' if note < 0 else f'{("C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B")[note % 12]}{note // 12 - 1} ({note})'


class ScrollPage(ttk.Frame):
    def __init__(self, parent, min_width=880):
        super().__init__(parent)
        self.min_width = min_width
        self.rowconfigure(0, weight=1)
        self.columnconfigure(0, weight=1)
        self.canvas = tk.Canvas(self, background=BG, highlightthickness=0, borderwidth=0)
        self.canvas.grid(row=0, column=0, sticky='nsew')
        vertical = ttk.Scrollbar(self, orient='vertical', command=self.canvas.yview)
        vertical.grid(row=0, column=1, sticky='ns')
        horizontal = ttk.Scrollbar(self, orient='horizontal', command=self.canvas.xview)
        horizontal.grid(row=1, column=0, sticky='ew')
        self.canvas.configure(yscrollcommand=vertical.set, xscrollcommand=horizontal.set)
        self.content = ttk.Frame(self.canvas, padding=(14, 14, 14, 20))
        self.window = self.canvas.create_window(0, 0, window=self.content, anchor='nw')
        self.content.bind('<Configure>', self._size_content)
        self.canvas.bind('<Configure>', self._size_canvas)

    def _size_content(self, _event=None):
        self.canvas.configure(scrollregion=self.canvas.bbox('all'))

    def _size_canvas(self, event):
        width = max(event.width, self.min_width, self.content.winfo_reqwidth())
        self.canvas.itemconfigure(self.window, width=width)


class DebuggerApp:
    def __init__(self, root, *, demo=False, controller=None):
        self.root = root
        self.controller = controller or Controller()
        self._closed = False
        self._loading = False
        self._form_version = -1
        self._state_seen = None
        self._events_seen = -1
        self._messages_seen = None
        self._after = None
        self.editor_error = ''
        self._invalid_text = False
        self._port_map = {}
        self._theme()
        root.title('B 板调试台 · 激光竖琴')
        window_width = min(1180, max(720, root.winfo_screenwidth() - 60))
        window_height = min(900, max(480, root.winfo_screenheight() - 80))
        root.geometry(f'{window_width}x{window_height}')
        root.minsize(720, 480)
        root.configure(background=BG)
        root.protocol('WM_DELETE_WINDOW', self.close)
        self.port_var = tk.StringVar()
        self.mode_var = tk.StringVar(value='离线 · UART 调试')
        self.status_var = tk.StringVar(value=self.controller.message)
        self.draft_var = tk.StringVar()
        self.validation_var = tk.StringVar()
        self.stats_var = tk.StringVar(value='等待遥测 · UART0 / CH343 · 115200 baud')
        self.links_var = tk.StringVar(value='USB2 保持 MIDI；本程序通过 USB1 / CH343 的 UART 调试')
        self.event_count_var = tk.StringVar()
        self.duration_var = tk.StringVar(value='100')
        self.preview_channel_var = tk.StringVar(value='1')
        self.preview_var = tk.StringVar()
        self.midi_channel_var = tk.StringVar()
        self.vars = {key: tk.StringVar() for key in RANGES}
        self.vars.update({key: tk.StringVar() for key in ('route', 'enc_edges', 'velocity_min',
                                                        'velocity_max', 'velocity_start', 'velocity_end')})
        self.note_vars = [tk.StringVar() for _ in range(12)]
        self.gain_vars = [tk.StringVar() for _ in range(12)]
        self.pin_vars = [[tk.StringVar() for _ in range(3)] for _ in range(4)]
        self.reverse_vars = [tk.IntVar() for _ in range(4)]
        self._build_header()
        self._build_actions()
        self.notebook = ttk.Notebook(root)
        self.notebook.pack(fill='both', expand=True, padx=16, pady=(4, 8))
        self.pages = [ScrollPage(self.notebook, min_width=w) for w in (1020, 820, 760, 900)]
        for page, title in zip(self.pages, ('12 路监测 / 音符校准', '力度曲线 / 演奏参数',
                                          '高级 / 编码器', '击弦 CSV / 命令日志')):
            self.notebook.add(page, text=title)
        self._build_channels(self.pages[0].content)
        self._build_music(self.pages[1].content)
        self._build_encoders(self.pages[2].content)
        self._build_events(self.pages[3].content)
        footer = ttk.Frame(root, padding=(18, 5, 18, 10))
        footer.pack(fill='x')
        ttk.Label(footer, textvariable=self.status_var, wraplength=1100).pack(fill='x')
        footer.bind('<Configure>', lambda e: footer.winfo_children()[0].configure(wraplength=max(650, e.width - 36)))
        for var in list(self.vars.values()) + self.note_vars + self.gain_vars:
            var.trace_add('write', self._form_changed)
        for row in self.pin_vars:
            for var in row:
                var.trace_add('write', self._form_changed)
        for var in self.reverse_vars:
            var.trace_add('write', self._form_changed)
        self.duration_var.trace_add('write', lambda *_: self._draw_curve())
        self.preview_channel_var.trace_add('write', lambda *_: self._draw_curve())
        self.notebook.bind('<<NotebookTabChanged>>', lambda _e: self._refresh_events(force=True))
        root.bind_all('<MouseWheel>', self._wheel, add='+')
        root.bind_all('<Button-4>', self._wheel, add='+')
        root.bind_all('<Button-5>', self._wheel, add='+')
        root.bind_all('<FocusIn>', self._focus_visible, add='+')
        root.bind('<Control-Return>', lambda _e: self.on_apply())
        self._load_form()
        self._tick()
        if demo:
            root.after(0, self.start_demo)

    def _theme(self):
        available = set(tkfont.families(self.root))
        family = 'Noto Sans CJK SC' if 'Noto Sans CJK SC' in available else 'TkDefaultFont'
        if family == 'TkDefaultFont':
            family = tkfont.nametofont('TkDefaultFont').actual('family')
        self.font = (family, 10)
        self.small_font = (family, 9)
        self.heading_font = (family, 17, 'bold')
        self.mono_font = ('DejaVu Sans Mono', 10)
        tkfont.nametofont('TkDefaultFont').configure(family=family, size=10)
        self.root.option_add('*TCombobox*Listbox.background', FIELD)
        self.root.option_add('*TCombobox*Listbox.foreground', FG)
        style = ttk.Style(self.root)
        style.theme_use('clam')
        style.configure('.', background=BG, foreground=FG, font=self.font,
                        bordercolor=BORDER, lightcolor=BORDER, darkcolor=BORDER,
                        focuscolor=TEAL)
        style.configure('TFrame', background=BG)
        style.configure('Card.TFrame', background=SURFACE)
        style.configure('TLabel', background=BG, foreground=FG)
        style.configure('Muted.TLabel', foreground=MUTED)
        style.configure('Accent.TLabel', foreground=TEAL)
        style.configure('Warn.TLabel', foreground=AMBER)
        style.configure('Error.TLabel', foreground=RED)
        style.configure('Title.TLabel', font=self.heading_font)
        style.configure('TButton', background=BORDER, foreground=FG, padding=(10, 6), borderwidth=1)
        style.map('TButton', background=[('active', '#526781'), ('disabled', '#26354a')],
                  foreground=[('disabled', '#93a1b6')], bordercolor=[('focus', TEAL)])
        for name, color in (('Accent', TEAL), ('Save', AMBER), ('Danger', RED)):
            style.configure(f'{name}.TButton', background=color, foreground=FIELD)
            style.map(f'{name}.TButton', background=[('active', FG), ('disabled', '#334155')],
                      foreground=[('disabled', '#93a1b6')])
        style.configure('Small.TButton', padding=(6, 2), font=self.small_font)
        style.configure('Compact.TSpinbox', padding=(3, 1), font=self.small_font)
        for widget in ('TEntry', 'TSpinbox', 'TCombobox'):
            style.configure(widget, fieldbackground=FIELD, background=BORDER, foreground=FG,
                            insertcolor=FG, padding=4, arrowsize=14)
            style.map(widget, fieldbackground=[('readonly', FIELD)], foreground=[('readonly', FG)],
                      bordercolor=[('focus', TEAL)])
        style.configure('TCheckbutton', background=BG, foreground=FG, padding=(6, 5))
        style.map('TCheckbutton', background=[('active', SURFACE)])
        style.configure('TNotebook', background=BG, borderwidth=0)
        style.configure('TNotebook.Tab', padding=(14, 8), background=SURFACE, foreground=MUTED)
        style.map('TNotebook.Tab', background=[('selected', BORDER)], foreground=[('selected', FG)])
        style.configure('Treeview', background=FIELD, fieldbackground=FIELD, foreground=FG,
                        rowheight=28, borderwidth=0, font=self.small_font)
        style.configure('Treeview.Heading', background=BORDER, foreground=FG, padding=(5, 6))
        style.map('Treeview', background=[('selected', '#27645f')])
        style.configure('TLabelframe', bordercolor=BORDER, background=BG)
        style.configure('TLabelframe.Label', foreground=TEAL, background=BG)

    def _build_header(self):
        header = ttk.Frame(self.root, padding=(18, 12, 18, 6))
        header.pack(fill='x')
        header.columnconfigure(0, weight=1)
        ttk.Label(header, text='B 板 / 激光竖琴调试台', style='Title.TLabel').grid(row=0, column=0, sticky='w')
        ttk.Label(header, textvariable=self.mode_var, style='Accent.TLabel').grid(row=0, column=1, sticky='e')
        bar = ttk.Frame(header)
        bar.grid(row=1, column=0, columnspan=2, sticky='ew', pady=(8, 0))
        bar.columnconfigure(1, weight=1)
        ttk.Label(bar, text='UART 串口').grid(row=0, column=0, padx=(0, 8))
        self.port_box = ttk.Combobox(bar, textvariable=self.port_var, state='readonly', width=33)
        self.port_box.grid(row=0, column=1, sticky='ew', padx=(0, 8))
        ttk.Button(bar, text='刷新', command=self.refresh_ports).grid(row=0, column=2, padx=3)
        self.connect_button = ttk.Button(bar, text='连接', command=self.connect_selected, style='Accent.TButton')
        self.connect_button.grid(row=0, column=3, padx=3)
        self.disconnect_button = ttk.Button(bar, text='断开', command=self.disconnect)
        self.disconnect_button.grid(row=0, column=4, padx=3)
        self.demo_button = ttk.Button(bar, text='离线演示', command=self.start_demo)
        self.demo_button.grid(row=0, column=5, padx=(3, 0))

    def _build_actions(self):
        actions = ttk.Frame(self.root, padding=(18, 4, 18, 4))
        actions.pack(fill='x')
        actions.columnconfigure(0, weight=1)
        ttk.Label(actions, textvariable=self.draft_var, style='Warn.TLabel').grid(row=0, column=0, columnspan=4, sticky='w')
        self.apply_button = ttk.Button(actions, text='应用草稿  Ctrl+Enter', command=self.on_apply, style='Accent.TButton')
        self.apply_button.grid(row=1, column=0, sticky='ew', padx=(0, 6), pady=(6, 0))
        self.save_button = ttk.Button(actions, text='暂停演奏并保存', command=self.on_save, style='Save.TButton')
        self.save_button.grid(row=1, column=1, sticky='ew', padx=3, pady=(6, 0))
        ttk.Button(actions, text='读取设备配置', command=self.controller.read_config).grid(row=1, column=2, sticky='ew', padx=3, pady=(6, 0))
        self.panic_button = ttk.Button(actions, text='Panic / 清音', command=self.controller.panic, style='Danger.TButton')
        self.panic_button.grid(row=1, column=3, sticky='ew', padx=(6, 0), pady=(6, 0))
        second = ttk.Frame(actions)
        second.grid(row=2, column=0, columnspan=4, sticky='w', pady=(6, 0))
        ttk.Button(second, text='还原草稿至设备', command=self.discard_draft, style='Small.TButton').pack(side='left', padx=(0, 6))
        ttk.Button(second, text='保留编辑项，以新版本为基准', command=self.rebase_draft, style='Small.TButton').pack(side='left', padx=3)
        ttk.Button(second, text='导入 JSON → 草稿', command=self.import_dialog, style='Small.TButton').pack(side='left', padx=3)
        ttk.Button(second, text='导出草稿 JSON', command=self.export_dialog, style='Small.TButton').pack(side='left', padx=3)
        ttk.Label(actions, textvariable=self.validation_var, style='Error.TLabel', wraplength=1040).grid(row=3, column=0, columnspan=4, sticky='w', pady=(3, 0))

    def _build_channels(self, parent):
        parent.columnconfigure(0, weight=1)
        ttk.Label(parent, text='数字 OUT → 完整遮光结束后发声', style='Accent.TLabel').grid(row=0, column=0, sticky='w')
        ttk.Label(parent, text='PR1..PR12 是板上模拟电位器。先调至受光低 / 遮光高稳定；上位机只读数字 OUT，不能测量或改变参考电压。',
                  style='Muted.TLabel', wraplength=1020).grid(row=1, column=0, sticky='w', pady=(4, 10))
        table = ttk.Frame(parent)
        table.grid(row=2, column=0, sticky='ew')
        headings = ('路', '原始 OUT', '遮光', '发声', '最近 ms', 'velocity', 'hits', 'MIDI note', 'gain %', '有效音高：设备 → 草稿', '试奏')
        for col, title in enumerate(headings):
            ttk.Label(table, text=title, style='Muted.TLabel').grid(row=0, column=col, padx=6, pady=(0, 6))
            table.columnconfigure(col, weight=1 if col in (4, 6, 9) else 0)
        self.channel_labels = []
        self.effective_labels = []
        self.strike_buttons = []
        for index in range(12):
            row = index + 1
            ttk.Label(table, text=f'{row:02d}', font=self.mono_font).grid(row=row, column=0, padx=4, pady=1)
            labels = []
            for col in range(1, 7):
                label = ttk.Label(table, text='—', anchor='center', font=self.mono_font)
                label.grid(row=row, column=col, sticky='ew', padx=4, pady=1)
                labels.append(label)
            self.channel_labels.append(labels)
            ttk.Spinbox(table, from_=0, to=127, width=5, textvariable=self.note_vars[index], style='Compact.TSpinbox').grid(row=row, column=7, padx=5, pady=1)
            ttk.Spinbox(table, from_=25, to=200, width=5, textvariable=self.gain_vars[index], style='Compact.TSpinbox').grid(row=row, column=8, padx=5, pady=1)
            label = ttk.Label(table, text='—', anchor='center', font=self.small_font)
            label.grid(row=row, column=9, sticky='ew', padx=5, pady=1)
            self.effective_labels.append(label)
            button = ttk.Button(table, text=f'试奏 {row}', style='Small.TButton', command=lambda i=row: self.on_strike(i))
            button.grid(row=row, column=10, padx=5, pady=1)
            self.strike_buttons.append(button)
        ttk.Label(parent, text='MIDI 60 = C4；note 必须互异。移调后越界的通道显示“静音”，不夹紧到端点。试奏使用设备当前已应用映射。',
                  style='Muted.TLabel', wraplength=1000).grid(row=3, column=0, sticky='w', pady=(12, 4))
        trial = ttk.Frame(parent)
        trial.grid(row=4, column=0, sticky='w', pady=(4, 12))
        ttk.Label(trial, text='软件试奏遮光时长 ms').pack(side='left', padx=(0, 8))
        ttk.Spinbox(trial, from_=1, to=60000, width=8, textvariable=self.duration_var).pack(side='left')
        ttk.Label(trial, text='　事件 synthetic=1；不会保存 Flash', style='Muted.TLabel').pack(side='left')
        stats = ttk.LabelFrame(parent, text='采样调度 / 链路 / 丢弃统计', padding=12)
        stats.grid(row=5, column=0, sticky='ew')
        ttk.Label(stats, textvariable=self.stats_var, wraplength=990, font=self.mono_font).pack(fill='x')
        ttk.Label(stats, textvariable=self.links_var, wraplength=990, style='Muted.TLabel').pack(fill='x', pady=(8, 0))

    def _field(self, parent, row, col, label, key, low=None, high=None, width=9):
        ttk.Label(parent, text=label).grid(row=row, column=col, sticky='w', padx=(0, 12), pady=6)
        if key == 'route':
            widget = ttk.Combobox(parent, textvariable=self.vars[key], values=('auto', 'usb', 'ble', 'both'), state='readonly', width=width)
        elif key in ('polarity', 'enc_edges'):
            widget = ttk.Combobox(parent, textvariable=self.vars[key], values=('0', '1') if key == 'polarity' else ('2', '4'), state='readonly', width=width)
        else:
            if low is None:
                low, high = (0, 15) if key == 'channel' else RANGES[key]
            widget = ttk.Spinbox(parent, textvariable=self.vars[key], from_=low, to=high, width=width)
        widget.grid(row=row, column=col + 1, sticky='w', padx=(0, 24), pady=6)

    def _build_music(self, parent):
        parent.columnconfigure(0, weight=1)
        ttk.Label(parent, text='遮光时长 → 单次 Note On 力度', style='Accent.TLabel').grid(row=0, column=0, sticky='w')
        ttk.Label(parent, text='时间范围是力度映射的输入区间，单位 ms；力度端点可倒向。gain 在全局曲线后作用。CC7 是独立的通道音量。',
                  wraplength=900, style='Muted.TLabel').grid(row=1, column=0, sticky='w', pady=(4, 12))
        curve_fields = ttk.Frame(parent)
        curve_fields.grid(row=2, column=0, sticky='ew')
        self._field(curve_fields, 0, 0, '时间范围 min ms', 'velocity_min', 1, 59999)
        self._field(curve_fields, 0, 2, '时间范围 max ms', 'velocity_max', 2, 60000)
        self._field(curve_fields, 1, 0, '起点 velocity', 'velocity_start', 1, 127)
        self._field(curve_fields, 1, 2, '终点 velocity', 'velocity_end', 1, 127)
        self.curve_canvas = tk.Canvas(parent, background=FIELD, height=230, highlightbackground=BORDER, highlightthickness=1)
        self.curve_canvas.grid(row=3, column=0, sticky='ew', pady=(8, 0))
        self.curve_canvas.bind('<Configure>', lambda _e: self._draw_curve())
        preview = ttk.Frame(parent)
        preview.grid(row=4, column=0, sticky='w', pady=(8, 12))
        ttk.Label(preview, text='预览路号').pack(side='left')
        ttk.Spinbox(preview, from_=1, to=12, width=4, textvariable=self.preview_channel_var).pack(side='left', padx=8)
        ttk.Label(preview, textvariable=self.preview_var, style='Accent.TLabel').pack(side='left')
        parameters = ttk.LabelFrame(parent, text='演奏参数草稿 · 点击顶部应用后才生效', padding=12)
        parameters.grid(row=5, column=0, sticky='ew')
        fields = [('MIDI 通道编号 (0–15)', 'channel'), ('CC7 通道音量', 'volume'), ('移调 半音', 'transpose'),
                  ('微调 cents', 'fine'), ('gate ms', 'gate'), ('route', 'route'),
                  ('CC74 cutoff', 'cutoff'), ('CC71 resonance', 'resonance')]
        for index, (label, key) in enumerate(fields):
            self._field(parameters, index // 2, (index % 2) * 2, label, key)
        ttk.Label(parameters, textvariable=self.midi_channel_var, style='Muted.TLabel',
                  wraplength=850).grid(row=4, column=0, columnspan=4, sticky='w', pady=(6, 0))
        ttk.Label(parent, text='微调按接收端 ±2 半音的 Pitch Bend 范围换算。保存只保存已应用配置，并会让设备暂停演奏、清音及重新武装。',
                  style='Muted.TLabel', wraplength=900).grid(row=6, column=0, sticky='w', pady=(12, 0))

    def _build_encoders(self, parent):
        ttk.Label(parent, text='数字输入 / EC11 绑定', style='Accent.TLabel').grid(row=0, column=0, sticky='w')
        ttk.Label(parent, text='编码器 MCU GPIO 不按排针位置猜测。每组填完整 A/B/SW；-1/-1/-1 为禁用。修改 pins / polarity / MIDI channel 会先维护清音。',
                  wraplength=900, style='Muted.TLabel').grid(row=1, column=0, sticky='w', pady=(4, 12))
        inputs = ttk.LabelFrame(parent, text='比较器数字 OUT', padding=12)
        inputs.grid(row=2, column=0, sticky='ew')
        self._field(inputs, 0, 0, '遮光有效电平 0低 / 1高', 'polarity')
        self._field(inputs, 1, 0, 'debounce ms', 'debounce')
        ttk.Label(inputs, text='原电路默认：受光低、遮光高；PR1..PR12 的模拟门限在板上调节。',
                  style='Muted.TLabel').grid(row=2, column=0, columnspan=2, sticky='w', pady=(8, 0))
        bindings = ttk.LabelFrame(parent, text='4 × 3 GPIO 完整绑定', padding=12)
        bindings.grid(row=3, column=0, sticky='ew', pady=14)
        for col, label in enumerate(('编码器', 'A GPIO', 'B GPIO', 'SW GPIO', '反向')):
            ttk.Label(bindings, text=label, style='Muted.TLabel').grid(row=0, column=col, padx=14, pady=6)
        for index in range(4):
            ttk.Label(bindings, text=f'ENC{index + 1}').grid(row=index + 1, column=0, padx=14, pady=6)
            for col in range(3):
                ttk.Spinbox(bindings, textvariable=self.pin_vars[index][col], from_=-1, to=48, width=7).grid(row=index + 1, column=col + 1, padx=14, pady=6)
            ttk.Checkbutton(bindings, text='反向', variable=self.reverse_vars[index]).grid(row=index + 1, column=4, padx=14, pady=6)
        self._field(bindings, 5, 0, '每步 edges', 'enc_edges')
        ttk.Label(parent, text='B profile GPIO 白名单：' + ', '.join(map(str, sorted(ENCODER_GPIO_WHITELIST))) + '\n不得重复；激光、USB、UART、存储、启动脚和 RGB 已保留。',
                  style='Muted.TLabel', wraplength=900).grid(row=4, column=0, sticky='w')
        self.encoder_state_var = tk.StringVar(value='等待编码器遥测')
        ttk.Label(parent, textvariable=self.encoder_state_var, wraplength=900, font=self.mono_font).grid(row=5, column=0, sticky='w', pady=20)

    def _build_events(self, parent):
        parent.columnconfigure(0, weight=1)
        bar = ttk.Frame(parent)
        bar.grid(row=0, column=0, sticky='ew')
        ttk.Button(bar, text='导出当前击弦 CSV', command=self.export_csv_dialog).pack(side='left', padx=(0, 8))
        self.capture_button = ttk.Button(bar, text='开始逐次写 CSV', command=self.capture_dialog)
        self.capture_button.pack(side='left')
        ttk.Label(parent, textvariable=self.event_count_var, wraplength=1000, style='Muted.TLabel').grid(row=1, column=0, sticky='w', pady=10)
        event_frame = ttk.Frame(parent)
        event_frame.grid(row=2, column=0, sticky='ew')
        fields = ('session', 'seq', 'channel', 'duration_ms', 'velocity', 'note', 'synthetic', 'revision')
        self.event_tree = ttk.Treeview(event_frame, columns=fields, show='headings', height=10)
        widths = (80, 80, 55, 110, 80, 90, 90, 80)
        titles = ('会话', 'seq', '路号', 'duration ms', 'velocity', 'note', 'synthetic', 'revision')
        for key, width, title in zip(fields, widths, titles):
            self.event_tree.heading(key, text=title)
            self.event_tree.column(key, width=width, minwidth=45, anchor='center')
        self.event_tree.pack(side='left', fill='both', expand=True)
        scrollbar = ttk.Scrollbar(event_frame, orient='vertical', command=self.event_tree.yview)
        scrollbar.pack(side='right', fill='y')
        self.event_tree.configure(yscrollcommand=scrollbar.set)
        ttk.Label(parent, text='界面显示最近 250 次；内存保留最近 20000 次。逐次 CSV 从开始记录起保存每个唯一事件，含 session、mode、synthetic 与 revision。',
                  style='Muted.TLabel', wraplength=1000).grid(row=3, column=0, sticky='w', pady=10)
        ttk.Label(parent, text='命令 / 错误日志', style='Accent.TLabel').grid(row=4, column=0, sticky='w', pady=(12, 6))
        self.log_text = tk.Text(parent, height=8, background=FIELD, foreground=MUTED, insertbackground=FG,
                                font=self.small_font, wrap='word', relief='flat', state='disabled', padx=10, pady=8)
        self.log_text.grid(row=5, column=0, sticky='ew')

    def _load_form(self):
        self._loading = True
        config = self.controller.draft
        try:
            for key, var in self.vars.items():
                if key.startswith('velocity_'):
                    index = ('velocity_min', 'velocity_max', 'velocity_start', 'velocity_end').index(key)
                    var.set(str(config['velocity'][index]))
                elif key == 'channel':
                    var.set(str(config[key] - 1))
                else:
                    var.set(str(config[key]))
            for index in range(12):
                self.note_vars[index].set(str(config['notes'][index]))
                self.gain_vars[index].set(str(config['gains'][index]))
            for index in range(4):
                for col in range(3):
                    self.pin_vars[index][col].set(str(config['pins'][index][col]))
                self.reverse_vars[index].set((config['enc_reverse'] >> index) & 1)
        finally:
            self._loading = False
        self.editor_error = ''
        self._invalid_text = False
        self._form_version = self.controller.draft_version
        self._draw_curve()

    def collect_draft(self):
        proposed = copy.deepcopy(self.controller.draft)
        try:
            for key in RANGES:
                proposed[key] = int(self.vars[key].get())
            if not 0 <= proposed['channel'] <= 15:
                raise ValueError('MIDI 通道编号必须为 0..15')
            proposed['channel'] += 1
            proposed['route'] = self.vars['route'].get()
            proposed['enc_edges'] = int(self.vars['enc_edges'].get())
            proposed['velocity'] = [int(self.vars[key].get()) for key in ('velocity_min', 'velocity_max', 'velocity_start', 'velocity_end')]
            proposed['notes'] = [int(var.get()) for var in self.note_vars]
            proposed['gains'] = [int(var.get()) for var in self.gain_vars]
            proposed['pins'] = [[int(var.get()) for var in row] for row in self.pin_vars]
            proposed['enc_reverse'] = sum(int(var.get()) << index for index, var in enumerate(self.reverse_vars))
            return validate_config(proposed)
        except (ValueError, tk.TclError) as exc:
            raise ConfigError(f'草稿无效：{exc}') from exc

    def _form_changed(self, *_args):
        if self._loading:
            return
        try:
            proposed = self.collect_draft()
            self.controller.edit(proposed)
            self._form_version = self.controller.draft_version
            self.editor_error = ''
            self._invalid_text = False
        except ConfigError as exc:
            self.editor_error = str(exc)
            self._invalid_text = True
            self.controller.hold_draft()
        self._refresh_actions()
        self._refresh_effective()
        self._draw_curve()

    def _refresh_effective(self):
        try:
            draft = self.collect_draft()
        except ConfigError:
            return
        actual = self.controller.config
        applied = (f'已应用：报文编号 {actual["channel"] - 1}（常用通道 {actual["channel"]}）'
                   if actual else '已应用：未连接')
        self.midi_channel_var.set(f'{applied}；草稿：编号 {draft["channel"] - 1}（常用通道 {draft["channel"]}）')
        proposed = effective_notes(draft)
        applied = self.controller.state.get('notes')
        if not isinstance(applied, list) or len(applied) != 12:
            applied = effective_notes(self.controller.config) if self.controller.config else [-1] * 12
        for index, label in enumerate(self.effective_labels):
            actual = applied[index] if type(applied[index]) is int and -1 <= applied[index] <= 127 else -1
            before = note_name(actual) if self.controller.config else '未连接'
            label.configure(text=f'{before} → {note_name(proposed[index])}',
                            foreground=AMBER if proposed[index] == -1 else FG)

    def _draw_curve(self):
        if self._loading or not hasattr(self, 'curve_canvas'):
            return
        canvas = self.curve_canvas
        canvas.delete('all')
        width, height = max(500, canvas.winfo_width()), max(200, canvas.winfo_height())
        left, right, top, bottom = 60, width - 28, 24, height - 42
        try:
            config = self.collect_draft()
            channel = int(self.preview_channel_var.get()) - 1
            duration = int(self.duration_var.get())
            if not 0 <= channel < 12 or not 1 <= duration <= 60000:
                raise ValueError('preview')
        except (ValueError, ConfigError):
            canvas.create_text(26, 30, anchor='nw', text='修正草稿或试奏时长后显示曲线预览', fill=AMBER, font=self.font)
            self.preview_var.set('预览输入无效')
            return
        lo, hi, start, end = config['velocity']
        max_time = hi + max(1, hi // 8)
        x = lambda t: left + (right - left) * t / max_time
        y = lambda v: bottom - (bottom - top) * (v - 1) / 126
        for value in (1, 32, 64, 96, 127):
            canvas.create_line(left, y(value), right, y(value), fill=BORDER)
            canvas.create_text(left - 10, y(value), text=str(value), fill=MUTED, font=self.small_font, anchor='e')
        canvas.create_text(10, top, text='力度', fill=MUTED, font=self.small_font, anchor='nw')
        canvas.create_line(left, bottom, right, bottom, fill=MUTED)
        for time_value in (0, lo, hi):
            canvas.create_text(x(time_value), bottom + 17, text=str(time_value), fill=MUTED, font=self.small_font)
        canvas.create_text(right, bottom + 33, text='遮光时长 ms', fill=MUTED, font=self.small_font, anchor='e')
        canvas.create_line(x(0), y(start), x(lo), y(start), x(hi), y(end), x(max_time), y(end), fill=MUTED, width=2, dash=(5, 4))
        points = []
        for index in range(161):
            t = index * max_time // 160
            points.extend((x(t), y(velocity_for(config, t, channel))))
        canvas.create_line(*points, fill=TEAL, width=3)
        selected = velocity_for(config, duration, channel)
        px, py = x(min(max_time, duration)), y(selected)
        canvas.create_oval(px - 5, py - 5, px + 5, py + 5, fill=AMBER, outline=FIELD)
        canvas.create_text(right, top + 3, text='虚线：全局曲线   实线：该路 gain 后', fill=FG, font=self.small_font, anchor='ne')
        self.preview_var.set(f'草稿 CH{channel + 1} · {duration} ms → velocity {selected} · gain {config["gains"][channel]}% · {note_name(effective_notes(config)[channel])}')

    def _refresh_actions(self):
        c = self.controller
        revision = f'r{c.revision}' if c.revision is not None else '未读取'
        dirty = c.dirty or self._invalid_text
        if self.editor_error:
            flag = '草稿无效 · 尚未应用'
        elif c.writing:
            flag = '等待设备实际 ACK · 尚未确认'
        elif c.conflict:
            flag = '版本冲突 · 先读取，再还原或保留编辑项重新基准'
        elif c.uncertain:
            flag = '写入结果待回读 · 未重发'
        else:
            flag = '有草稿改动 · 尚未应用' if dirty else '草稿与最近已确认配置一致'
        self.draft_var.set(f'设备 {revision}　|　草稿基准 {c.draft_revision if c.draft_revision is not None else "离线"}　|　{flag}')
        self.validation_var.set(self.editor_error)
        self.apply_button.state(['!disabled'] if c.ready and dirty and not c.writing and not self.editor_error else ['disabled'])
        self.save_button.state(['!disabled'] if c.ready and not c.writing else ['disabled'])
        self.panic_button.state(['!disabled'] if c.connected else ['disabled'])
        self.connect_button.state(['disabled'] if c.connected or c.connecting else ['!disabled'])
        self.disconnect_button.state(['!disabled'] if c.connected or c.connecting else ['disabled'])
        for button in self.strike_buttons:
            button.state(['!disabled'] if c.ready else ['disabled'])
        stale = c.ready and c.last_state_at is not None and c.now() - c.last_state_at > 1.5
        mode = ('UART 已连接 · 遥测已过期' if stale else 'UART 已连接 · 115200 / 5 Hz') if c.ready else ('连接 / 握手中…' if c.connecting else '离线 · UART 调试')
        self.mode_var.set('演示模式 · 所有数据由软件生成' if c.demo and (c.connected or c.connecting) else mode)
        self.status_var.set(c.message)

    @staticmethod
    def _number(obj, key, fallback=0):
        value = obj.get(key, fallback) if isinstance(obj, dict) else fallback
        return value if type(value) is int else fallback

    def _refresh_state(self):
        c, state = self.controller, self.controller.state
        for index, labels in enumerate(self.channel_labels):
            raw, shade, playing = [bool(self._number(state, key) & (1 << index)) for key in ('raw', 'shade', 'playing')]
            values = [('1 高' if raw else '0 低', TEAL if raw else MUTED),
                      ('遮光' if shade else '光照', AMBER if shade else MUTED),
                      ('发声' if playing else '待机', TEAL if playing else MUTED)]
            for key in ('duration', 'velocity', 'hits'):
                array = state.get(key, [])
                value = array[index] if isinstance(array, list) and len(array) == 12 and type(array[index]) is int else '—'
                values.append((str(value), FG))
            for label, (text, color) in zip(labels, values):
                label.configure(text=text if state else '—', foreground=color)
        timing, drops, links = [state.get(key, {}) for key in ('timing', 'drops', 'links')]
        host = c.inbox.stats
        bad_frames = c.worker.bad_frames if c.worker else 0
        self.stats_var.set(f'采样 gap 最近/最大 {self._number(timing, "last_gap_us")} / {self._number(timing, "max_gap_us")} μs　'
                           f'工作最大 {self._number(timing, "max_work_us")} μs　错过周期 {self._number(timing, "missed_periods")}\n'
                           f'设备丢弃 cmd/reply/event {self._number(drops, "commands")} / {self._number(drops, "replies")} / {self._number(drops, "events")}　'
                           f'主机事件丢弃 {host["event_drops"]}　快照合并 {host["state_coalesced"]}　坏帧 {bad_frames}')
        selected = self._number(links, 'selected')
        online = links if isinstance(links, dict) else {}
        controls = state.get('controls', {})
        controls = controls if isinstance(controls, dict) else {}
        self.links_var.set(f'设备阶段 {state.get("lifecycle", "等待遥测")} · uptime {self._number(state, "uptime_ms")} ms · '
                           f'USB MIDI {"在线" if online.get("usb") else "离线"} / BLE {"在线" if online.get("ble") else "离线"} / targets {selected}\n'
                           f'发送 USB/BLE {self._number(links, "usb_sent")} / {self._number(links, "ble_sent")}　'
                           f'重试 USB/BLE {self._number(links, "usb_retries")} / {self._number(links, "ble_retries")}　BLE错误 {self._number(links, "ble_errors")}\n'
                           f'设备实时：移调 {self._number(controls, "transpose")}　CC7 {self._number(controls, "volume")}　fine {self._number(controls, "fine")} cents　'
                           f'CC74 {self._number(controls, "cutoff")}　CC71 {self._number(controls, "resonance")}')
        self.encoder_state_var.set(f'设备 encoder_enabled 0x{self._number(state, "encoder_enabled"):X}　'
                                   f'AB {state.get("ab", "—")}　pressed 0x{self._number(state, "pressed"):X}\n'
                                   f'illegal_edges {self._number(state, "illegal_edges")}　heap_free {self._number(state, "heap_free")} bytes')
        self._refresh_effective()

    def _refresh_events(self, force=False):
        if not hasattr(self, 'event_tree'):
            return
        events = self.controller.events
        self.event_count_var.set(f'已记录 {events.total} 次 · 重复忽略 {events.duplicates} · 无效事件 {events.invalid} · '
                                 f'内存淘汰 {events.evicted} · ' +
                                 (f'逐次 CSV 正在写入：{events.capture_path}' if events.capture_path else '逐次 CSV 未开始'))
        self.capture_button.configure(text='停止逐次 CSV' if events.capture_path else '开始逐次写 CSV')
        if self.notebook.index(self.notebook.select()) == 3 and (force or events.total != self._events_seen):
            fields = self.event_tree['columns']
            self.event_tree.delete(*self.event_tree.get_children())
            for row in list(events.rows)[-250:]:
                self.event_tree.insert('', 'end', values=[row[key] for key in fields])
            self.event_tree.yview_moveto(1)
            self._events_seen = events.total
        last_message = self.controller.messages[-1] if self.controller.messages else None
        if last_message != self._messages_seen:
            self.log_text.configure(state='normal')
            self.log_text.delete('1.0', 'end')
            self.log_text.insert('end', '\n'.join(f'{stamp}  {message}' for stamp, message in list(self.controller.messages)[-100:]))
            self.log_text.see('end')
            self.log_text.configure(state='disabled')
            self._messages_seen = last_message

    def _tick(self):
        if self._closed:
            return
        self.controller.poll()
        if self._form_version != self.controller.draft_version and not self._invalid_text:
            self._load_form()
        if self._state_seen is not self.controller.state:
            self._refresh_state()
            self._state_seen = self.controller.state
        self._refresh_actions()
        self._refresh_events()
        self._after = self.root.after(50, self._tick)

    def _safe(self, operation):
        try:
            return operation()
        except (ValueError, OSError, UnicodeError, tk.TclError) as exc:
            self.controller.notify(str(exc))
            self._refresh_actions()
            return None

    def refresh_ports(self):
        def action():
            ports = list_ports()
            self._port_map = {f'{path} · {description}': path for path, description in ports}
            self.port_box['values'] = list(self._port_map)
            if self.port_var.get() not in self._port_map:
                self.port_var.set('')
            self.controller.notify(f'找到 {len(ports)} 个串口；选择后点击连接，不自动打开')
        self._safe(action)

    def connect_selected(self):
        selected = self._port_map.get(self.port_var.get())
        if selected:
            self.controller.connect(selected)
        else:
            self.controller.notify('点击刷新，明确选择 UART / CH343 串口后连接')
        self._refresh_actions()

    def start_demo(self):
        self.controller.connect(demo=True)
        self._refresh_actions()

    def disconnect(self):
        self.controller.disconnect()
        self._refresh_actions()
        self._refresh_state()

    def on_apply(self):
        def action():
            self.controller.edit(self.collect_draft())
            self._form_version = self.controller.draft_version
            return self.controller.apply()
        self._safe(action)
        self._refresh_actions()

    def on_save(self):
        self.controller.save()
        self._refresh_actions()

    def on_strike(self, channel):
        self._safe(lambda: self.controller.strike(channel, int(self.duration_var.get())))

    def discard_draft(self):
        self.controller.discard_draft()
        self._load_form()
        self._refresh_actions()

    def rebase_draft(self):
        def action():
            self.controller.edit(self.collect_draft())
            self.controller.rebase_draft()
            self._load_form()
        self._safe(action)
        self._refresh_actions()

    def import_path(self, path):
        with open(path, 'rb') as source:
            text = source.read(65537).decode('utf-8-sig')
        self.controller.import_draft(import_config(text))
        self._load_form()
        self._refresh_actions()

    def export_path(self, path):
        config = self.collect_draft()
        with open(path, 'w', encoding='utf-8') as target:
            json.dump(config, target, ensure_ascii=False, indent=2)
            target.write('\n')
        self.controller.notify(f'已导出草稿：{path}')

    def import_dialog(self):
        path = filedialog.askopenfilename(parent=self.root, title='导入配置到草稿（不会自动应用）', filetypes=[('JSON 配置', '*.json')])
        if path:
            self._safe(lambda: self.import_path(path))

    def export_dialog(self):
        path = filedialog.asksaveasfilename(parent=self.root, title='导出草稿配置', initialfile='b-board-config.json', defaultextension='.json', filetypes=[('JSON 配置', '*.json')])
        if path:
            self._safe(lambda: self.export_path(path))

    def export_csv_path(self, path):
        with open(path, 'w', newline='', encoding='utf-8-sig') as target:
            self.controller.events.write_csv(target)
        self.controller.notify(f'已导出内存击弦记录：{path}')

    def export_csv_dialog(self):
        path = filedialog.asksaveasfilename(parent=self.root, title='导出已收到的唯一击弦事件', initialfile='b-board-events.csv', defaultextension='.csv', filetypes=[('CSV 事件', '*.csv')])
        if path:
            self._safe(lambda: self.export_csv_path(path))

    def capture_dialog(self):
        if self.controller.events.capture_path:
            self.controller.events.stop_capture()
            self.controller.notify('已停止逐次 CSV；文件已关闭')
        else:
            path = filedialog.asksaveasfilename(parent=self.root, title='开始逐次写 CSV（从此刻起）', initialfile='b-board-live-events.csv', defaultextension='.csv', filetypes=[('CSV 事件', '*.csv')])
            if path:
                self._safe(lambda: self.controller.events.start_capture(path))
        self._refresh_events(force=True)

    def _wheel(self, event):
        if self._closed or isinstance(event.widget, (ttk.Spinbox, ttk.Combobox, tk.Text, ttk.Treeview)):
            return
        page = self.pages[self.notebook.index(self.notebook.select())]
        delta = -1 if getattr(event, 'num', None) == 4 or getattr(event, 'delta', 0) > 0 else 1
        if getattr(event, 'state', 0) & 1:
            page.canvas.xview_scroll(delta * 3, 'units')
        else:
            page.canvas.yview_scroll(delta * 3, 'units')

    def _focus_visible(self, event):
        if self._closed or not hasattr(self, 'pages'):
            return
        page = self.pages[self.notebook.index(self.notebook.select())]
        if not str(event.widget).startswith(str(page.content) + '.'):
            return
        self.root.update_idletasks()
        top = event.widget.winfo_rooty() - page.content.winfo_rooty()
        bottom = top + event.widget.winfo_height()
        visible_top = page.canvas.canvasy(0)
        visible_bottom = visible_top + page.canvas.winfo_height()
        region = page.canvas.bbox('all')
        if region and region[3] > 0:
            if top < visible_top:
                page.canvas.yview_moveto(max(0, top - 10) / region[3])
            elif bottom > visible_bottom:
                page.canvas.yview_moveto(max(0, bottom - page.canvas.winfo_height() + 10) / region[3])

    def close(self):
        if self._closed:
            return
        self._closed = True
        if self._after:
            self.root.after_cancel(self._after)
        self.controller.disconnect(quiet=True)
        self.controller.events.stop_capture()
        self.root.destroy()
