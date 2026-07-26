// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Startup diagnostics that detect common online configuration mistakes and log
 * actionable fixes: missing ini entries, wrong net driver, login problems.
 * Runs once when the subsystem comes up and on demand via EasySession.Diagnose.
 */
namespace EasySessionDiagnostics
{
	/** Run all checks for the given world and log the results. */
	void RunDiagnostics(UWorld* World);
}
