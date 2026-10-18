// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: BSD-3-Clause-Clear
/**

 * @file executorch_runner.h

 * @brief ExecuTorch C wrapper for Alif E8

 */



#ifndef EXECUTORCH_RUNNER_H

#define EXECUTORCH_RUNNER_H



#include <stdint.h>

#include <stddef.h>



#ifdef __cplusplus

extern "C" {

#endif



/**

 * @brief Initialize ExecuTorch runtime with model

 * @param model_data Pointer to .pte model data

 * @param model_size Size of model data in bytes

 * @return 0 on success, negative on error

 */

int executorch_init(const uint8_t* model_data, size_t model_size);



/**

 * @brief Run inference on input data

 * @param input_data Pointer to input tensor (INT8)

 * @param input_size Size of input in bytes

 * @param output_data Pointer to output buffer (INT8)

 * @param output_size Size of output buffer in bytes

 * @return 0 on success, negative on error

 */

int executorch_run_inference(const int8_t* input_data, size_t input_size,

                              int8_t* output_data, size_t output_size);



/**

 * @brief Deinitialize ExecuTorch runtime

 */

void executorch_deinit(void);



/**

 * @brief CPU cycles spent in method.execute() during the last inference

 */

void executorch_set_verbose(int verbose);

uint32_t executorch_last_execute_cycles(void);



#ifdef __cplusplus

}

#endif



#endif /* EXECUTORCH_RUNNER_H */

