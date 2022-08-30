#include <linux/types.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include "commands.h"
#include "lake_kapi.h"
#include "lake_shm.h"
#include "kargs.h"

/*
 *
 *   Functions in this file export CUDA symbols.
 *   In general they fill a struct and send it through netlink.
 *   They also choose if they are sync or async calls.
 *   Some have special handling, such as memcpys
 * 
 *   TODO: support netlink copies (not urgent)
 *   TODO: accumulate errors
 */

CUresult CUDAAPI cuInit(unsigned int flags) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuInit cmd = {
        .API_ID = LAKE_API_cuInit, .flags = flags,
    };
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
	return ret.res;
}
EXPORT_SYMBOL(cuInit);

CUresult CUDAAPI cuDeviceGet(CUdevice *device, int ordinal) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuDeviceGet cmd = {
        .API_ID = LAKE_API_cuDeviceGet, .ordinal = ordinal,
    };
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
    *device = ret.device;
	return ret.res;
}
EXPORT_SYMBOL(cuDeviceGet);

CUresult CUDAAPI cuCtxCreate(CUcontext *pctx, unsigned int flags, CUdevice dev) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuCtxCreate cmd = {
        .API_ID = LAKE_API_cuCtxCreate, .flags = flags, .dev = dev
    };
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
    *pctx = ret.pctx;
	return ret.res;
}
EXPORT_SYMBOL(cuCtxCreate);

CUresult CUDAAPI cuModuleLoad(CUmodule *module, const char *fname) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuModuleLoad cmd = {
        .API_ID = LAKE_API_cuModuleLoad
    };
    strcpy(cmd.fname, fname);
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
    *module = ret.module;
	return ret.res;
}
EXPORT_SYMBOL(cuModuleLoad);

CUresult CUDAAPI cuModuleUnload(CUmodule hmod) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuModuleUnload cmd = {
        .API_ID = LAKE_API_cuModuleUnload, .hmod = hmod
    };
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
	return ret.res;
}
EXPORT_SYMBOL(cuModuleUnload);

CUresult CUDAAPI cuModuleGetFunction(CUfunction *hfunc, CUmodule hmod, const char *name) {
    struct kernel_args_metadata* meta;
    struct lake_cmd_ret ret;
	struct lake_cmd_cuModuleGetFunction cmd = {
        .API_ID = LAKE_API_cuModuleGetFunction, .hmod = hmod
    };
    strcpy(cmd.name, name);
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
    *hfunc = ret.func;

    //parse and store kargs
    meta = get_kargs(*hfunc);
    kava_parse_function_args(name, meta);

    return ret.res;
}
EXPORT_SYMBOL(cuModuleGetFunction);

CUresult CUDAAPI cuLaunchKernel(CUfunction f,
                                unsigned int gridDimX,
                                unsigned int gridDimY,
                                unsigned int gridDimZ,
                                unsigned int blockDimX,
                                unsigned int blockDimY,
                                unsigned int blockDimZ,
                                unsigned int sharedMemBytes,
                                CUstream hStream,
                                void **kernelParams,
                                void **extra) {
    struct lake_cmd_ret ret;
    struct kernel_args_metadata* meta = get_kargs(f);
    u32 tsize = sizeof(struct lake_cmd_cuLaunchKernel) + meta->total_size;
    void* cmd_and_args = vmalloc(tsize);
	struct lake_cmd_cuLaunchKernel *cmd = (struct lake_cmd_cuLaunchKernel*) cmd_and_args;
    u8 *args = cmd_and_args + sizeof(struct lake_cmd_cuLaunchKernel);

    cmd->API_ID = LAKE_API_cuLaunchKernel; cmd->f = f; 
    cmd->gridDimX = gridDimX; cmd->gridDimY = gridDimY; cmd->gridDimZ = gridDimZ;
    cmd->blockDimX = blockDimX; cmd->blockDimY = blockDimY; cmd->blockDimZ = blockDimZ;
    cmd->sharedMemBytes = sharedMemBytes; cmd->hStream = hStream; cmd->extra = 0;

    cmd->paramsSize = meta->total_size;
    serialize_args(meta, args, kernelParams);

    lake_send_cmd(cmd_and_args, tsize, CMD_ASYNC, &ret);
    vfree(cmd_and_args);
	return ret.res;
}
EXPORT_SYMBOL(cuLaunchKernel);

CUresult CUDAAPI cuMemAlloc(CUdeviceptr *dptr, size_t bytesize) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuMemAlloc cmd = {
        .API_ID = LAKE_API_cuMemAlloc, .bytesize = bytesize
    };
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
    *dptr = ret.ptr;
	return ret.res;
}
EXPORT_SYMBOL(cuMemAlloc);

CUresult CUDAAPI cuMemcpyHtoD(CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuMemcpyHtoD cmd = {
        .API_ID = LAKE_API_cuMemcpyHtoD, .dstDevice = dstDevice, .srcHost = srcHost,
        .ByteCount = ByteCount
    };

    s64 offset = kava_shm_offset(srcHost);
    if (offset < 0) {
        pr_err("srcHost in cuMemcpyHtoD is NOT a kshm pointer (use kava_alloc to fix it)\n");
        return CUDA_ERROR_INVALID_VALUE;
    }
    cmd.srcHost = (void*)offset;
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
	return ret.res;
}
EXPORT_SYMBOL(cuMemcpyHtoD);

CUresult CUDAAPI cuMemcpyDtoH(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuMemcpyDtoH cmd = {
        .API_ID = LAKE_API_cuMemcpyDtoH, .srcDevice = srcDevice,
        .ByteCount = ByteCount
    };

    s64 offset = kava_shm_offset(dstHost);
    if (offset < 0) {
        pr_err("dstHost in cuMemcpyHtoD is NOT a kshm pointer (use kava_alloc to fix it)\n");
        return CUDA_ERROR_INVALID_VALUE;
    }
    cmd.dstHost = (void*)offset;
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
	return ret.res;
}
EXPORT_SYMBOL(cuMemcpyDtoH);

CUresult CUDAAPI cuCtxSynchronize(void) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuCtxSynchronize cmd = {
        .API_ID = LAKE_API_cuCtxSynchronize,
    };
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
	return ret.res;
}
EXPORT_SYMBOL(cuCtxSynchronize);

CUresult CUDAAPI cuMemFree(CUdeviceptr dptr) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuMemFree cmd = {
        .API_ID = LAKE_API_cuMemFree, .dptr = dptr
    };
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
	return ret.res;
}
EXPORT_SYMBOL(cuMemFree);

CUresult CUDAAPI cuStreamCreate(CUstream *phStream, unsigned int Flags) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuStreamCreate cmd = {
        .API_ID = LAKE_API_cuStreamCreate, .Flags = Flags
    };
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
    *phStream = ret.stream;
	return ret.res;
}
EXPORT_SYMBOL(cuStreamCreate);

CUresult CUDAAPI cuStreamSynchronize(CUstream hStream) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuStreamSynchronize cmd = {
        .API_ID = LAKE_API_cuStreamSynchronize, .hStream = hStream
    };
    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_SYNC, &ret);
	return ret.res;
}
EXPORT_SYMBOL(cuStreamSynchronize);

CUresult CUDAAPI cuMemcpyHtoDAsync(CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount, CUstream hStream) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuMemcpyHtoDAsync cmd = {
        .API_ID = LAKE_API_cuMemcpyHtoDAsync, .dstDevice = dstDevice, .srcHost = srcHost, 
        .ByteCount = ByteCount, .hStream = hStream
    };
    s64 offset = kava_shm_offset(srcHost);
    if (offset < 0) {
        pr_err("srcHost in cuMemcpyHtoD is NOT a kshm pointer (use kava_alloc to fix it)\n");
        return CUDA_ERROR_INVALID_VALUE;
    }
    cmd.srcHost = (void*)offset;

    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_ASYNC, &ret);
	return ret.res;
}
EXPORT_SYMBOL(cuMemcpyHtoDAsync);

CUresult CUDAAPI cuMemcpyDtoHAsync(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount, CUstream hStream) {
    struct lake_cmd_ret ret;
	struct lake_cmd_cuMemcpyDtoHAsync cmd = {
        .API_ID = LAKE_API_cuMemcpyDtoHAsync, .dstHost = dstHost, .srcDevice = srcDevice,
        .ByteCount = ByteCount, .hStream = hStream
    };
    
    s64 offset = kava_shm_offset(dstHost);
    if (offset < 0) {
        pr_err("dstHost in cuMemcpyHtoD is NOT a kshm pointer (use kava_alloc to fix it)\n");
        return CUDA_ERROR_INVALID_VALUE;
    }
    cmd.dstHost = (void*)offset;

    lake_send_cmd((void*)&cmd, sizeof(cmd), CMD_ASYNC, &ret);
	return ret.res;
}
EXPORT_SYMBOL(cuMemcpyDtoHAsync);