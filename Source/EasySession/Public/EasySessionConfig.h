// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EasySessionTypes.h"
#include "EasySessionConfig.generated.h"

/**
 * Project-wide settings for the EasySession plugin.
 * Found in Project Settings > Plugins > EasySession.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "EasySession Settings", ToolTip = "Project-wide settings for the EasySession plugin."))
class EASYSESSION_API UEasySessionConfig : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	//~ Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	//~ End UDeveloperSettings Interface

	/**
	 * Automatically clean up the session and return to the main menu, the project's Game Default Map.
	 * Runs when the connection to a session is lost, or when traveling to a session fails.
	 * Disable to keep the player in place and handle it yourself.
	 * The reason is preserved and can be read on the menu with Consume Last Disconnect Info.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Recovery")
	bool bAutoReturnToMenuOnDisconnect = true;

	/**
	 * Automatically join the session when the player accepts an invite from the platform overlay (e.g. Steam).
	 * Disable to only receive the On Session Invite Accepted event and handle joining yourself.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Invites")
	bool bAutoJoinAcceptedInvites = true;

	/**
	 * Whether an invite can be accepted while this player is already in a session. False by default.
	 * With this on, one click in the platform overlay destroys the session they are in before joining the invited one.
	 * The On Session Invite Accepted event still fires either way, so the game can ask the player first and then join.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Invites")
	bool bAcceptInvitesWhileInSession = false;

	/**
	 * How long a session request may wait for the online service before it is failed with the Timeout result and the queue moves on.
	 * Online services are not required to ever call back, and Steam's async tasks have no timeout of their own.
	 * Without this, a silent service would stall every request behind it.
	 * Searching adds its own Timeout Seconds on top of this value.
	 * A timeout means the outcome is unknown, not that nothing happened.
	 * If the operation completes after the timeout and leaves a session behind, it is destroyed so the next request starts clean.
	 * Set to 0 to wait forever.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Advanced", meta = (ClampMin = 0.0, UIMin = 0.0))
	float RequestTimeoutSeconds = 30.0f;

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
