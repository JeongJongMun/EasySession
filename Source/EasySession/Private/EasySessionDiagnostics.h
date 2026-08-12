// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Startup checks that find common online configuration mistakes and log how to fix them: missing ini entries, the wrong net driver, login problems.
 * They run once when the subsystem starts, and again whenever EasySession.Diagnose is entered.
 * Neither the checks nor that console command exist in a Shipping build.
 */
namespace EasySessionDiagnostics
{
	/**
	 * Run all checks for the given world and log the results.
	 *
	 * @return One line naming the online service the project asked for and the one that actually loaded.
	 *         The full findings only go to the log, which a packaged build shows to nobody unless it was
	 *         launched with -log, so a caller that can print to the screen should print this line too.
	 */
	FString RunDiagnostics(UWorld* World);
}
