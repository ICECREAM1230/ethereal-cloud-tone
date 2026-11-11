// Local-only bootstrap: this must remain independent of the main bundle.
(() => {
  let started = false;
  const text = (id, value) => {
    const node = document.getElementById(id);
    if (node) node.textContent = value;
  };
  text('connectionText', '界面资源加载中');
  const timer = setTimeout(() => {
    if (started) return;
    text('connectionText', '界面尚未启动');
    text('voiceTag', '界面尚未启动');
    text('engineStatus', '界面尚未启动，资源可能仍在加载，请稍候或重新加载页面。');
    text('feedback', '界面尚未启动，控制保持禁用。');
    for (const control of document.querySelectorAll('button,input,select')) control.disabled = true;
  }, 8000);
  globalThis.__synthStartup = {
    start() {
      started = true;
      clearTimeout(timer);
      text('connectionText', '连接设备');
    },
  };
})();
