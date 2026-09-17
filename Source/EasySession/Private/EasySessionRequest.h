// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"

/**
 * A single queued call to the online subsystem.
 *
 * Requests run strictly one at a time.
 * The online subsystem already refuses a second call of the same kind, so the queue exists to keep two different calls from overlapping.
 * Steam's DestroySession, for one, only refuses while another destroy is running, so it would destroy a session whose create has not finished.
 * Running requests in order also turns "refused because another call was running" into "runs next", which is what a beginner expects.
 *
 * Each request carries its own deadline.
 * The online subsystem is not required to ever call back, and Steam tasks do not implement CancelWhenTimeout.
 * Without a deadline a request that never completes would block every request behind it.
 */
class FEasySessionRequest
{
public:

	/** Which online subsystem call a request makes. */
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

	/** Human readable name of the request type, for logs and status output. */
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

	/** Record the start time and fix the deadline. */
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
	 * Deadline for this request: the configured timeout, which a search may replace with its own Timeout Override Seconds.
	 * 0 disables the deadline.
	 */
	double ComputeTimeoutSeconds(float ConfiguredTimeoutSeconds) const
	{
		if (Type == EType::Find && SearchParams.TimeoutOverrideSeconds > 0.0f)
		{
			return SearchParams.TimeoutOverrideSeconds;
		}

		return FMath::Max(0.0f, ConfiguredTimeoutSeconds);
	}

	/** Whether this request may have left a session behind when it timed out. */
	bool CouldHaveCreatedSession() const
	{
		return Type == EType::Create || Type == EType::Join;
	}

	/** Which call this request makes. Decides which of the payload fields below are read. */
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

	/**
	 * Whether the requester canceled this request.
	 * It keeps the active slot until the online subsystem completes it, does not count as busy, and its late completion is dropped.
	 */
	bool bCanceled = false;

	/** Create: the session to advertise, and how to open the server for it. */
	FEasySessionHostParams HostParams;

	/** Update: the settings to advertise in place of the current ones. */
	FEasySessionSettings Settings;

	/** Find: the filters to search with, including the targeted-query ids. */
	FEasySessionSearchParams SearchParams;

	/** Join: the session to join, as returned by a search. */
	FEasySessionSearchResult JoinTarget;

	/** Join: the password sent to the host's approval beacon, and carried in the travel URL for PreLogin. */
	FString JoinPassword;

	/** Join: extra options appended to the client travel URL. */
	FString JoinTravelOptions;

	/** Called when the request completes. A Find request completes through OnFindComplete instead. */
	FEasySessionCompleteDelegate OnComplete;

	/** Find: called with the filtered results when the search finishes. */
	FEasySessionFindCompleteDelegate OnFindComplete;
};
