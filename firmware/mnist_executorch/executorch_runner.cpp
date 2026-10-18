// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: BSD-3-Clause-Clear
/**
 * @file executorch_runner.cpp
 * @brief ExecuTorch MNIST runtime integration for Alif E8 M55-HP.
 */

#include "RTE_Components.h"
#include CMSIS_device_header

#include "SEGGER_RTT.h"
#include "ethosu_driver.h"
#include "sys_utils.h"
#include "executorch_runner.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include <executorch/runtime/core/data_loader.h>
#include <executorch/runtime/core/evalue.h>
#include <executorch/runtime/core/hierarchical_allocator.h>
#include <executorch/runtime/core/memory_allocator.h>
#include <executorch/runtime/core/span.h>
#include <executorch/runtime/executor/memory_manager.h>
#include <executorch/runtime/executor/method.h>
#include <executorch/runtime/executor/method_meta.h>
#include <executorch/runtime/executor/program.h>
#include <executorch/runtime/platform/platform.h>
#include <executorch/runtime/platform/runtime.h>

#define NPU_HG_STATUS (*(volatile uint32_t *)(NPU_HG_BASE + 0x04))

using namespace executorch::runtime;

namespace {

constexpr size_t kMaxPlannedBuffers = 16;
constexpr size_t kMaxTensorDims = 8;
constexpr size_t kMaxInputElements = 4096;
constexpr size_t kPalPoolSize = 256 * 1024;
constexpr size_t kMethodPoolSize = 768 * 1024;
constexpr size_t kTempPoolSize = 1536 * 1024;
constexpr size_t kPlannedPoolSize = 2 * 1024 * 1024;

struct ethosu_driver g_ethos_driver;
uint32_t g_last_execute_cycles = 0;
int g_verbose = 1;
#define ET_VLOG(...) do { if (g_verbose) printf(__VA_ARGS__); } while (0)
bool g_runtime_initialized = false;
bool g_npu_initialized = false;
const uint8_t* g_model_data = nullptr;
size_t g_model_size = 0;

__attribute__((section(".bss.at_sram0"), aligned(16)))
uint8_t g_pal_pool[kPalPoolSize];

__attribute__((section(".bss.at_sram0"), aligned(16)))
uint8_t g_method_pool[kMethodPoolSize];

__attribute__((section(".bss.at_sram0"), aligned(16)))
uint8_t g_temp_pool[kTempPoolSize];

__attribute__((section(".bss.at_sram0"), aligned(16)))
float g_input_float[kMaxInputElements];

__attribute__((section(".bss.at_sram1"), aligned(16)))
uint8_t g_planned_pool[kPlannedPoolSize];

size_t g_pal_offset = 0;

class BufferDataLoader final : public DataLoader {
public:
    BufferDataLoader(const void* data, size_t size)
        : data_(static_cast<const uint8_t*>(data)), size_(size) {}

    Result<FreeableBuffer> load(
        size_t offset,
        size_t size,
        const SegmentInfo& segment_info) const override
    {
        (void)segment_info;
        if (offset + size > size_) {
            return Error::InvalidArgument;
        }
        return FreeableBuffer(data_ + offset, size, nullptr);
    }

    Result<size_t> size() const override
    {
        return size_;
    }

private:
    const uint8_t* data_;
    size_t size_;
};

const char* scalar_type_name(executorch::aten::ScalarType type)
{
    switch (type) {
    case executorch::aten::ScalarType::Byte:
        return "uint8";
    case executorch::aten::ScalarType::Char:
        return "int8";
    case executorch::aten::ScalarType::Short:
        return "int16";
    case executorch::aten::ScalarType::Int:
        return "int32";
    case executorch::aten::ScalarType::Float:
        return "float32";
    default:
        return "other";
    }
}

void print_tensor_info(const char* label, const TensorInfo& info)
{
    ET_VLOG("[ET] %s: type=%s nbytes=%u shape=[",
           label,
           scalar_type_name(info.scalar_type()),
           static_cast<unsigned>(info.nbytes()));

    auto sizes = info.sizes();
    for (size_t i = 0; i < sizes.size(); ++i) {
        ET_VLOG("%ld%s", static_cast<long>(sizes[i]), (i + 1 == sizes.size()) ? "" : ",");
    }
    ET_VLOG("]\r\n");
}

size_t tensor_numel(const TensorInfo& info)
{
    size_t count = 1;
    auto sizes = info.sizes();
    for (size_t i = 0; i < sizes.size(); ++i) {
        count *= static_cast<size_t>(sizes[i]);
    }
    return count;
}

void make_contiguous_metadata(
    const TensorInfo& info,
    executorch::aten::TensorImpl::SizesType* sizes,
    executorch::aten::TensorImpl::DimOrderType* dim_order,
    executorch::aten::TensorImpl::StridesType* strides)
{
    auto src_sizes = info.sizes();
    const size_t dim = src_sizes.size();

    for (size_t i = 0; i < dim; ++i) {
        sizes[i] = src_sizes[i];
        dim_order[i] = static_cast<executorch::aten::TensorImpl::DimOrderType>(i);
    }

    executorch::aten::TensorImpl::StridesType stride = 1;
    for (size_t i = dim; i > 0; --i) {
        strides[i - 1] = stride;
        stride *= sizes[i - 1];
    }
}

int npu_init()
{
    if (g_npu_initialized) {
        return 0;
    }

    if (ethosu_init(&g_ethos_driver,
                    reinterpret_cast<void*>(NPU_HG_BASE),
                    0,
                    0,
                    1,
                    1)) {
        printf("[ET] ERROR: ethosu_init failed\r\n");
        return -1;
    }

    g_npu_initialized = true;
    ET_VLOG("[ET] Ethos-U85 initialized at 0x%08lx\r\n",
           static_cast<unsigned long>(NPU_HG_BASE));
    return 0;
}

int fill_input_tensor(
    const TensorInfo& input_info,
    const int8_t* input_data,
    size_t input_size,
    executorch::aten::TensorImpl::SizesType* sizes,
    executorch::aten::TensorImpl::DimOrderType* dim_order,
    executorch::aten::TensorImpl::StridesType* strides,
    executorch::aten::TensorImpl* input_impl,
    executorch::aten::Tensor* input_tensor,
    EValue* input_evalue)
{
    const size_t dim = input_info.sizes().size();
    if (dim == 0 || dim > kMaxTensorDims) {
        printf("[ET] ERROR: unsupported input rank %u\r\n", static_cast<unsigned>(dim));
        return -1;
    }

    const size_t numel = tensor_numel(input_info);
    if (numel > kMaxInputElements) {
        printf("[ET] ERROR: input tensor too large: %u elements\r\n", static_cast<unsigned>(numel));
        return -2;
    }

    make_contiguous_metadata(input_info, sizes, dim_order, strides);

    void* data_ptr = nullptr;
    auto scalar_type = input_info.scalar_type();
    if (scalar_type == executorch::aten::ScalarType::Float) {
        if (input_size < numel) {
            printf("[ET] ERROR: input buffer too small: %u < %u\r\n",
                   static_cast<unsigned>(input_size),
                   static_cast<unsigned>(numel));
            return -3;
        }
        for (size_t i = 0; i < numel; ++i) {
            const float x = static_cast<float>(input_data[i]) / 127.0f;
            g_input_float[i] = (x - 0.1307f) / 0.3081f;
        }
        data_ptr = g_input_float;
    } else if (scalar_type == executorch::aten::ScalarType::Char ||
               scalar_type == executorch::aten::ScalarType::Byte) {
        if (input_size < input_info.nbytes()) {
            printf("[ET] ERROR: input buffer too small: %u < %u\r\n",
                   static_cast<unsigned>(input_size),
                   static_cast<unsigned>(input_info.nbytes()));
            return -4;
        }
        data_ptr = const_cast<int8_t*>(input_data);
    } else {
        printf("[ET] ERROR: unsupported input scalar type %d\r\n",
               static_cast<int>(scalar_type));
        return -5;
    }

    new (input_impl) executorch::aten::TensorImpl(
        scalar_type,
        static_cast<ssize_t>(dim),
        sizes,
        data_ptr,
        dim_order,
        strides);
    new (input_tensor) executorch::aten::Tensor(input_impl);
    new (input_evalue) EValue(*input_tensor);

    return 0;
}

int copy_output_tensor(const executorch::aten::Tensor& output_tensor, int8_t* output_data, size_t output_size)
{
    const size_t count = std::min(static_cast<size_t>(output_tensor.numel()), output_size);
    if (count == 0) {
        return -1;
    }

    if (output_tensor.scalar_type() == executorch::aten::ScalarType::Char) {
        const int8_t* scores = output_tensor.const_data_ptr<int8_t>();
        for (size_t i = 0; i < count; ++i) {
            output_data[i] = scores[i];
        }
        return 0;
    }

    if (output_tensor.scalar_type() == executorch::aten::ScalarType::Byte) {
        const uint8_t* scores = output_tensor.const_data_ptr<uint8_t>();
        for (size_t i = 0; i < count; ++i) {
            output_data[i] = static_cast<int8_t>(std::min<unsigned>(scores[i], 127U));
        }
        return 0;
    }

    if (output_tensor.scalar_type() == executorch::aten::ScalarType::Float) {
        const float* scores = output_tensor.const_data_ptr<float>();
        ET_VLOG("[ET] raw logits x1000:");
        for (size_t i = 0; i < count; ++i) {
            ET_VLOG(" %ld", static_cast<long>(scores[i] * 1000.0f));
        }
        ET_VLOG("\r\n");
        float min_score = scores[0];
        float max_score = scores[0];
        for (size_t i = 1; i < count; ++i) {
            min_score = std::min(min_score, scores[i]);
            max_score = std::max(max_score, scores[i]);
        }

        const float range = max_score - min_score;
        for (size_t i = 0; i < count; ++i) {
            float scaled = (range > 0.0f) ? ((scores[i] - min_score) * 127.0f / range) : 0.0f;
            scaled = std::max(0.0f, std::min(127.0f, scaled));
            output_data[i] = static_cast<int8_t>(scaled);
        }
        return 0;
    }

    printf("[ET] ERROR: unsupported output scalar type %d\r\n",
           static_cast<int>(output_tensor.scalar_type()));
    return -2;
}

} // namespace

struct ethosu_sem_t {
    uint8_t count;
};

extern "C" int ethosu_semaphore_take(void* sem, uint64_t timeout)
{
    (void)timeout;

    ethosu_sem_t* s = static_cast<ethosu_sem_t*>(sem);

    while (s->count == 0) {
        if (NPU_HG_STATUS & 0x2U) {
            ethosu_irq_handler(&g_ethos_driver);
        }
        __NOP();
    }

    s->count--;
    return 0;
}

extern "C" void NPU_HP_IRQHandler(void)
{
}

/*
 * The NPU is a bus master on the global address map. Buffers in the M55's DTCM/ITCM
 * have CPU-local addresses (0x2000_0000 / 0x0000_0000) that the NPU can't reach, so
 * translate them to their global alias. SRAM0/1 and MRAM addresses pass through.
 */
extern "C" uint64_t ethosu_address_remap(uint64_t address, int index)
{
    (void)index;
    return LocalToGlobal(reinterpret_cast<const volatile void*>(static_cast<uintptr_t>(address)));
}

/*
 * Cache maintenance for the Ethos-U driver. The M55 D-cache is enabled and the
 * tensor arenas live in cacheable SRAM, but the NPU reads/writes memory directly.
 * Without these overrides (the driver's weak versions are empty) the NPU reads
 * stale input and the CPU reads stale output.
 */
extern "C" void ethosu_flush_dcache(const uint64_t* base_addr, const size_t* base_addr_size,
                                    int num_base_addr)
{
    (void)base_addr;
    (void)base_addr_size;
    (void)num_base_addr;
    SCB_CleanDCache();
}

extern "C" void ethosu_invalidate_dcache(const uint64_t* base_addr, const size_t* base_addr_size,
                                         int num_base_addr)
{
    (void)base_addr;
    (void)base_addr_size;
    (void)num_base_addr;
    SCB_CleanInvalidateDCache();
}

extern "C" {

void et_pal_init(void) {}

ET_NORETURN void et_pal_abort(void)
{
    ET_VLOG("[ET] et_pal_abort() called\r\n");
    __BKPT(0);
    while (1) {
        __WFI();
    }
}

et_timestamp_t et_pal_current_ticks(void)
{
    return 0;
}

et_tick_ratio_t et_pal_ticks_to_ns_multiplier(void)
{
    return {1, 1};
}

void et_pal_emit_log_message(
    et_timestamp_t timestamp,
    et_pal_log_level_t level,
    const char* filename,
    const char* function,
    size_t line,
    const char* message,
    size_t length)
{
    (void)timestamp;
    (void)filename;
    (void)function;
    (void)line;
    if (g_verbose || level == 'E' || level == 'F') printf("[ET:%c] %.*s\r\n", static_cast<char>(level), static_cast<int>(length), message);
}

void* et_pal_allocate(size_t size)
{
    size_t aligned = (g_pal_offset + 15U) & ~static_cast<size_t>(15U);
    if (aligned + size > sizeof(g_pal_pool)) {
        ET_VLOG("[ET] et_pal_allocate(%u) failed, used=%u\r\n",
               static_cast<unsigned>(size),
               static_cast<unsigned>(g_pal_offset));
        return nullptr;
    }

    void* ptr = g_pal_pool + aligned;
    g_pal_offset = aligned + size;
    return ptr;
}

void et_pal_free(void* ptr)
{
    (void)ptr;
}

int executorch_init(const uint8_t* model_data, size_t model_size)
{
    if (!model_data || model_size == 0) {
        printf("[ET] ERROR: invalid model data\r\n");
        return -1;
    }

    if (!g_runtime_initialized) {
        runtime_init();
        g_runtime_initialized = true;
        ET_VLOG("[ET] ExecuTorch runtime initialized\r\n");
    }

    if (npu_init() != 0) {
        return -2;
    }

    g_model_data = model_data;
    g_model_size = model_size;

    ET_VLOG("[ET] Model registered (%u bytes)\r\n", static_cast<unsigned>(model_size));
    return 0;
}

int executorch_run_inference(
    const int8_t* input_data,
    size_t input_size,
    int8_t* output_data,
    size_t output_size)
{
    if (!g_model_data || g_model_size == 0) {
        printf("[ET] ERROR: not initialized\r\n");
        return -1;
    }
    if (!input_data || !output_data || output_size == 0) {
        printf("[ET] ERROR: invalid input/output buffers\r\n");
        return -2;
    }

    ET_VLOG("[ET] Loading program\r\n");
    BufferDataLoader loader(g_model_data, g_model_size);
    auto program_result = Program::load(&loader, Program::Verification::Minimal);
    if (!program_result.ok()) {
        printf("[ET] ERROR: Program::load failed (%d)\r\n", static_cast<int>(program_result.error()));
        return -3;
    }
    Program program(std::move(program_result.get()));

    auto meta_result = program.method_meta("forward");
    if (!meta_result.ok()) {
        printf("[ET] ERROR: method_meta failed (%d)\r\n", static_cast<int>(meta_result.error()));
        return -4;
    }
    MethodMeta meta = meta_result.get();

    ET_VLOG("[ET] Method '%s': inputs=%u outputs=%u planned=%u\r\n",
           meta.name(),
           static_cast<unsigned>(meta.num_inputs()),
           static_cast<unsigned>(meta.num_outputs()),
           static_cast<unsigned>(meta.num_memory_planned_buffers()));

    if (meta.num_inputs() < 1 || meta.num_outputs() < 1) {
        printf("[ET] ERROR: method must have at least one input and output\r\n");
        return -5;
    }

    auto input_info_result = meta.input_tensor_meta(0);
    if (!input_info_result.ok()) {
        printf("[ET] ERROR: input metadata failed (%d)\r\n", static_cast<int>(input_info_result.error()));
        return -6;
    }
    TensorInfo input_info = input_info_result.get();
    print_tensor_info("input[0]", input_info);

    auto output_info_result = meta.output_tensor_meta(0);
    if (output_info_result.ok()) {
        print_tensor_info("output[0]", output_info_result.get());
    }

    const size_t planned_count = meta.num_memory_planned_buffers();
    if (planned_count > kMaxPlannedBuffers) {
        printf("[ET] ERROR: too many planned buffers: %u\r\n", static_cast<unsigned>(planned_count));
        return -7;
    }

    Span<uint8_t> planned_spans[kMaxPlannedBuffers];
    size_t planned_offset = 0;
    for (size_t i = 0; i < planned_count; ++i) {
        auto size_result = meta.memory_planned_buffer_size(i);
        if (!size_result.ok()) {
            printf("[ET] ERROR: planned buffer size failed for %u\r\n", static_cast<unsigned>(i));
            return -8;
        }
        const size_t planned_size = static_cast<size_t>(size_result.get());
        const size_t aligned = (planned_offset + 15U) & ~static_cast<size_t>(15U);
        if (aligned + planned_size > sizeof(g_planned_pool)) {
            printf("[ET] ERROR: planned pool overflow at buffer %u: need=%u used=%u cap=%u\r\n",
                   static_cast<unsigned>(i),
                   static_cast<unsigned>(planned_size),
                   static_cast<unsigned>(aligned),
                   static_cast<unsigned>(sizeof(g_planned_pool)));
            return -9;
        }

        planned_spans[i] = Span<uint8_t>(g_planned_pool + aligned, planned_size);
        planned_offset = aligned + planned_size;
        ET_VLOG("[ET] planned[%u]=%u bytes\r\n",
               static_cast<unsigned>(i),
               static_cast<unsigned>(planned_size));
    }

    HierarchicalAllocator planned_memory(Span<Span<uint8_t>>(planned_spans, planned_count));
    MemoryAllocator method_allocator(kMethodPoolSize, g_method_pool);
    MemoryAllocator temp_allocator(kTempPoolSize, g_temp_pool);
    MemoryManager memory_manager(&method_allocator, &planned_memory, &temp_allocator);

    ET_VLOG("[ET] Loading method\r\n");
    auto method_result = program.load_method("forward", &memory_manager);
    if (!method_result.ok()) {
        printf("[ET] ERROR: load_method failed (%d)\r\n", static_cast<int>(method_result.error()));
        return -10;
    }
    Method method(std::move(method_result.get()));

    alignas(16) executorch::aten::TensorImpl::SizesType input_sizes[kMaxTensorDims];
    alignas(16) executorch::aten::TensorImpl::DimOrderType input_dim_order[kMaxTensorDims];
    alignas(16) executorch::aten::TensorImpl::StridesType input_strides[kMaxTensorDims];
    alignas(16) uint8_t input_impl_storage[sizeof(executorch::aten::TensorImpl)];
    alignas(16) uint8_t input_tensor_storage[sizeof(executorch::aten::Tensor)];
    alignas(16) uint8_t input_evalue_storage[sizeof(EValue)];

    auto* input_impl = reinterpret_cast<executorch::aten::TensorImpl*>(input_impl_storage);
    auto* input_tensor = reinterpret_cast<executorch::aten::Tensor*>(input_tensor_storage);
    auto* input_evalue = reinterpret_cast<EValue*>(input_evalue_storage);

    int input_status = fill_input_tensor(
        input_info,
        input_data,
        input_size,
        input_sizes,
        input_dim_order,
        input_strides,
        input_impl,
        input_tensor,
        input_evalue);
    if (input_status != 0) {
        return -11;
    }

    Error err = method.set_input(*input_evalue, 0);
    if (err != Error::Ok) {
        printf("[ET] ERROR: set_input failed (%d)\r\n", static_cast<int>(err));
        return -12;
    }

    ET_VLOG("[ET] Executing method\r\n");
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    const uint32_t t0 = DWT->CYCCNT;
    err = method.execute();
    const uint32_t cycles = DWT->CYCCNT - t0;
    g_last_execute_cycles = cycles;
    if (err != Error::Ok) {
        printf("[ET] ERROR: execute failed (%d)\r\n", static_cast<int>(err));
        return -13;
    }
    ET_VLOG("[ET] execute: %lu cycles = %lu us @ %lu MHz\r\n",
           static_cast<unsigned long>(cycles),
           static_cast<unsigned long>(cycles / (SystemCoreClock / 1000000U)),
           static_cast<unsigned long>(SystemCoreClock / 1000000U));

    const EValue& output_evalue = method.get_output(0);
    if (!output_evalue.isTensor()) {
        printf("[ET] ERROR: output[0] is not a tensor\r\n");
        return -14;
    }

    const executorch::aten::Tensor& output_tensor = output_evalue.toTensor();
    ET_VLOG("[ET] Output tensor: type=%s elements=%ld bytes=%u\r\n",
           scalar_type_name(output_tensor.scalar_type()),
           static_cast<long>(output_tensor.numel()),
           static_cast<unsigned>(output_tensor.nbytes()));

    int copy_status = copy_output_tensor(output_tensor, output_data, output_size);
    if (copy_status != 0) {
        return -15;
    }

    ET_VLOG("[ET] Inference complete\r\n");
    return 0;
}

void executorch_deinit(void)
{
    g_model_data = nullptr;
    g_model_size = 0;
    g_pal_offset = 0;
    ET_VLOG("[ET] Deinitialized\r\n");
}

void executorch_set_verbose(int verbose)
{
    g_verbose = verbose;
}

uint32_t executorch_last_execute_cycles(void)
{
    return g_last_execute_cycles;
}

} // extern "C"
