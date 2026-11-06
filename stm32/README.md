# instructions

```
make
sudo dfu-util -a 0 -d 0483:df11 --dfuse-address 0x08000000 -D build/umb_airclick.bin
minicom -D /dev/ttyACM0
```
