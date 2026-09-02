# KISS FFT 本地改动

- 上游：`mborgerding/kissfft`
- 固定提交：`6398d8a1d0c92486b5ece8a456fd5e6a97ad1f08`
- 只复制复数 FFT 所需的 `kiss_fft.c`、`kiss_fft.h`、`_kiss_fft_guts.h` 和 `kiss_fft_log.h`，不引入 tools、real FFT、ND FFT 或测试资产。
- 未修改上游算法源码；只调整为 Demo 私有 ESP-IDF 组件目录，并固定 `kiss_fft_scalar=float`。
- `main/ofdm_phy.c` 在初始化阶段创建 256 点前向/逆向配置，热路径始终使用不同的输入/输出缓冲，因此不会触发 KISS FFT 的 in-place 临时分配。
