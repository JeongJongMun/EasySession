// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"

/**
 * A single queued session operation.
 *
 * Requests run strictly one at a time.
 * The online subsystem already rejects a second call of the same kind.
 * A session name can only be created, joined or destroyed once, and searches refuse to overlap, so the queue is not there for that.
 * What it prevents is two different operations overlapping.
 * Steam's DestroySession only refuses while another destroy is running, so it will destroy a session whose create has not finished.
 * Running them in order also turns "rejected because another operation was running" into "runs next", which is what a caller expects from a beginner-friendly API.
 *
 * Each request carries its own deadline.
 * The online service is not required to ever call back, and Steam tasks do not implement CancelWhenTimeout.
 * Without a deadline a silent service would stall every request behind it.
 */
class FEasySessionRequest
{
public:

	/** Which session operation a request performs. */
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

	/** Build a request of the given type. The caller fills in the payload fields it needs. */
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
	 * Deadline for this request.
	 * A search runs its own timeout inside the online service, so that time is added on top.
	 * Otherwise the watchdog would fire on a search that is still working normally.
	 * 0, or a non-positive setting, disables the deadline.
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

	/** Which operation this request performs. Decides which of the payload fields below are read. */
	EType Type;

	/**
	 * The session every step of this request acts on, read by the execution, the completion filter and the timeout cleanup alike.
	 * Set once when the request is enqueued and constant afterwards.
	 * It holds one value today, because the plugin hosts a single session per process.
	 */
	FName SessionName;

	/** Time the request started executing. */
	double StartTimeSeconds = 0.0;

	/** Deadline for this run, frozen when the request starts. 0 = no deadline. */
	double TimeoutSeconds = 0.0;

	/** Create and Update: the settings to advertise for the session. */
	FEasySessionHostParams HostParams;

	/** Find: the filters to search with. */
	FEasySessionSearchParams SearchParams;

	/** Join: the session to join, as returned by a search. */
	FEasySessionSearchResult JoinTarget;

	/** Join: whether to travel to the host once the online service accepts the join. */
	bool bTravelOnSuccess = true;

	/** Join: the password sent to the host's approval beacon, and carried in the travel URL for PreLogin. */
	FString JoinPassword;

	/** Join: extra options appended to the client travel URL. */
	FString JoinTravelOptions;

	/** Called when the request finishes. Find reports through OnFindComplete instead. */
	FEasySessionCompleteDelegate OnComplete;

	/** Find: called with the filtered results when the search finishes. */
	FEasySessionFindCompleteDelegate OnFindComplete;
};
