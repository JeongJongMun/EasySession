// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

/** Which multi-step operation an entry in the queue's operation list is. One of each type runs at a time. */
enum class EEasySessionOperationType : uint8
{
	Matchmaking,
	FriendSearch
};

/**
 * A multi-step operation the request queue keeps track of.
 *
 * An operation is made of several requests, submitted one at a time through the subsystem's public API as each answer arrives.
 * The queue does not run the steps.
 * It records that the operation exists, so that being busy, being already running, cancellation and the status line have one owner.
 *
 * Operations are shared references.
 * A step's completion delegate is bound with CreateSP, so it unbinds itself when the operation dies.
 */
class IEasySessionOperation : public TSharedFromThis<IEasySessionOperation>
{
public:

	virtual ~IEasySessionOperation() = default;

	/** @return Which operation this is. */
	virtual EEasySessionOperationType GetType() const = 0;

	/** @return Whether the subsystem is busy while this runs. Matchmaking is. A friend search only reads, so it is not. */
	virtual bool CountsAsBusy() const = 0;

	/** Stop the operation. The running step's answer is ignored, the operation completes as Canceled and ends itself on the queue. */
	virtual void Cancel() = 0;

	/** @return One fragment for the status line, e.g. "Matchmaking (Searching, pass 2)" or "Friend search 3/7". */
	virtual FString DescribeProgress() const = 0;
};

namespace EasySession
{
	/** Name of an operation type, for logs. */
	inline const TCHAR* OperationTypeToString(EEasySessionOperationType Type)
	{
		switch (Type)
		{
			case EEasySessionOperationType::Matchmaking: return TEXT("Matchmaking");
			case EEasySessionOperationType::FriendSearch: return TEXT("FriendSearch");
			default: return TEXT("Unknown");
		}
	}
}
