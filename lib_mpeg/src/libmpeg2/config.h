/* Copyright 2026 XMOS LIMITED.
 * SPDX-License-Identifier: MIT
 * Scalar baseline on every architecture, including native reference builds.
 * Deliberately do not define ARCH_* or ACCEL_DETECT.
 */
#ifndef XCORE_MPEG2_CONFIG_H_
#define XCORE_MPEG2_CONFIG_H_
#ifdef __xcore__
#define ATTRIBUTE_ALIGNED_MAX 8
#else
#define ATTRIBUTE_ALIGNED_MAX 64
#endif
#define HAVE_BUILTIN_EXPECT 1
#endif
