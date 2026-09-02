# Local changes

Source: `espressif/gmf_io` 1.0.0 from `espressif/esp-gmf` commit
`3ae5408f2c900270c3c0dc6e4e84dc8760306ad3`.

This local component contains only the file IO implementation required by
`mp3_demo`. It adds `esp_gmf_io_file_set_lock_callback()` and wraps every
stdio/filesystem operation with that callback so MP3 streaming shares the
Demo's SPI2 mutex with LCD flushes. File IO behavior is otherwise preserved.

License: Espressif Modified MIT, see `LICENSE`.
