# libcorrect 本地改动

- 上游：`quiet/libcorrect`
- 固定提交：`ee82e6673a806dfdf0a969b975ab36596ecc5401`
- 本目录只保留 `RS(255,223)` 实现及其头文件依赖；Demo 通过 shortened 输入 `16` bytes 使用为 `RS(32,16)`。
- 未修改 Reed-Solomon 算法源码。新增的 `CMakeLists.txt` 仅用于 ESP-IDF 私有组件注册。
- libcorrect 的 shortened 编码实现会写入 32 bytes，但返回完整块长度 255；`main/ofdm_fec.c` 按实际写入长度和负值错误处理，不把该返回值当作输出字节数。
- libcorrect 解码器首次调用会建立查表并分配内存；`ofdm_fec_init()` 使用零码字预热，避免运行期首次收包发生分配。
