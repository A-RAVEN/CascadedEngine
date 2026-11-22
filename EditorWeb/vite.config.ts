import { defineConfig } from 'vite';

// 面板由编辑器服务用 HttpListener 从 bin/editor-web/ 提供静态文件，所以资源引用必须是相对路径
//（base './'），不能假设挂在站点根目录。构建产物固定 dist/，build.py 会整目录拷到 bin/editor-web/。
export default defineConfig({
  base: './',
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
});
