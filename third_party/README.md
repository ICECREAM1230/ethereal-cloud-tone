# Offline web dependencies

web_node_dependencies.tar.gz 保存当前配套网页已经安装的锁定依赖（含许可证），共406个文件/链接项，约5MB压缩。根 package.json/package-lock.json 保持来源原样。Node 本身仍为外部运行时。

在工程根目录运行 `python3 -B tools/restore_web_dependencies.py`，按本目录的 `web-dependencies.json` 在尚不存在的 `apps/web/node_modules` 中恢复并逐 SHA 校验；不下载、不执行 npm 安装脚本、不覆盖已有目录。然后在 `apps/web` 运行 `npm test` 或构建命令。也可按锁文件使用 `npm ci`。独立网页与固件内嵌网页的版本边界见 `../docs/VALIDATION.md`，依赖恢复不会改写页面。
