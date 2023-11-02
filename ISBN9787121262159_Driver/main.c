#include "ntddk.h"
#include "ntstrsafe.h"


#define CCP_MAX_COM_ID 32 // ����������ֻ��32�����ڣ��������ߵļ��裬��Ҳ��֪��ɶ��˼��������д
static PDEVICE_OBJECT s_fltobj[CCP_MAX_COM_ID] = { 0 };
static PDEVICE_OBJECT s_nextobj[CCP_MAX_COM_ID] = { 0 };


UNICODE_STRING com_name = RTL_CONSTANT_STRING(L"\\Device\\Serial0");

// ��������ǻ�ȡĳ�豸���豸����
PDEVICE_OBJECT ccpOpenCom(ULONG id, NTSTATUS* status) {
    UNICODE_STRING name_str;   // ����һ��UNICODE_STRING�ṹ���ڱ����豸����
    static WCHAR name[32] = { 0 };   // ����һ��32�ַ���WCHAR���鲢��ʼ��Ϊ0
    PFILE_OBJECT fileobj = NULL;   // ����һ���ļ�����ָ�벢��ʼ��ΪNULL
    PDEVICE_OBJECT devobj = NULL;   // ����һ���豸����ָ�벢��ʼ��ΪNULL

    //memset(name, 0, sizeof(WCHAR) * 32);   // �Ҿ��ö���

    // ʹ��RtlStringCchPrintfW�������豸���Ƹ�ʽ��Ϊ�ַ��������洢��name������
    RtlStringCchPrintfW(
        name, 32,
        L"\\Device\\Serial%d", id);

    RtlInitUnicodeString(&name_str, name);   // ��ʼ��UNICODE_STRING�ṹ��������name����

    // ʹ��IoGetDeviceObjectPointer������ȡ�豸����ָ����ļ�����ָ��
    *status = IoGetDeviceObjectPointer(
        &name_str,
        FILE_ALL_ACCESS,
        &fileobj, &devobj);

    if (*status == STATUS_SUCCESS) ObDereferenceObject(fileobj);   // ����ɹ���ȡ�豸���󣬼����ļ���������ü���

    return devobj;   // �����豸����ָ��
}

// ������������ǵ��������򴴽���һ���豸����һ��ʼ���ǿյģ�Ȼ�󸽼ӵ�COM1�豸���豸�����ϡ� �������󴴽����豸����й����豸���󣬾���Ϊ�˰���ʵ�豸�����õġ�
// Ȼ������豸����������Ū�ģ�Ȼ�����⹹�����������������豸����Ϣ������һ���м�㡣
NTSTATUS ccpAttachDevice(
    PDRIVER_OBJECT driver,
    PDEVICE_OBJECT oldobj,
    PDEVICE_OBJECT* fltobj,
    PDEVICE_OBJECT* next)
{
    NTSTATUS status;
    PDEVICE_OBJECT topdev = NULL;
    status = IoCreateDevice(
        driver,
        0,
        NULL,
        oldobj->Type,
        0,
        FALSE,
        fltobj);
    if (status != STATUS_SUCCESS) return status;
    if (oldobj->Flags & DO_BUFFERED_IO)
        (*fltobj)->Flags |= DO_BUFFERED_IO;
    if (oldobj->Flags & DO_DIRECT_IO)
        (*fltobj)->Flags |= DO_DIRECT_IO;
    // ������û�� Characteristics ��Ա������Ҳû�°ɣ�����
    // ��Ϊ̫���� https://www.nirsoft.net/kernel_struct/vista/DEVICE_OBJECT.html ������
    //if (oldobj->Characteristics & FILE_DEVICE_SECURE_OPEN) 
        //(*fltobj)->Characteristics |= FILE_DEVICE_SECURE_OPEN;
    (*fltobj)->Flags |= DO_POWER_PAGABLE;
    topdev = IoAttachDeviceToDeviceStack(*fltobj, oldobj);
    if (topdev == NULL) {
        IoDeleteDevice(*fltobj);
        *fltobj = NULL;
        status = STATUS_UNSUCCESSFUL;
        return status;
    }
    *next = topdev;
    (*fltobj)->Flags = (*fltobj)->Flags & ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

// ��ÿһ���豸���豸���󶼸���һ��
void ccpAttachAllComs(PDRIVER_OBJECT driver) {
    ULONG i;
    PDEVICE_OBJECT com_ob;
    NTSTATUS status;
    for (i = 0; i < CCP_MAX_COM_ID; i++)
    {
        com_ob = ccpOpenCom(i, &status);
        if (com_ob == NULL) continue;
        ccpAttachDevice(driver, com_ob, &s_fltobj[i], &s_nextobj[i]);
    }
}

NTSTATUS ccpDispatch(PDEVICE_OBJECT device, PIRP irp) {
	PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(irp); // ��ȡ��ǰ IRP ��ջλ����Ϣ��
	NTSTATUS status;
	ULONG i, j;
	for (i = 0; i < CCP_MAX_COM_ID; i++) {
        DbgPrint("(i = 0; i < CCP_MAX_COM_ID; i++\r\n");
        if (s_fltobj[i] == device) {
            DbgPrint("s_fltobj[i] == device\r\n");
            if (irpsp->MajorFunction == IRP_MJ_POWER) {
                PoStartNextPowerIrp(irp);
                IoSkipCurrentIrpStackLocation(irp);
                return PoCallDriver(s_nextobj[i], irp);
            }
            if (irpsp->MajorFunction == IRP_MJ_WRITE) {
                DbgPrint("IRP_MJ_WRITE\r\n");
                ULONG len = irpsp->Parameters.Write.Length;
                PUCHAR buf = NULL;
                if (irp->MdlAddress != NULL) {
                    buf = (PUCHAR)MmGetSystemAddressForMdlSafe(irp->MdlAddress,
                        NormalPagePriority);
                }
                else {
                    buf = (PUCHAR)irp->UserBuffer;
                }
                if (buf == NULL)
                    buf = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
                for (j = 0; j < len; ++j) {
                    DbgPrint("comcap: Send Data: %2x\r\n", buf[j]);
                }
            }
            IoSkipCurrentIrpStackLocation(irp);
            return IoCallDriver(s_nextobj[i], irp);
        }
	}
    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}
#define DELAY_ONE_MICROSECOND (-10)
#define DELAY_ONE_MILLISECOND (DELAY_ONE_MICROSECOND*100)
#define DELAY_ONE_SECOND (DELAY_ONE_MICROSECOND*1000)

void ccpUnload(PDRIVER_OBJECT drv)
{
    ULONG i;
    LARGE_INTEGER interval;

    for (i = 0; i < CCP_MAX_COM_ID; i++) {
        if (s_nextobj[i] != NULL) {
            IoDetachDevice(s_nextobj[i]);
        }
    }
    interval.QuadPart = (5 * 1000 * DELAY_ONE_MILLISECOND);
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
    for (i = 0; i < CCP_MAX_COM_ID; i++) {
        if (s_fltobj[i] != NULL) IoDeleteDevice(s_fltobj[i]);
    }
}

// 
// ��ڵ㣬��main����һ��
NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg_path){
    size_t i;

    // ѭ���������� IRP_MJ_MAXIMUM_FUNCTION ���͵� IRP ����
        //IRP_MJ_CREATE: ���ڴ����ļ����豸�Ĵ�������
        //IRP_MJ_CLOSE : �����ļ����豸�Ĺر�����
        //IRP_MJ_READ : ������ļ����豸��ȡ���ݵ�����
        //IRP_MJ_WRITE : �������ļ����豸д�����ݵ�����
        //IRP_MJ_DEVICE_CONTROL : ���ڴ����豸��������ͨ�����û�̬Ӧ�ó���ͨ�� IOCTL ���ô�����
        //IRP_MJ_INTERNAL_DEVICE_CONTROL : �����ڲ��豸��������ͨ�������������ڲ������������
        //IRP_MJ_POWER : �����Դ�������������豸��������ֹͣ�����ߡ�
        //IRP_MJ_SYSTEM_CONTROL : ����ϵͳ��������ͨ������֧�� WMI(Windows Management Instrumentation)��
        //IRP_MJ_PNP : �����弴��(Plugand Play) ��ص��������豸�İ�װ��ж�ء�
    for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) { // IRP_MJ_MAXIMUM_FUNCTION����Ϊ�˰����к�������ֵ
        // ��ӡ������Ϣ
        DbgPrint("DriverEntry: %d\r\n", (int)i);

        // Ϊÿ�� IRP �������ô����� ccpDispatch
        driver->MajorFunction[i] = ccpDispatch; // ������� i ��Ӧ IRP_MJ_WRITE ���ֺ궨�塣������˼�ǣ���ÿһ����������� ccpDispatch���������ֵ�Ļ�������Ҳ��û�к����ġ�
    }

    // ������������ж�غ���Ϊ ccpUnload
    driver->DriverUnload = ccpUnload;

    // ���� ccpAttachAllComs ���������ӵ����д���ͨ�Ŷ˿�
    ccpAttachAllComs(driver);

    // ���� STATUS_SUCCESS ��ʾ�������سɹ�
    return STATUS_SUCCESS;

}