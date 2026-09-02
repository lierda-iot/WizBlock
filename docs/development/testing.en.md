# Testing and Evidence

[中文](testing.md) | [English](testing.en.md)

Host tests prove pure logic only. A clean build proves firmware generation only. Flash/hash proves bytes were written. Boot logs prove observed initialization. Functional hardware testing proves only the exercised board, firmware, environment, and behavior. None of these layers silently replaces another.

Run the smallest affected host tests, refresh the complete mirror, clean-build the target Example, and only then perform explicitly authorized erase/flash/hardware checks. Record unexecuted items as unverified. Generated artifacts, raw logs, and device identifiers are not automatically public evidence.

