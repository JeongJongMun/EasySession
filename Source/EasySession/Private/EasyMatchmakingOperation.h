// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionOperation.h"
#include "UObject/StrongObjectPtr.h"

class UEasyMatchmakingPolicy;

/**
 * The matchmaking run as a queue operation.
 *
 * The policy keeps driving its own steps through the subsystem's public API.
 * This object only keeps the policy alive and reports the run's state to the queue.
 */
class FEasyMatchmakingOperation final : public IEasySessionOperation
{
public:

	explicit FEasyMatchmakingOperation(UEasyMatchmakingPolicy& InPolicy);

	//~ Begin IEasySessionOperation interface
	virtual EEasySessionOperationType GetType() const override { return EEasySessionOperationType::Matchmaking; }
	virtual bool CountsAsBusy() const override { return true; }
	virtual void Cancel() override;
	virtual FString DescribeProgress() const override;
	//~ End IEasySessionOperation interface

	/** @return The policy running this matchmaking. */
	UEasyMatchmakingPolicy* GetPolicy() const { return Policy.Get(); }

private:

	/** Keeps the policy alive for the run. */
	TStrongObjectPtr<UEasyMatchmakingPolicy> Policy;
};
