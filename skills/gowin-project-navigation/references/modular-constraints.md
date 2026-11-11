# 按模块管理约束与纯 Tcl 构建前合并

用于按模块整理 SDC/CST、手工注释模块约束，以及接入构建前合并的任务。沿用用户已确认的纯 Tcl 方案；合并器负责文本拼接，正式实现仍由 Gowin 完成。仅整理或合并约束不要求运行综合、布局布线或重新生成 IP。

## 目录与维护来源

路径相对于选定 `.gprj` 所在目录；文件名可随模块调整，保持 SDC、CST 分离：

```text
constraints/
├── split_manifest.tcl
├── timing/
│   ├── clocks.sdc
│   ├── fm.sdc
│   ├── ks.sdc
│   └── module_interfaces.sdc
├── physical/
│   ├── board_io.cst
│   └── placement.cst
└── generated/
    ├── top.sdc
    └── top.cst
scripts/
└── merge_constraints.tcl
```

手工维护 `timing/`、`physical/` 中的分片和 Tcl 清单，生成结果由脚本更新。SDC 使用 `#`、CST 使用 `//` 注释。分片写中文说明，交代所属模块、共享依赖、来源与启停状态。公共时钟、复位、模块接口和诊断报告单独归类；调整某音源时同时核对这些共享文件。

从已有约束拆分时，完整保留活动命令、已注释命令、诊断报告、原注释与顺序。原先停用的 HDMI、传感器、布局预案等条目继续保留原状态。注释或移除一个模块的 RTL，不等于可以自动删除其约束。

## 清单与合并器

复用工程已有的 `split_manifest.tcl` 和 `merge_constraints.tcl`。新建同类方案时使用纯 Tcl；当前参考实现需要 Tcl 8.6，不为文本合并引入 Python 或 JSON 解析依赖。清单显式决定顺序，避免目录通配扫描带入备份或临时文件。

当前清单格式返回字典，`timing`、`physical` 是按顺序排列的分片记录。例如：

```tcl
return {
    schema_version 1
    timing {
        {file timing/clocks.sdc}
        {file timing/fm.sdc}
        {file timing/ks.sdc}
        {file timing/module_interfaces.sdc}
    }
    physical {
        {file physical/board_io.cst}
        {file physical/placement.cst}
    }
}
```

`file` 相对于清单目录；当前实现使用两级路径。`description`、`source_lines`、`source_snapshot` 可记录来源，不决定启停。`return { ... }` 内是字典/列表数据，其中的 `#` 不是 Tcl 行注释；停用具体约束时在 SDC/CST 分片中注释，移除整个分片时修改清单列表，并检查共享依赖。

合并器应满足以下约定：

- 把 SDC/CST 当文本读取和拼接，保留中文、换行、命令及注释；使用适合各自格式的分片边界注释。仅执行 Tcl 清单，合并过程中不 `source` SDC/CST。
- 在写输出前检查两类全部输入，包括缺失文件、重复记录、文件类型与路径范围。输出固定在 `generated/`，保护原约束和工程文件，避免符号链接把写入重定向到其他位置。
- 先准备临时文件，再替换各输出；内容相同则保留输出和时间戳。任一步骤失败向调用者报错，由构建入口停止后续操作。
- 保持手工启停状态，不解释 RTL 的 `ENABLE_*` 参数，也不恢复已有 `DISABLED` 注释。若存在旧参数生成器，核对其开关和调用位置，避免旧流程覆盖分片维护结果或切回旧 SDC。

## 手动使用与构建接入

从工程根目录手动合并或只读检查：

```bash
tclsh scripts/merge_constraints.tcl
tclsh scripts/merge_constraints.tcl --check
```

当前参考实现的检查返回值：`0` 表示输出一致，`1` 表示需要合并，`2` 表示输入或调用错误。直接使用脚本绝对路径也应能从其他工作目录运行。

在构建 Tcl 中调用时，先解析实际工程目录。以下示例假定启动 Tcl 位于工程的 `scripts/`：

```tcl
set project_dir [file normalize [file join [file dirname [info script]] ..]]
source -encoding utf-8 [file join $project_dir scripts merge_constraints.tcl]
# 然后由原有流程冻结/复制构建输入、打开目标工程并执行已授权的阶段。
```

先合并，再冻结或复制本次构建输入，使快照包含本次实际使用的清单、分片和合并结果。每次启动 `syn`、单独 `pnr` 或 `all` 都要经过合并；仅放入综合前的 `TclPre` 不能覆盖独立布局布线。被 `source` 的合并器成功后继续宿主流程，失败时抛错，保留宿主的参数和生命周期；它自身不启动实现或烧录。

正式接入后，目标工程只启用 `generated/top.sdc` 与 `generated/top.cst`，每种约束各一份。Gowin 的同类型约束启用行为会让新增分片互相替换启用状态，因此分片作为合并源管理，不逐个加入工程。添加或切换时检查最终 `.gprj` 的实际 `enable` 项，以及当前工具版本的规则。

授权范围决定接入动作：只要求分类整理时，保留原 SDC/CST、模板、`.gprj` 与启动配置；已授权接入或切换时，更新目标工程的约束选择及入口调用，并验证它们确实读取合并输出。已有相应授权时直接执行，不另设重复确认。创建了分片和脚本，只说明整理完成，不能据此声称正式构建已经接入。

真正运行综合/PnR时，沿用项目已确认的 Gowin 启动、后台进程、日志和结果交接约定，参见 [项目与构建 Tcl](tcl-project-build.md)及对应主机指南。更新 skill 或演示合并命令本身不启动构建。

## 验证与交付

核对拆分后的原文覆盖和命令顺序，活动与已注释条目都要计入；只数当前活动命令不足以证明完整。确认原约束、模板和未授权修改的配置保持原样，并记录实际接入状态。

验证清单顺序、重复合并不改写、`--check` 只读、缺失输入时保留原输出，以及 `source` 的成功继续和失败停止行为。优先复用工程已有测试；当前参考实现的测试入口为：

```bash
tclsh scripts/tests/test_merge_constraints.tcl
```

纯 Tcl 文件行为验证可用本机 `tclsh`；更换 Gowin 安装版本时核对其 Tcl 兼容性。只整理文档或约束时无须启动厂商工具探测，避免工具刷新现有工程元数据。语法、文本等价和合并测试不代表综合网表对象命中或时序收敛；这些结论需要相应版本的正式实现报告。

## 本机参考工程

原记录引用的是来源机器上的 Tang Mega 138K 工程。先定位当前 `.gprj` 所在目录，再按需读取该工程的 `constraints/SPLIT_README.md`、清单和脚本；公开技能包不携带当时的绝对工作目录，也不证明当前工程仍有相同入口。

2026-10-25 核对时，分片与纯 Tcl 合并器已建立，主 `.gprj` 仍选择原 SDC/CST，原 `TclPre` 尚未换成合并入口。这是当时的接入记录；之后每次操作都以实际 `.gprj`、过程配置和启动脚本为准。
