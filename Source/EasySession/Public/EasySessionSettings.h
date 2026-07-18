// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EasySessionTypes.h"
#include "EasySessionSettings.generated.h"

/**
 * Project-wide settings for the EasySession plugin.
 * Found in Project Settings > Plugins > EasySession.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "EasySession"))
class EASYSESSION_API UEasySessionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	//~ Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	//~ End UDeveloperSettings Interface

	/**
	 * Automatically create and advertise a session when running as a dedicated server.
	 * The session is created with the Dedicated Server Host Params below.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Dedicated Server")
	bool bAutoHostOnDedicatedServer = true;

	/**
	 * Host params used when a dedicated server automatically creates its session.
	 * Map Name is ignored - the server keeps the map it was launched with.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Dedicated Server")
	FEasySessionHostParams DedicatedServerHostParams;
};
