/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * efi.h - 이 프로젝트에 필요한 최소 UEFI 정의 (UEFI Specification 2.x)
 * 함수 표는 규격의 필드 순서를 그대로 따른다. 호출 규약은 MS x64 (clang -target x86_64-windows 기본).
 */
#ifndef MSX_EFI_H
#define MSX_EFI_H
#include <stdint.h>
#include <stddef.h>

typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint16_t CHAR16;
typedef uint64_t UINTN;
typedef uint64_t EFI_PHYSICAL_ADDRESS;

#define EFI_SUCCESS            0
#define EFI_ERROR(s)           (((int64_t)(s)) < 0)
#define EFI_BUFFER_TOO_SMALL   (0x8000000000000005ull)

typedef struct { uint32_t a; uint16_t b, c; uint8_t d[8]; } EFI_GUID;

typedef struct {
    uint64_t Signature;
    uint32_t Revision, HeaderSize, CRC32, Reserved;
} EFI_TABLE_HEADER;

/* 텍스트 출력 (그래픽 전 진단용) */
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (*Reset)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, uint8_t);
    EFI_STATUS (*OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, const CHAR16 *);
    void *TestString, *QueryMode, *SetMode, *SetAttribute;
    EFI_STATUS (*ClearScreen)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *);
    void *SetCursorPosition, *EnableCursor, *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef enum { AllocateAnyPages, AllocateMaxAddress, AllocateAddress } EFI_ALLOCATE_TYPE;
#define EfiLoaderData 2

typedef struct {
    uint32_t Type, Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    uint64_t VirtualStart, NumberOfPages, Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    UINTN (*RaiseTPL)(UINTN);
    void (*RestoreTPL)(UINTN);
    EFI_STATUS (*AllocatePages)(EFI_ALLOCATE_TYPE, uint32_t, UINTN, EFI_PHYSICAL_ADDRESS *);
    EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS, UINTN);
    EFI_STATUS (*GetMemoryMap)(UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *, UINTN *, uint32_t *);
    EFI_STATUS (*AllocatePool)(uint32_t, UINTN, void **);
    EFI_STATUS (*FreePool)(void *);
    EFI_STATUS (*CreateEvent)(uint32_t Type, UINTN NotifyTpl, void (*Notify)(void *Event, void *Ctx), void *Ctx, void **Event);
    EFI_STATUS (*SetTimer)(void *Event, int Type, uint64_t TriggerTime);
    void *WaitForEvent, *SignalEvent, *CloseEvent, *CheckEvent;
    void *InstallProtocolInterface, *ReinstallProtocolInterface, *UninstallProtocolInterface;
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE, const EFI_GUID *, void **);
    void *Reserved;
    void *RegisterProtocolNotify, *LocateHandle, *LocateDevicePath, *InstallConfigurationTable;
    void *LoadImage, *StartImage, *Exit, *UnloadImage;
    EFI_STATUS (*ExitBootServices)(EFI_HANDLE, UINTN);
    void *GetNextMonotonicCount;
    EFI_STATUS (*Stall)(UINTN);
    EFI_STATUS (*SetWatchdogTimer)(UINTN, uint64_t, UINTN, const CHAR16 *);
    EFI_STATUS (*ConnectController)(EFI_HANDLE, EFI_HANDLE *, void *, uint8_t);
    EFI_STATUS (*DisconnectController)(EFI_HANDLE, EFI_HANDLE, EFI_HANDLE);
    void *OpenProtocol, *CloseProtocol, *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    EFI_STATUS (*LocateHandleBuffer)(int, const EFI_GUID *, void *, UINTN *, EFI_HANDLE **);
    EFI_STATUS (*LocateProtocol)(const EFI_GUID *, void *, void **);
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *GetTime, *SetTime, *GetWakeupTime, *SetWakeupTime;
    void *SetVirtualAddressMap, *ConvertPointer;
    void *GetVariable, *GetNextVariableName, *SetVariable;
    void *GetNextHighMonotonicCount;
    void (*ResetSystem)(int type, EFI_STATUS, UINTN, void *);
} EFI_RUNTIME_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    void *StdErr;
    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* 그래픽 출력 (GOP) */
typedef enum { PixelRedGreenBlueReserved8BitPerColor, PixelBlueGreenRedReserved8BitPerColor,
               PixelBitMask, PixelBltOnly } EFI_GRAPHICS_PIXEL_FORMAT;
typedef struct {
    uint32_t Version, HorizontalResolution, VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    uint32_t RedMask, GreenMask, BlueMask, ReservedMask;
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;
typedef struct {
    uint32_t MaxMode, Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (*QueryMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *, uint32_t, UINTN *, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
    EFI_STATUS (*SetMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *, uint32_t);
    EFI_STATUS (*Blt)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *, void *buf, uint32_t op, UINTN sx, UINTN sy, UINTN dx, UINTN dy,
                      UINTN w, UINTN h, UINTN delta);   /* op 2 = EfiBltBufferToVideo */
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* 로드된 이미지 (어느 장치에서 부팅했는지) */
typedef struct {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath, *Reserved;
    uint32_t LoadOptionsSize;
    void *LoadOptions, *ImageBase;
    uint64_t ImageSize;
    uint32_t ImageCodeType, ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* 파일 */
typedef struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (*Open)(struct EFI_FILE_PROTOCOL *, struct EFI_FILE_PROTOCOL **, const CHAR16 *, uint64_t, uint64_t);
    EFI_STATUS (*Close)(struct EFI_FILE_PROTOCOL *);
    EFI_STATUS (*Delete)(struct EFI_FILE_PROTOCOL *);  /* 닫기도 함 */
    EFI_STATUS (*Read)(struct EFI_FILE_PROTOCOL *, UINTN *, void *);
    EFI_STATUS (*Write)(struct EFI_FILE_PROTOCOL *, UINTN *, void *);
    void *GetPosition;
    EFI_STATUS (*SetPosition)(struct EFI_FILE_PROTOCOL *, uint64_t);
    EFI_STATUS (*GetInfo)(struct EFI_FILE_PROTOCOL *, const EFI_GUID *, UINTN *, void *);
    EFI_STATUS (*SetInfo)(struct EFI_FILE_PROTOCOL *, const EFI_GUID *, UINTN, void *);
    EFI_STATUS (*Flush)(struct EFI_FILE_PROTOCOL *);
} EFI_FILE_PROTOCOL;
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (*OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct {
    uint64_t Size, FileSize, PhysicalSize;
    uint8_t CreateTime[16], LastAccessTime[16], ModificationTime[16];
    uint64_t Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;
/* 볼륨 정보 (이름, 크기) */
typedef struct {
    uint64_t Size;
    uint8_t ReadOnly, Pad[7];
    uint64_t VolumeSize, FreeSpace;
    uint32_t BlockSize;
    CHAR16 VolumeLabel[1];
} EFI_FILE_SYSTEM_INFO;

/* USB I/O (펌웨어 USB 스택을 거쳐 장치와 직접 통신) */
#pragma pack(push, 1)
typedef struct { uint8_t RequestType, Request; uint16_t Value, Index, Length; } EFI_USB_DEVICE_REQUEST;
typedef struct { uint8_t Length, DescriptorType, InterfaceNumber, AlternateSetting, NumEndpoints,
                 InterfaceClass, InterfaceSubClass, InterfaceProtocol, Interface; } EFI_USB_INTERFACE_DESCRIPTOR;
typedef struct { uint8_t Length, DescriptorType, EndpointAddress, Attributes; uint16_t MaxPacketSize;
                 uint8_t Interval; } EFI_USB_ENDPOINT_DESCRIPTOR;
#pragma pack(pop)
#pragma pack(push, 1)
typedef struct {
    uint8_t Length, DescriptorType;
    uint16_t BcdUSB;
    uint8_t DeviceClass, DeviceSubClass, DeviceProtocol, MaxPacketSize0;
    uint16_t IdVendor, IdProduct, BcdDevice;
    uint8_t StrManufacturer, StrProduct, StrSerialNumber, NumConfigurations;
} EFI_USB_DEVICE_DESCRIPTOR;
#pragma pack(pop)
typedef EFI_STATUS (*EFI_ASYNC_USB_TRANSFER_CALLBACK)(void *Data, UINTN DataLength, void *Context, uint32_t Status);
typedef struct EFI_USB_IO_PROTOCOL {
    EFI_STATUS (*UsbControlTransfer)(struct EFI_USB_IO_PROTOCOL *, EFI_USB_DEVICE_REQUEST *, int Direction,
                                     uint32_t Timeout, void *Data, UINTN DataLength, uint32_t *Status);
    EFI_STATUS (*UsbBulkTransfer)(struct EFI_USB_IO_PROTOCOL *, uint8_t Endpoint, void *Data, UINTN *DataLength, UINTN Timeout, uint32_t *Status);   /* 3.4.4 */
    EFI_STATUS (*UsbAsyncInterruptTransfer)(struct EFI_USB_IO_PROTOCOL *, uint8_t Endpoint, uint8_t IsNewTransfer,
                                            UINTN PollingInterval, UINTN DataLength,
                                            EFI_ASYNC_USB_TRANSFER_CALLBACK Callback, void *Context);
    EFI_STATUS (*UsbSyncInterruptTransfer)(struct EFI_USB_IO_PROTOCOL *, uint8_t Endpoint, void *Data, UINTN *DataLength, UINTN Timeout, uint32_t *Status);
    void *UsbIsochronousTransfer, *UsbAsyncIsochronousTransfer;
    EFI_STATUS (*UsbGetDeviceDescriptor)(struct EFI_USB_IO_PROTOCOL *, EFI_USB_DEVICE_DESCRIPTOR *);
    void *UsbGetConfigDescriptor;
    EFI_STATUS (*UsbGetInterfaceDescriptor)(struct EFI_USB_IO_PROTOCOL *, EFI_USB_INTERFACE_DESCRIPTOR *);
    EFI_STATUS (*UsbGetEndpointDescriptor)(struct EFI_USB_IO_PROTOCOL *, uint8_t Index, EFI_USB_ENDPOINT_DESCRIPTOR *);
    void *UsbGetStringDescriptor, *UsbGetSupportedLanguages; void *UsbPortReset;
} EFI_USB_IO_PROTOCOL;
#define EfiUsbDataIn  0
#define EfiUsbDataOut 1
#define EfiUsbNoData  2

/* 블록 장치 (디스크 전체 또는 파티션) */
typedef struct {
    uint32_t MediaId;
    uint8_t RemovableMedia, MediaPresent, LogicalPartition, ReadOnly, WriteCaching, Pad[3];
    uint32_t BlockSize, IoAlign;
    uint32_t Pad2;
    uint64_t LastBlock;
} EFI_BLOCK_IO_MEDIA;
typedef struct EFI_BLOCK_IO_PROTOCOL {
    uint64_t Revision;
    EFI_BLOCK_IO_MEDIA *Media;
    void *Reset;
    EFI_STATUS (*ReadBlocks)(struct EFI_BLOCK_IO_PROTOCOL *, uint32_t, uint64_t, UINTN, void *);
    EFI_STATUS (*WriteBlocks)(struct EFI_BLOCK_IO_PROTOCOL *, uint32_t, uint64_t, UINTN, void *);
    EFI_STATUS (*FlushBlocks)(struct EFI_BLOCK_IO_PROTOCOL *);
} EFI_BLOCK_IO_PROTOCOL;

/* 장치 경로 노드 머리 */
typedef struct { uint8_t Type, SubType; uint8_t Length[2]; } EFI_DEVICE_PATH;

/* 파일 정보의 시각 (CreateTime 등) */
typedef struct { uint16_t Year; uint8_t Month, Day, Hour, Minute, Second, Pad1; uint32_t Nanosecond;
                 int16_t TimeZone; uint8_t Daylight, Pad2; } EFI_TIME;

#define AllHandles  0
#define ByProtocol  2
#define EFI_FILE_MODE_READ 1
#define EFI_FILE_MODE_WRITE 2
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL
#define EFI_FILE_DIRECTORY 0x10

static const EFI_GUID GOP_GUID = { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };
static const EFI_GUID LOADED_IMAGE_GUID = { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const EFI_GUID SIMPLE_FS_GUID = { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const EFI_GUID BLOCK_IO_GUID = { 0x964e5b21, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const EFI_GUID SIMPLE_POINTER_GUID = { 0x31878c87, 0x0b75, 0x11d5, { 0x9a, 0x4f, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };
/* 펌웨어 마우스 (Simple Pointer) */
typedef struct { int32_t RelativeMovementX, RelativeMovementY, RelativeMovementZ; uint8_t LeftButton, RightButton; } EFI_SIMPLE_POINTER_STATE;
typedef struct { uint64_t ResolutionX, ResolutionY, ResolutionZ; uint8_t LeftButton, RightButton; } EFI_SIMPLE_POINTER_MODE;
typedef struct EFI_SIMPLE_POINTER_PROTOCOL {
    EFI_STATUS (*Reset)(struct EFI_SIMPLE_POINTER_PROTOCOL *, uint8_t ExtendedVerification);
    EFI_STATUS (*GetState)(struct EFI_SIMPLE_POINTER_PROTOCOL *, EFI_SIMPLE_POINTER_STATE *);
    void *WaitForInput;
    EFI_SIMPLE_POINTER_MODE *Mode;
} EFI_SIMPLE_POINTER_PROTOCOL;
static const EFI_GUID USB_IO_GUID = { 0x2b2f68d6, 0x0cd2, 0x44cf, { 0x8e, 0x8b, 0xbb, 0xa2, 0x0b, 0x1b, 0x5b, 0x75 } };
static const EFI_GUID DEVICE_PATH_GUID = { 0x09576e91, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const EFI_GUID TEXT_IN_GUID = { 0x387477c1, 0x69c7, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const EFI_GUID FS_INFO_GUID = { 0x09576e93, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const EFI_GUID FILE_INFO_GUID = { 0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };

#endif
