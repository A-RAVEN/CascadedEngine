## 说明

纯 bug 修复，不引入新 capability，不修改已有 spec。

### 修复的 bug

1. ~~vertex input attribute lookup semantic-name map miss（VUID-07904）~~ ✅
2. ~~GPL library renderPass=NULL → 06055 + 02684~~ ✅
3. ~~command buffer 单例别名 → draw 被擦掉~~ ✅
4. ~~acquire semaphore 未 wait → 09600 + UNASSIGNED（第一帧）~~ ✅
5. ~~compute fence 死锁~~ ✅
6. ~~hash_combine 返回值丢弃 → cache 碰撞 → frame 2+ 渲染到错误 image~~ ✅
7. ~~frame 3 Aquire fence 死锁~~ ✅
8. present semaphore 复用（VUID-vkQueueSubmit-pSignalSemaphores-00067）：per-frame-context → per-swapchain-image
9. teardown 销毁顺序：framebuffer 在 window handle 之前销毁
