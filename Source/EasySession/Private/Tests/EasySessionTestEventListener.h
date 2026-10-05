// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionSubsystem.h"
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

	/** How many times OnSessionSettingsChanged fired, so a test can prove a repeated settings payload does not fire it again. */
	UPROPERTY()
	int32 SettingsChangedBroadcasts = 0;

	/** Bind to OnSessionSettingsChanged. */
	UFUNCTION()
	void HandleSettingsChanged()
	{
		++SettingsChangedBroadcasts;
	}

	/** The states seen on OnSessionStateChanged, in order. */
	UPROPERTY()
	TArray<EEasySessionState> SessionStates;

	/** Bind to OnSessionStateChanged. */
	UFUNCTION()
	void HandleSessionStateChanged(EEasySessionState OldState, EEasySessionState NewState)
	{
		SessionStates.Add(NewState);
	}

	/** Reasons seen on OnSessionFailure, in order. */
	UPROPERTY()
	TArray<FString> FailureReasons;

	/** Bind to OnSessionFailure. */
	UFUNCTION()
	void HandleSessionFailure(const FString& Reason)
	{
		FailureReasons.Add(Reason);
	}

	/** When set, entering Hosting calls CancelMatchmaking on this subsystem, which the run ignores in that state. */
	UPROPERTY()
	TObjectPtr<UEasySessionSubsystem> CancelMatchmakingOnHosting;

	/** Bind to the subsystem's OnMatchmakingStateChanged. */
	UFUNCTION()
	void HandleMatchmakingState(EEasyMatchmakingState OldState, EEasyMatchmakingState NewState)
	{
		if (NewState == EEasyMatchmakingState::Hosting && CancelMatchmakingOnHosting)
		{
			CancelMatchmakingOnHosting->CancelMatchmaking();
		}
	}

	/**
	 * Everything seen from the subsystem's matchmaking events, in arrival order.
	 * OnMatchmakingUpdated is kept out, both the once-a-second broadcasts and the ones fired on every state change, and goes to MatchmakingElapsedSeen.
	 */
	UPROPERTY()
	TArray<FString> MatchmakingJournal;

	/** Elapsed seconds seen on OnMatchmakingUpdated, in arrival order. */
	UPROPERTY()
	TArray<int32> MatchmakingElapsedSeen;

	/** Bind to the subsystem's OnMatchmakingStarted. */
	UFUNCTION()
	void HandleMatchmakingStarted()
	{
		MatchmakingJournal.Add(TEXT("Started"));
	}

	/** Bind to the subsystem's OnMatchmakingStateChanged. */
	UFUNCTION()
	void HandleMatchmakingTransition(EEasyMatchmakingState OldState, EEasyMatchmakingState NewState)
	{
		const UEnum* StateEnum = StaticEnum<EEasyMatchmakingState>();
		MatchmakingJournal.Add(FString::Printf(TEXT("State=%s>%s"),
			*StateEnum->GetNameStringByValue(static_cast<int64>(OldState)),
			*StateEnum->GetNameStringByValue(static_cast<int64>(NewState))));
	}

	/** Bind to the subsystem's OnMatchmakingUpdated. */
	UFUNCTION()
	void HandleMatchmakingUpdated(EEasyMatchmakingState MatchmakingState, int32 ElapsedSeconds)
	{
		MatchmakingElapsedSeen.Add(ElapsedSeconds);
	}

	/** Bind to the subsystem's OnMatchmakingComplete. */
	UFUNCTION()
	void HandleMatchmakingCompleted(EEasySessionResult Result, const FString& ErrorMessage)
	{
		MatchmakingJournal.Add(FString::Printf(TEXT("Completed=%s"), *EasySession::ResultToString(Result)));
	}

	/** How many times OnPartyMembersChanged fired. */
	UPROPERTY()
	int32 PartyMembersChangedBroadcasts = 0;

	/** Bind to the subsystem's OnPartyMembersChanged. */
	UFUNCTION()
	void HandlePartyMembersChanged()
	{
		++PartyMembersChangedBroadcasts;
	}

	/** The reasons seen on OnPartyLeft, in order. */
	UPROPERTY()
	TArray<EEasyPartyLeaveReason> PartyLeftReasons;

	/** Bind to the subsystem's OnPartyLeft. */
	UFUNCTION()
	void HandlePartyLeft(EEasyPartyLeaveReason Reason, const FText& ReasonText)
	{
		PartyLeftReasons.Add(Reason);
	}

	/** How many journal entries match the given prefix, so a test can assert an event fired exactly once. */
	int32 CountJournal(const TCHAR* Prefix) const
	{
		int32 Count = 0;
		for (const FString& Entry : MatchmakingJournal)
		{
			if (Entry.StartsWith(Prefix))
			{
				++Count;
			}
		}
		return Count;
	}
};

/**
 * Records which output pin of an async node fired, so a test can assert on what a Blueprint graph would have run.
 *
 * A node reaches its game through those two pins and nothing else, so a pin that never fires leaves the graph waiting forever.
 * Every node routes Success to On Success and every other result to On Failure, which is what these handlers check.
 *
 * The pins come in three shapes because Find and Read Friends carry their results alongside the enum.
 * All six handlers record into the same two arrays, so a test never has to know which shape it just bound to.
 *
 * A UObject for the same reason as UEasySessionTestEventListener above: these delegates take UFUNCTIONs, never lambdas.
 */
UCLASS()
class UEasySessionTestNodePinListener : public UObject
{
	GENERATED_BODY()

public:

	/** Results seen on On Success, in order. */
	UPROPERTY()
	TArray<EEasySessionResult> SuccessResults;

	/** Results seen on On Failure, in order. */
	UPROPERTY()
	TArray<EEasySessionResult> FailureResults;

	/** Bind to the On Success pin of a node that reports only a result. */
	UFUNCTION()
	void HandleSuccess(EEasySessionResult Result, const FString& ErrorMessage)
	{
		SuccessResults.Add(Result);
	}

	/** Bind to the On Failure pin of a node that reports only a result. */
	UFUNCTION()
	void HandleFailure(EEasySessionResult Result, const FString& ErrorMessage)
	{
		FailureResults.Add(Result);
	}

	/** Bind to the On Success pin of Find Easy Sessions. */
	UFUNCTION()
	void HandleFindSuccess(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
	{
		SuccessResults.Add(Result);
	}

	/** Bind to the On Failure pin of Find Easy Sessions. */
	UFUNCTION()
	void HandleFindFailure(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
	{
		FailureResults.Add(Result);
	}

	/** Bind to the On Success pin of Read Easy Friends. */
	UFUNCTION()
	void HandleFriendsSuccess(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionFriend>& Friends)
	{
		SuccessResults.Add(Result);
	}

	/** Bind to the On Failure pin of Read Easy Friends. */
	UFUNCTION()
	void HandleFriendsFailure(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionFriend>& Friends)
	{
		FailureResults.Add(Result);
	}

	/** Bind to the On Success pin of Find Easy Friend Sessions. */
	UFUNCTION()
	void HandleFriendSessionsSuccess(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasyFriendSession>& FriendSessions)
	{
		SuccessResults.Add(Result);
	}

	/** Bind to the On Failure pin of Find Easy Friend Sessions. */
	UFUNCTION()
	void HandleFriendSessionsFailure(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasyFriendSession>& FriendSessions)
	{
		FailureResults.Add(Result);
	}

	/** @return How many times a pin fired, so a test can catch a node that fires twice as well as one that never fires. */
	int32 TotalFired() const { return SuccessResults.Num() + FailureResults.Num(); }

	/** Forget everything seen so far, so one listener can serve a list of nodes. */
	void Reset()
	{
		SuccessResults.Reset();
		FailureResults.Reset();
	}

	/** Which pins fired and with what, for a failure message that names what actually arrived. */
	FString Describe() const
	{
		FString Out;
		for (const EEasySessionResult Result : SuccessResults)
		{
			Out += FString::Printf(TEXT("OnSuccess=%s "), *EasySession::ResultToString(Result));
		}
		for (const EEasySessionResult Result : FailureResults)
		{
			Out += FString::Printf(TEXT("OnFailure=%s "), *EasySession::ResultToString(Result));
		}
		return Out.IsEmpty() ? TEXT("no pin fired") : Out;
	}
};
