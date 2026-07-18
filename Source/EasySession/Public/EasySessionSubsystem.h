// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EasySessionSubsystem.generated.h"

/**
 * Core subsystem of the EasySession plugin.
 * Automatically created for each game instance - no custom GameInstance class required.
 * All session and matchmaking operations of the plugin are routed through this subsystem.
 */
UCLASS()
class EASYSESSION_API UEasySessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

	/** Get the name of the online subsystem currently in use (e.g. NULL, STEAM, EOS). */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	FName GetOnlineSubsystemName() const;

	/** Check whether an online subsystem is available and its session interface is valid. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	bool IsOnlineSubsystemAvailable() const;
};
