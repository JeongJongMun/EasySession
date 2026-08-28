// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Startup checks that find common online configuration mistakes and say how to fix them: missing ini entries, the wrong net driver, login problems.
 * They run once when the subsystem starts, and again whenever EasySession.Diagnose is entered.
 * Neither the checks nor that console command exist in a Shipping build.
 */
namespace EasySessionDiagnostics
{
	/** How a finding is meant to be read. */
	enum class EFindingKind : uint8
	{
		/** A problem, with the correction attached. */
		Fix,

		/** Information worth knowing, nothing to correct. */
		Note,

		/** A check that passed and is worth saying so. */
		Ok,
	};

	/** One thing a diagnostics run found. */
	struct FFinding
	{
		EFindingKind Kind = EFindingKind::Fix;

		/** What was found, in one sentence. */
		FString Message;

		/** The exact lines that fix it, printed under an "Add to DefaultEngine.ini:" heading. Empty when the fix is not an ini entry. */
		TArray<FString> IniLines;

		/** One follow-up line printed after the ini lines, for advice that is not an ini entry. */
		FString Postscript;
	};

	/** Everything one diagnostics run found. */
	struct FReport
	{
		/** One line naming the online service the project asked for and the one that actually loaded. */
		FString Summary;

		/** The findings, in the order the checks ran. */
		TArray<FFinding> Findings;
	};

	/** Run all checks for the given world and return what they found. Writes nothing to the log - pass the report to LogReport for that. */
	FReport RunDiagnostics(UWorld* World);

	/**
	 * Write the report to the log, fixes as warnings with their ini lines, notes as plain lines.
	 * NOTE: The log reaches nobody in a packaged build unless it was launched with -log, so a caller that can print to the screen should print the report's Summary too.
	 */
	void LogReport(const FReport& Report);
}
