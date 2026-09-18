#include <windows.h>
#include <wdf.h>
#include <vhf.h>

#define WININSPECT_STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#define WININSPECT_STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008L)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD WinInspectEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP WinInspectEvtDeviceCleanup;

typedef struct _DEVICE_CONTEXT {
    VHFHANDLE VhfHandle;
    WDFIOTARGET VhfIoTarget;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceContextGet);

static const UCHAR kKeyboardDescriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x06,       /* Usage (Keyboard) */
    0xA1, 0x01,       /* Collection (Application) */
    0x05, 0x07,       /* Usage Page (Keyboard) */
    0x19, 0xE0,       /* Usage Minimum (Left Control) */
    0x29, 0xE7,       /* Usage Maximum (Right GUI) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x08,       /* Report Count (8) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0x95, 0x01,       /* Report Count (1) */
    0x75, 0x08,       /* Report Size (8) */
    0x81, 0x01,       /* Input (Constant) */
    0x95, 0x05,       /* Report Count (5) */
    0x75, 0x01,       /* Report Size (1) */
    0x05, 0x08,       /* Usage Page (LEDs) */
    0x19, 0x01,       /* Usage Minimum (Num Lock) */
    0x29, 0x05,       /* Usage Maximum (Kana) */
    0x91, 0x02,       /* Output (Data, Variable, Absolute) */
    0x95, 0x01,       /* Report Count (1) */
    0x75, 0x03,       /* Report Size (3) */
    0x91, 0x01,       /* Output (Constant) */
    0x95, 0x06,       /* Report Count (6) */
    0x75, 0x08,       /* Report Size (8) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x73,       /* Logical Maximum (0x73) */
    0x05, 0x07,       /* Usage Page (Keyboard) */
    0x19, 0x00,       /* Usage Minimum (0) */
    0x29, 0x73,       /* Usage Maximum (0x73) */
    0x81, 0x00,       /* Input (Data, Array, Absolute) */
    0xC0              /* End Collection */
};

VOID WinInspectEvtDeviceCleanup(_In_ WDFOBJECT Object)
{
    PDEVICE_CONTEXT context = DeviceContextGet((WDFDEVICE)Object);

    if (context->VhfHandle != NULL) {
        VhfDelete(context->VhfHandle, TRUE);
        context->VhfHandle = NULL;
    }

    if (context->VhfIoTarget != NULL) {
        WdfIoTargetClose(context->VhfIoTarget);
        WdfObjectDelete(context->VhfIoTarget);
        context->VhfIoTarget = NULL;
    }
}

NTSTATUS WinInspectEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);

    NTSTATUS status;
    WDFDEVICE device = NULL;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    PDEVICE_CONTEXT context;
    WDF_OBJECT_ATTRIBUTES targetAttributes;
    WDF_IO_TARGET_OPEN_PARAMS openParams;
    HANDLE fileHandle;
    VHF_CONFIG vhfConfig;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);
    deviceAttributes.EvtCleanupCallback = WinInspectEvtDeviceCleanup;

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    context = DeviceContextGet(device);
    context->VhfHandle = NULL;
    context->VhfIoTarget = NULL;

    WDF_OBJECT_ATTRIBUTES_INIT(&targetAttributes);
    targetAttributes.ParentObject = device;

    status = WdfIoTargetCreate(device, &targetAttributes, &context->VhfIoTarget);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_FILE(&openParams, NULL);
    status = WdfIoTargetOpen(context->VhfIoTarget, &openParams);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    fileHandle = WdfIoTargetWdmGetTargetFileHandle(context->VhfIoTarget);
    if (fileHandle == NULL || fileHandle == INVALID_HANDLE_VALUE) {
        return WININSPECT_STATUS_INVALID_HANDLE;
    }

    VHF_CONFIG_INIT(&vhfConfig,
                    fileHandle,
                    (USHORT)sizeof(kKeyboardDescriptor),
                    (PUCHAR)kKeyboardDescriptor);
    vhfConfig.VendorID = 0x1209;
    vhfConfig.ProductID = 0xF382;
    vhfConfig.VersionNumber = 0x0001;

    status = VhfCreate(&vhfConfig, &context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = VhfStart(context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        VhfDelete(context->VhfHandle, TRUE);
        context->VhfHandle = NULL;
        return status;
    }

    return WININSPECT_STATUS_SUCCESS;
}

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    WDF_DRIVER_CONFIG_INIT(&config, WinInspectEvtDeviceAdd);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    return WdfDriverCreate(DriverObject, RegistryPath, &attributes, &config, WDF_NO_HANDLE);
}
