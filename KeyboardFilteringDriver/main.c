#include "ntddk.h"
#include "ntstrsafe.h"

extern POBJECT_TYPE IoDriverObjectType;
#define KBD_DRIVER_NAME  L"\\Driver\\Kbdclass"

typedef struct _C2P_DEV_EXT
{
	ULONG NodeSize;
	PDEVICE_OBJECT pFilterDeviceObject;
	KSPIN_LOCK IoRequestsSpinLock;
	KEVENT IoInProgressEvent;
	PDEVICE_OBJECT TargetDeviceObject;
	PDEVICE_OBJECT LowerDeviceObject;
}C2P_DEV_EXT, *PC2P_DEV_EXT;

// 本来就有，没公开，声明一下就能用
NTSTATUS
ObReferenceObjectByName(
	PUNICODE_STRING ObjectName,
	ULONG Attributes,
	PACCESS_STATE AccessState,
	ACCESS_MASK DesiredAccess,
	POBJECT_TYPE ObjectType,
	KPROCESSOR_MODE AccessMode,
	PVOID ParseContext,
	PVOID* Object
);

NTSTATUS
c2pDevExtInit(
	IN PC2P_DEV_EXT devExt,
	IN PDEVICE_OBJECT pFilterDeviceObject,
	IN PDEVICE_OBJECT pTargetDeviceObject,
	IN PDEVICE_OBJECT pLowerDeviceObject
)
{
	memset(devExt, 0, sizeof(C2P_DEV_EXT));
	devExt->NodeSize = sizeof(C2P_DEV_EXT);
	devExt->pFilterDeviceObject = pFilterDeviceObject;
	KeInitializeSpinLock(&(devExt->IoRequestsSpinLock));
	KeInitializeEvent(&(devExt->IoInProgressEvent), NotificationEvent, FALSE);
	devExt->TargetDeviceObject = pTargetDeviceObject;
	devExt->LowerDeviceObject = pLowerDeviceObject;
	return (STATUS_SUCCESS);
}


NTSTATUS
c2pAttachDevices(
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath
	)
{
	NTSTATUS status = 0;
	UNICODE_STRING uniNtNameString;
	PC2P_DEV_EXT devExt;
	PDEVICE_OBJECT pFilterDeviceObject = NULL;
	PDEVICE_OBJECT pTargetDeviceObject = NULL;
	PDEVICE_OBJECT pLowerDeviceObject = NULL;

	PDRIVER_OBJECT KbdDriverObject = NULL;

	KdPrint(("MyAttach\n"));

	RtlInitUnicodeString(&uniNtNameString, KBD_DRIVER_NAME);
	status = ObReferenceObjectByName(
		&uniNtNameString,
		OBJ_CASE_INSENSITIVE,
		NULL,
		0,
		IoDriverObjectType,
		KernelMode,
		NULL,
		&KbdDriverObject // 这是键盘驱动对象
	);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("MyAttach: Couldn't get the MyTest Device Object\n"));
		return (status);
	}
	else
	{
		ObDereferenceObject(DriverObject); // 减少引用次数
	}
	pTargetDeviceObject = KbdDriverObject->DeviceObject;
	// while 循环用于遍历键盘驱动对象的设备对象链表，
	while (pTargetDeviceObject) {
		status = IoCreateDevice(
			IN DriverObject,
			IN sizeof(C2P_DEV_EXT),
			IN NULL,
			IN pTargetDeviceObject->DeviceType,
			IN pTargetDeviceObject->Characteristics,
			IN FALSE,
			OUT & pFilterDeviceObject
			);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("MyAttach: Couldn't create the MyFilter Filter Device Object\n"));
			return (status);
		}
		pLowerDeviceObject = IoAttachDeviceToDeviceStack(pFilterDeviceObject, pTargetDeviceObject);
		if (!pLowerDeviceObject)
		{
			KdPrint(("MyAttach: Couldn't attach to MyFilter Filter Device Object\n"));
			IoDeleteDevice(pFilterDeviceObject);
			pFilterDeviceObject = NULL;
			return (status);
		}
		devExt = (PC2P_DEV_EXT)(pFilterDeviceObject->DeviceExtension);
		c2pDevExtInit(
			devExt,
			pFilterDeviceObject,
			pTargetDeviceObject,
			pLowerDeviceObject
		);
		pFilterDeviceObject->DeviceType = pLowerDeviceObject->DeviceType;
		pFilterDeviceObject->Characteristics = pLowerDeviceObject->Characteristics;
		pFilterDeviceObject->StackSize = pLowerDeviceObject->StackSize + 1;
		pFilterDeviceObject->Flags |= pLowerDeviceObject->Flags &(DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE);
		pTargetDeviceObject = pTargetDeviceObject->NextDevice;

	}
	return status;
}