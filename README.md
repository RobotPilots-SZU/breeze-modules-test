# Breeze Modules Test

Breeze 模块层自动测试 App. 在 模块层开发中主要起这些作用:

- 提供west build目标, 从而能够跑configure等等步骤, 以建立开发环境
- 自动测试, 来验证功能正确性, 便于修复问题

## 约定

可以参考 `src/referee`

1. 每个模块单独在src下建立文件夹, 单独建立目标.
2. 目标务必使用`OBJECT`, 不然测试不会跑.
3. 每个目标独立声明依赖.
