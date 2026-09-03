// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionOperation.h"
#include "UObject/StrongObjectPtr.h"

class UEasyMatchmakingPolicy;

/**
 * Registers a matchmaking run with the request queue as the operation it is.
 *
 * The policy keeps driving its own steps through the subsystem's public API.
 * This adapter only holds the policy alive and answers the queue's questions about the run.
 */
class FEasyMatchmakingOperation final : public IEasySessionOperation
{
public:

	explicit FEasyMatchmakingOperation(UEasyMatchmakingPolicy& InPolicy);

	//~ Begin IEasySessionOperation interface
	EEasySessionOperationType GetType() const override { return EEasySessionOperationType::Matchmaking; }
	bool CountsAsBusy() const override { return true; }
	void Cancel() override;
	FString DescribeProgress() const override;
	//~ End IEasySessionOperation interface

	/** @return The policy running this matchmaking. */
	UEasyMatchmakingPolicy* GetPolicy() const { return Policy.Get(); }

private:

	/** Keeps the policy alive for the run. It used to be a UPROPERTY on the subsystem. */
	TStrongObjectPtr<UEasyMatchmakingPolicy> Policy;
};
