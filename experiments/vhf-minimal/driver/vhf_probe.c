#include <windows.h>
#include <wdf.h>
#include <vhf.h>

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD WinInspectVhfEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP WinInspectVhfEvtCleanup;

static const UCHAR g_KeyboardReportDescriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x06,       /* Usage (Keyboard) */
    0xA1, 0x01,       /* Collection (Application) */
    0x05, 0x07,       /*   Usage Page (Keyboard/Keypad) */
    0x19, 0xE0,       /*   Usage Minimum (Left Control) */
    0x29, 0xE7,       /*   Usage Maximum (Right GUI) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x75, 0x01,       /*   Report Size (1) */
    0x95, 0x08,       /*   Report Count (8) */
    0x81, 0x02,       /*   Input (Data,Var,Abs) */
    0x95, 0x01,       /*   Report Count (1) */
    0x75, 0x08,       /*   Report Size (8) */
    0x81, 0x01,       /*   Input (Const,Array,Abs) */
    0x95, 0x06,       /*   Report Count (6) */
    0x75, 0x08,       /*   Report Size (8) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x65,       /*   Logical Maximum (101) */
    0x05, 0x07,       /*   Usage Page (Keyboard/Keypad) */
    0x19, 0x00,       /*   Usage Minimum (0) */
    0x29, 0x65,       /*   Usage Maximum (101) */
    0x81, 0x00,       /*   Input (Data,Array,Abs) */
    0xC0              /* End Collection */
};

typedef struct _VHF_PROBE_CONTEXT {
    WDFIOTARGET IoTarget;
    VHFHANDLE VhfHandle;
} VHF_PROBE_CONTEXT, *PVHF_PROBE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VHF_PROBE_CONTEXT, WinInspectVhfGetContext);

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, WinInspectVhfEvtDeviceAdd);
    return WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE);
}

NTSTATUS
WinInspectVhfEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_TARGET_OPEN_PARAMS openParams;
    PVHF_PROBE_CONTEXT context;
    VHF_CONFIG vhfConfig;

    UNREFERENCED_PARAMETER(Driver);

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_UNKNOWN);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VHF_PROBE_CONTEXT);
    attributes.EvtCleanupCallback = WinInspectVhfEvtCleanup;

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    context = WinInspectVhfGetContext(device);
    context->IoTarget = NULL;
    context->VhfHandle = NULL;

    status = WdfIoTargetCreate(device, WDF_NO_OBJECT_ATTRIBUTES, &context->IoTarget);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_FILE(&openParams, NULL);
    status = WdfIoTargetOpen(context->IoTarget, &openParams);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    VHF_CONFIG_INIT(
        &vhfConfig,
        WdfIoTargetWdmGetTargetFileHandle(context->IoTarget),
        (USHORT)sizeof(g_KeyboardReportDescriptor),
        (PUCHAR)g_KeyboardReportDescriptor);
    vhfConfig.VendorID = 0x1209;
    vhfConfig.ProductID = 0x0266;
    vhfConfig.VersionNumber = 0x0001;

    status = VhfCreate(&vhfConfig, &context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        context->VhfHandle = NULL;
        return status;
    }

    status = VhfStart(context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        VhfDelete(context->VhfHandle, TRUE);
        context->VhfHandle = NULL;
        return status;
    }

    return STATUS_SUCCESS;
}

VOID
WinInspectVhfEvtCleanup(
    _In_ WDFOBJECT Object
    )
{
    PVHF_PROBE_CONTEXT context = WinInspectVhfGetContext((WDFDEVICE)Object);
    if (context->VhfHandle != NULL) {
        VhfDelete(context->VhfHandle, TRUE);
        context->VhfHandle = NULL;
    }
    if (context->IoTarget != NULL) {
        WdfIoTargetClose(context->IoTarget);
        context->IoTarget = NULL;
    }
}
