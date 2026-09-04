#pragma once

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <Windows.h>
#include <winioctl.h>
#endif

#define UWU_DEVICE_NAME     L"\\Device\\uwudrv"
#define UWU_SYMLINK_NAME    L"\\DosDevices\\uwudrv"
#define UWU_USERMODE_PATH   L"\\\\.\\uwudrv"

#define UWU_IOCTL_ATTACH  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define UWU_IOCTL_READ    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define UWU_IOCTL_WRITE   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define UWU_IOCTL_DETACH  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define UWU_IOCTL_PING    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define UWU_MAX_IO 8192

#pragma pack(push, 1)
typedef struct { unsigned int pid; } UwuAttachReq;
typedef struct { unsigned long long address; unsigned int size; } UwuIoReq;
typedef struct { unsigned int magic; } UwuPingResp;
#pragma pack(pop)

#define UWU_PING_MAGIC 0x55575503u
