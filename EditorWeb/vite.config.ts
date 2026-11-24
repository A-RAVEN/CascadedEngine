import { defineConfig } from 'vite';

// 面板由编辑器服务用 HttpListener 从 bin/editor-web/ 提供静态文件，所以资源引用必须是相对路径
//（base './'），不能假设挂在站点根目录。构建产物固定 dist/，build.py 会整目录拷到 bin/editor-web/。
//
// 多页面入口（editor-asset-browser 3.1）：右面板对象检查器 index.html + 底部资产浏览器 dock
// assets.html。两个页面各由自己的 WebView2 控制器加载，互不引用；入口写成字符串（相对 root），
// 不引入 node:path 的类型依赖。
//
// 第三个入口（editor-web-render-spike 1.2）：spike.html = 渲染原型页（全客户区单 WebView2
// 控制器加载，scene 面板 canvas 显示原生读回帧）。build.py 的产物校验也同步要求它存在。
export default defineConfig({
  base: './',
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    rollupOptions: {
      input: {
        index: 'index.html',
        assets: 'assets.html',
        spike: 'spike.html',
      },
    },
  },
});
