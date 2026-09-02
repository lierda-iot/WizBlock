# Hardware

[中文](README.md) | [English](README.en.md)

Current public facts: ESP32-S3, L-AIWFS300 A0 core board, CH340E USB-UART, ST7789V3 320x240 display, CST836U single-touch path, ES8311 output, ES7210 input, and TDM slots MIC1/REF/MIC2/unused. Optional capabilities include BMI260, AIP8563, TF, SP0A39, the D0 motor board, and LTE/CAT1.

The public candidate excludes private schematics, internal vendor material, and unapproved assets. Current board mappings come from `CODE/components/laiwfs300/include/`; the historical `CODE/boards/laiwfs300` draft is excluded.

Power off before attaching modules. Use stable power, supervise motor Examples, confirm the target port/role before flashing, and remember that full erase removes stored configuration. Battery percentage is not a safety measurement until its divider and thresholds are calibrated.

