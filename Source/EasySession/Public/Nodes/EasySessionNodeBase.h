// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "EasySessionNodeBase.generated.h"

class UEasySessionSubsystem;

/**
 * Base class for EasySession async Blueprint nodes.
 * Nodes are thin wrappers - all logic lives in the EasySessionSubsystem.
 */
UCLASS(Abstract)
class EASYSESSION_API UEasySessionNodeBase : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

protected:

	/** Resolve the EasySession subsystem from the stored world context. Returns null if unavailable. */
	UEasySessionSubsystem* GetEasySessionSubsystem() const;

	/** The world context object in which this node runs. */
	UPROPERTY()
	TObjectPtr<UObject> NodeWorldContext;
};
