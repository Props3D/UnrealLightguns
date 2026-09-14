// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT
//
// Minimal assertion helpers for the host tests. No framework dependency.

#pragma once

#include <cstdio>

extern int GFailures;
extern int GChecks;

#define CHECK_EQ(Actual, Expected) \
	do { \
		++GChecks; \
		const long long ActualValue = static_cast<long long>(Actual); \
		const long long ExpectedValue = static_cast<long long>(Expected); \
		if (ActualValue != ExpectedValue) \
		{ \
			++GFailures; \
			std::printf("  FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #Actual, ActualValue, ExpectedValue); \
		} \
	} while (0)

#define CHECK(Condition) CHECK_EQ((Condition) ? 1 : 0, 1)

void RunReportTests();
void RunDeviceMatchTests();
void RunInputReportTests();
void RunDeviceInfoReportTests();
