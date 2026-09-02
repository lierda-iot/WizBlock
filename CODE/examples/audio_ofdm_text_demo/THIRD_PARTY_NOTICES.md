# 第三方声明

## quiet/libcorrect

- 用途：Reed-Solomon 编码和解码。
- 固定提交：`ee82e6673a806dfdf0a969b975ab36596ecc5401`。
- 许可证：BSD-3-Clause，完整文本见 `components/libcorrect/LICENSE`。
- 上游路径：`https://github.com/quiet/libcorrect`。
- 复制内容：`include/correct.h`、`include/correct/portable.h`、`include/correct/reed-solomon.h`、`include/correct/reed-solomon/` 和 `src/reed-solomon/`。
- 本地改动：只增加 ESP-IDF 组件注册和 `ofdm_fec` 适配；算法源码未改动，具体说明见 `components/libcorrect/LOCAL_CHANGES.md`。

## mborgerding/kissfft

- 用途：256 点浮点复数 FFT/IFFT。
- 固定提交：`6398d8a1d0c92486b5ece8a456fd5e6a97ad1f08`。
- 许可证：BSD-3-Clause，版权声明见 `components/kissfft/COPYING`，完整条款见 `components/kissfft/LICENSES/BSD-3-Clause`。
- 上游路径：`https://github.com/mborgerding/kissfft`。
- 复制内容：`kiss_fft.c`、`kiss_fft.h`、`_kiss_fft_guts.h` 和 `kiss_fft_log.h`。
- 本地改动：只增加私有 ESP-IDF 组件注册并固定 float 类型；算法源码未改动，具体说明见 `components/kissfft/LOCAL_CHANGES.md`。

## Noto Sans SC

- 用途：接收中文正文和状态 UI。
- 字库：Noto Sans SC，转换为 Demo 私有的 16px、2bpp 二进制字库。
- 许可证：SIL Open Font License 1.1，完整文本见 `fonts/OFL.txt`。
- 本地改动：只执行字形光栅化和二进制打包；可复现生成器为 `fonts/generate_ofdm_font.py`。
