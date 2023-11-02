#include "ntddk.h"
#include "ntstrsafe.h"


#define CCP_MAX_COM_ID 32 // 计算机上最多只有32个串口，这是作者的假设，我也不知道啥意思，先这样写
static PDEVICE_OBJECT s_fltobj[CCP_MAX_COM_ID] = { 0 };
static PDEVICE_OBJECT s_nextobj[CCP_MAX_COM_ID] = { 0 };


UNICODE_STRING com_name = RTL_CONSTANT_STRING(L"\\Device\\Serial0");

// 这个函数是获取某设备的设备对象
PDEVICE_OBJECT ccpOpenCom(ULONG id, NTSTATUS* status) {
    UNICODE_STRING name_str;   // 声明一个UNICODE_STRING结构用于保存设备名称
    static WCHAR name[32] = { 0 };   // 创建一个32字符的WCHAR数组并初始化为0
    PFILE_OBJECT fileobj = NULL;   // 声明一个文件对象指针并初始化为NULL
    PDEVICE_OBJECT devobj = NULL;   // 声明一个设备对象指针并初始化为NULL

    //memset(name, 0, sizeof(WCHAR) * 32);   // 我觉得多余

    // 使用RtlStringCchPrintfW函数将设备名称格式化为字符串，并存储在name数组中
    RtlStringCchPrintfW(
        name, 32,
        L"\\Device\\Serial%d", id);

    RtlInitUnicodeString(&name_str, name);   // 初始化UNICODE_STRING结构，将其与name关联

    // 使用IoGetDeviceObjectPointer函数获取设备对象指针和文件对象指针
    *status = IoGetDeviceObjectPointer(
        &name_str,
        FILE_ALL_ACCESS,
        &fileobj, &devobj);

    if (*status == STATUS_SUCCESS) ObDereferenceObject(fileobj);   // 如果成功获取设备对象，减少文件对象的引用计数

    return devobj;   // 返回设备对象指针
}

// 这个函数：我们的驱动程序创建了一个设备对象，一开始就是空的，然后附加到COM1设备的设备对象上。 驱动对象创建的设备对象叫过滤设备对象，就是为了绑定真实设备对象用的。
// 然后这个设备对象是我们弄的，然后随意构造他，用他来过滤设备的信息，就是一个中间层。
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
    // 我这里没有 Characteristics 成员，好像也没事吧，试试
    // 因为太老了 https://www.nirsoft.net/kernel_struct/vista/DEVICE_OBJECT.html 这里有
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

// 把每一个设备的设备对象都附加一下
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
	PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(irp); // 获取当前 IRP 的栈位置信息。
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
// 入口点，跟main函数一样
NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg_path){
    size_t i;

    // 循环处理所有 IRP_MJ_MAXIMUM_FUNCTION 类型的 IRP 请求
        //IRP_MJ_CREATE: 用于处理文件或设备的创建请求。
        //IRP_MJ_CLOSE : 处理文件或设备的关闭请求。
        //IRP_MJ_READ : 处理从文件或设备读取数据的请求。
        //IRP_MJ_WRITE : 处理向文件或设备写入数据的请求。
        //IRP_MJ_DEVICE_CONTROL : 用于处理设备控制请求，通常由用户态应用程序通过 IOCTL 调用触发。
        //IRP_MJ_INTERNAL_DEVICE_CONTROL : 用于内部设备控制请求，通常由驱动程序内部的组件触发。
        //IRP_MJ_POWER : 处理电源管理请求，例如设备的启动、停止或休眠。
        //IRP_MJ_SYSTEM_CONTROL : 处理系统控制请求，通常用于支持 WMI(Windows Management Instrumentation)。
        //IRP_MJ_PNP : 处理即插即用(Plugand Play) 相关的请求，如设备的安装和卸载。
    for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) { // IRP_MJ_MAXIMUM_FUNCTION就是为了把所有函数都赋值
        // 打印调试信息
        DbgPrint("DriverEntry: %d\r\n", (int)i);

        // 为每个 IRP 类型设置处理函数 ccpDispatch
        driver->MajorFunction[i] = ccpDispatch; // 这里面的 i 对应 IRP_MJ_WRITE 这种宏定义。他的意思是，让每一个函数都变成 ccpDispatch，如果不赋值的话，本来也是没有函数的。
    }

    // 设置驱动程序卸载函数为 ccpUnload
    driver->DriverUnload = ccpUnload;

    // 调用 ccpAttachAllComs 函数来附加到所有串口通信端口
    ccpAttachAllComs(driver);

    // 返回 STATUS_SUCCESS 表示驱动加载成功
    return STATUS_SUCCESS;

}