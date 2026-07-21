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
	 * Automatically clean up the session and travel back to Return To Menu Map when
	 * the connection to a session is lost or traveling to a session fails.
	 * The reason is preserved and can be read on the menu with Consume Last Disconnect Info.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Recovery")
	bool bAutoReturnToMenuOnDisconnect = true;

	/**
	 * Map to travel to after a disconnect (e.g. /Game/Maps/MainMenu).
	 * Leave empty to fall back to the engine's default behavior (game default map).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Recovery", meta = (EditCondition = "bAutoReturnToMenuOnDisconnect"))
	FString ReturnToMenuMap;

	/**
	 * Automatically join the session when the player accepts an invite from the
	 * platform overlay (e.g. Steam). Disable to only receive the
	 * On Session Invite Accepted event and handle joining yourself.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Invites")
	bool bAutoJoinAcceptedInvites = true;

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
