// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

#define SEGGER_RTT_MAX_NUM_UP_BUFFERS     (1)
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS   (1)

#define SEGGER_RTT_BUFFER_SIZE_UP         (4096)
#define SEGGER_RTT_BUFFER_SIZE_DOWN       (16)

#define SEGGER_RTT_MODE_DEFAULT           SEGGER_RTT_MODE_NO_BLOCK_SKIP

#define SEGGER_RTT_PRINTF_BUFFER_SIZE     (256)

#endif
