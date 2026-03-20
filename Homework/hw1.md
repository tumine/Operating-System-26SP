## 1. 
### (1)
在 NexOS 中新增一个用户程序所需的三个必要步骤：
1. 在 `user/` 目录下编写该程序对应的 `.c` 源文件；
2. 在根目录 `Makefile` 的 `UPROGS` 中添加该程序名称；
3. 在根目录 `Makefile` 的 `fsimg` 规则中，将该程序打包进 `fs.img`。

### (2)
如果未向 `UPROGS` 中加入 `sid`，则 `sid.c` 不会被编译；如果没有更新 `fsimg` 规则，则编译得到的 `sid.elf` 不会被打包进 `fs.img`，从而无法在 NexOS Shell 中运行。
