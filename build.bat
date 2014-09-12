make
copy blargSnes.elf blargSnes_s.elf
arm-none-eabi-strip blargSnes_s.elf
makerom -f cci -o blargSnes.3ds -rsf ./cci/gw_workaround.rsf -target d -exefslogo -elf blargSnes_s.elf -icon ./cci/icon.bin -banner ./cci/banner.bin
pause