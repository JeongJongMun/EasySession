// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "EasySessionNodeBase.generated.h"

class UEasySessionSubsystem;

/**
 * Base class for EasySession async Blueprint nodes.
 * Nodes are thin wrappers.
 * All logic lives in the EasySessionSubsystem.
 */
UCLASS(Abstract)
class EASYSESSION_API UEasySessionNodeBase : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

protected:

	/** Resolve the subsystem from the world context below, or null when that context leads to no game instance. */
	UEasySessionSubsystem* GetSubsystem() const;

	/** The world context object in which this node runs. */
	UPROPERTY()
	TObjectPtr<UObject> WorldContext;
};
