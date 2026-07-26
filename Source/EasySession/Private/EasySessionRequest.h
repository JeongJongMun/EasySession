// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"

/**
 * A single queued session operation.
 *
 * Requests run strictly one at a time. The online subsystem already rejects a
 * second call of the *same* kind (a session name can only be created, joined or
 * destroyed once, and searches refuse to overlap), so the queue is not there to
 * prevent that. What it prevents is *different* operations crossing: Steam's
 * DestroySession, for example, only refuses while a destroy is already running -
 * it will happily tear down a session whose create is still in flight. Serializing
 * also turns "rejected because something else was running" into "runs next", which
 * is the behavior a caller expects from a beginner-friendly API.
 *
 * Each request owns its own deadline: the online service is not required to ever
 * call back (Steam tasks do not implement CancelWhenTimeout), so without a
 * deadline a silent service would stall every request behind it.
 */
class FEasySessionRequest
{
public:

	enum class EType : uint8
	{
		Create,
		Find,
		Join,
		Destroy,
		Update,
		Start,
		End
	};

	explicit FEasySessionRequest(EType InType)
		: Type(InType)
	{
	}

	/** Human readable name of the operation, for logs and status output. */
	const TCHAR* GetTypeName() const
	{
		switch (Type)
		{
			case EType::Create:		return TEXT("Create");
			case EType::Find:		return TEXT("Find");
			case EType::Join:		return TEXT("Join");
			case EType::Destroy:	return TEXT("Destroy");
			case EType::Update:		return TEXT("Update");
			case EType::Start:		return TEXT("Start");
			case EType::End:		return TEXT("End");
			default:				return TEXT("Unknown");
		}
	}

	/** Stamp the start time and freeze the deadline for this run. */
	void MarkStarted(double NowSeconds, float ConfiguredTimeoutSeconds)
	{
		StartTimeSeconds = NowSeconds;
		TimeoutSeconds = ComputeTimeoutSeconds(ConfiguredTimeoutSeconds);
	}

	/** How long this request has been running. */
	double GetElapsedSeconds(double NowSeconds) const
	{
		return NowSeconds - StartTimeSeconds;
	}

	/** Whether the deadline has passed. Always false when the timeout is disabled. */
	bool HasTimedOut(double NowSeconds) const
	{
		return TimeoutSeconds > 0.0 && GetElapsedSeconds(NowSeconds) >= TimeoutSeconds;
	}

	/**
	 * Deadline for this request. Searching runs its own timeout inside the online
	 * service, so that budget is added on top or the watchdog would fire on a
	 * perfectly healthy search. 0 (or a non-positive setting) disables the deadline.
	 */
	double ComputeTimeoutSeconds(float ConfiguredTimeoutSeconds) const
	{
		if (ConfiguredTimeoutSeconds <= 0.0f)
		{
			return 0.0;
		}

		if (Type == EType::Find)
		{
			return ConfiguredTimeoutSeconds + FMath::Max(0.0f, SearchParams.TimeoutSeconds);
		}

		return ConfiguredTimeoutSeconds;
	}

	/** Whether this request may have left a session behind when it timed out. */
	bool CouldHaveCreatedSession() const
	{
		return Type == EType::Create || Type == EType::Join;
	}

	EType Type;

	/** Time the request started executing. */
	double StartTimeSeconds = 0.0;

	/** Deadline for this run, frozen when the request starts. 0 = no deadline. */
	double TimeoutSeconds = 0.0;

	FEasySessionHostParams HostParams;
	FEasySessionSearchParams SearchParams;
	FEasySessionSearchResult JoinTarget;
	bool bTravelOnSuccess = true;
	FString JoinPassword;
	FString JoinTravelOptions;
	FEasySessionCompleteDelegate OnComplete;
	FEasySessionFindCompleteDelegate OnFindComplete;
};
