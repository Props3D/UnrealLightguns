// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT
//
// Host-side tests for the engine-free parts of LightgunCore. Build and run from the repo root:
//   c++ -std=c++11 -Wall -Wextra -Werror -ISource/LightgunCore/Public Tests/host/HostTests.cpp Tests/host/ReportTests.cpp Tests/host/DeviceMatchTests.cpp Tests/host/InputReportTests.cpp Tests/host/DeviceInfoReportTests.cpp -o Tests/host/host_tests && Tests/host/host_tests

#include "TestHarness.h"

int GFailures = 0;
int GChecks = 0;

int main()
{
	std::printf("FLightgunReport\n");
	RunReportTests();
	std::printf("LightgunDeviceMatch\n");
	RunDeviceMatchTests();
	std::printf("LightgunInputReport\n");
	RunInputReportTests();
	std::printf("LightgunDeviceInfoReport\n");
	RunDeviceInfoReportTests();

	std::printf("%d checks, %d failures\n", GChecks, GFailures);
	return GFailures == 0 ? 0 : 1;
}
