// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"
#include "EasySessionTestEventListener.generated.h"

/**
 * Records the session events a game would receive, so a test can assert on what a Blueprint binding would have seen.
 *
 * The events are dynamic multicast delegates, the kind Blueprint binds to, and those accept only UFUNCTIONs and never lambdas.
 * That is why this is a UObject rather than a plain struct.
 *
 * Deliberately not wrapped in WITH_DEV_AUTOMATION_TESTS.
 * UnrealHeaderTool would still emit reflection code for the class while the compiler dropped its body, and that mismatch only shows up in a packaged build.
 * The class is a couple of arrays, so shipping it in every configuration costs less than the risk.
 */
UCLASS()
class UEasySessionTestEventListener : public UObject
{
	GENERATED_BODY()

public:

	/** Results seen on OnSessionStarted, in order. */
	UPROPERTY()
	TArray<EEasySessionResult> StartedResults;

	/** Results seen on OnSessionEnded, in order. */
	UPROPERTY()
	TArray<EEasySessionResult> EndedResults;

	/** Bind to OnSessionStarted. */
	UFUNCTION()
	void HandleStarted(EEasySessionResult Result, const FString& ErrorMessage)
	{
		StartedResults.Add(Result);
	}

	/** Bind to OnSessionEnded. */
	UFUNCTION()
	void HandleEnded(EEasySessionResult Result, const FString& ErrorMessage)
	{
		EndedResults.Add(Result);
	}

	/** @return How many events arrived in total, so a test can assert that none were duplicated. */
	int32 TotalEvents() const { return StartedResults.Num() + EndedResults.Num(); }

	/** Every result seen, for a failure message that names what actually arrived. */
	FString Describe() const
	{
		FString Out;
		for (const EEasySessionResult Result : StartedResults)
		{
			Out += FString::Printf(TEXT("Started=%s "), *EasySession::ResultToString(Result));
		}
		for (const EEasySessionResult Result : EndedResults)
		{
			Out += FString::Printf(TEXT("Ended=%s "), *EasySession::ResultToString(Result));
		}
		return Out.IsEmpty() ? TEXT("none") : Out;
	}
};
