#include <bits/stdint-intn.h>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <numeric>
#include <vector>

#include "cuda.h"

namespace {
int32_t reportError(CUresult result, const char *where) {
  if (result != CUDA_SUCCESS) {
    std::cerr << "-> OEC-RT Error: CUDA failed with " << result << " in "
              << where << "\n";
  }
  return result;
}
} // anonymous namespace

static std::vector<std::pair<void *, CUdeviceptr>> paramBuffer;
static std::vector<CUmodule> moduleBuffer;
static CUstream stream;

extern "C" int32_t init() {
  CUcontext context;
  CUdevice device;
  int32_t err;
  err = reportError(cuInit(0), "Init");
  err = reportError(cuDeviceGet(&device, 0), "Init");
  err = reportError(cuCtxCreate(&context, CU_CTX_SCHED_SPIN, device), "Init");
  err = reportError(cuStreamCreate(&stream, CU_STREAM_DEFAULT), "StreamCreate");
  return err;
}

extern "C" int32_t oecTeardown() {
  int32_t err;
  for (auto module : moduleBuffer)
    err = reportError(cuModuleUnload(module), "ModuleUnload");
  for (auto param : paramBuffer) {
    if (param.first) {
      free(param.first);
    } else {
      err = reportError(cuMemFree(param.second), "MemFree");
    }
  }
  return err;
}

extern "C" int32_t oecModuleLoad(void **module, void *data) {
  int32_t err;
  err =
      reportError(cuModuleLoadData(reinterpret_cast<CUmodule *>(module), data),
                  "ModuleLoad");
  moduleBuffer.push_back(reinterpret_cast<CUmodule>(*module));
  return err;
}

extern "C" int32_t oecModuleGetFunction(void **function, void *module,
                                        const char *name) {
  return reportError(
      cuModuleGetFunction(reinterpret_cast<CUfunction *>(function),
                          reinterpret_cast<CUmodule>(module), name),
      "GetFunction");
}

extern "C" int32_t oecLaunchKernel(void *function, intptr_t gridX,
                                   intptr_t gridY, intptr_t gridZ,
                                   intptr_t blockX, intptr_t blockY,
                                   intptr_t blockZ, void **params) {
  return reportError(cuLaunchKernel(reinterpret_cast<CUfunction>(function),
                                    gridX, gridY, gridZ, blockX, blockY, blockZ,
                                    0, stream, params, nullptr),
                     "LaunchKernel");
}

extern "C" int32_t oecStreamSynchronize() {
  return reportError(cuStreamSynchronize(stream), "StreamSync");
}

extern "C" int32_t oecStoreParam(void *paramPtr, int64_t size, int32_t device) {
  int32_t err = CUDA_SUCCESS;
  CUdeviceptr devPtr = reinterpret_cast<CUdeviceptr>(nullptr);
  void *hostPtr = nullptr;
  if (device) {
    err = reportError(cuMemAlloc(&devPtr, size), "MemAlloc");
    err = reportError(cuMemcpyHtoD(devPtr, paramPtr, size), "MemCopy");
  } else {
    hostPtr = malloc(size);
    memcpy(hostPtr, paramPtr, size);
  }
  paramBuffer.push_back(std::make_pair(hostPtr, devPtr));
  return err;
}

extern "C" void oecFillParamArray(void **paramArray) {
  for (size_t i = 0, e = paramBuffer.size(); i != e; ++i) {
    if (paramBuffer[i].first) {
      paramArray[i] = reinterpret_cast<void *>(paramBuffer[i].first);
    } else {
      paramArray[i] = reinterpret_cast<void *>(&paramBuffer[i].second);
    }
  }
}
