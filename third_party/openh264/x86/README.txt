OpenH264 v2.7.0 (Cisco, BSD-2-Clause) 의 x86 NASM 어셈 파일 (codec/common/x86, codec/decoder/core/x86).
X86MSX 8.0.126: ini Mp4Asm=1 일 때만 쓰임 (기본 0 = C 코드). 파일은 받은 그대로이고 이름만 바꿈:
  dec_dct.asm = decoder/core/x86/dct.asm, dec_intra_pred.asm = decoder/core/x86/intra_pred.asm (common/dct.asm 와 이름이 겹쳐서)
쓰지 않는 인코더용(satd_sad, vaa)은 뺌. 빌드: nasm -f elf64 -DUNIX64 (호스트) / nasm -f win64 -DWIN64 (UEFI), -I 이 폴더.
