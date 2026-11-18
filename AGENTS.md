# Dtbtui

## 库

- ftxui 用于tui展示
- libftb 用于解析设备树

## 操作

- 编译: `xmake b`
- 测试: `xmake test -v` 或 `xmake b test && xmake r test`

## 约定

- 源文件后缀为 cpp, 头文件后缀为 hpp

## Note

- 添加源文件后, 运行 `xmake project -k compile_commands` 以生成编译命令
- Git: one line commit
