#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# UEFI QEMU: USB 메모리(xHCI 저장 장치)로 부팅 + 추가 인자
rm -f /tmp/qm.sock
cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/vars.fd
setsid nohup qemu-system-x86_64 -cpu max -machine q35,i8042=off -display none -monitor unix:/tmp/qm.sock,server,nowait -m 512 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd -drive if=pflash,format=raw,file=/tmp/vars.fd \
  -device qemu-xhci,id=xhci -drive id=usbstick,if=none,format=raw,file=/home/claude/msxemu/build/msx1.img \
  -device usb-storage,bus=xhci.0,drive=usbstick,bootindex=0 -vga std "$@" > /tmp/qemu.log 2>&1 < /dev/null &
