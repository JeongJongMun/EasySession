// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"
#include "EasySessionTestEventListener.generated.h"

/**
 * Records the session events a game would receive, so a test can assert on what
 * a Blueprint binding would have seen.
 *
 * The events are dynamic multicast delegates - the kind Blueprint binds to - and
 * those only accept UFUNCTIONs, never lambdas. Hence a small UObject.
 *
 * Deliberately not wrapped in WITH_DEV_AUTOMATION_TESTS: UnrealHeaderTool would
 * still emit reflection code for the class while the compiler dropped its body,
 * and the mismatch only surfaces in a packaged build. The class is a handful of
 * counters, so paying for it in every configuration is the cheaper trade.
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

	UFUNCTION()
	void HandleStarted(EEasySessionResult Result, const FString& ErrorMessage)
	{
		StartedResults.Add(Result);
	}

	UFUNCTION()
	void HandleEnded(EEasySessionResult Result, const FString& ErrorMessage)
	{
		EndedResults.Add(Result);
	}

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
