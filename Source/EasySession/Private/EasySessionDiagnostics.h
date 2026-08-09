// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Startup diagnostics that detect common online configuration mistakes and log
 * actionable fixes: missing ini entries, wrong net driver, login problems.
 * Outside Shipping, runs once when the subsystem comes up; on demand via
 * EasySession.Diagnose, which is itself Shipping-free.
 */
namespace EasySessionDiagnostics
{
	/**
	 * Run all checks for the given world and log the results.
	 *
	 * Returns a one-line summary of which service was asked for and which one
	 * actually came up. The details only reach the log, which a packaged build
	 * shows to nobody unless it was launched with -log, so callers that have a
	 * screen (the console command) print this line as well.
	 */
	FString RunDiagnostics(UWorld* World);
}
