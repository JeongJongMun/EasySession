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
	 * Automatically destroy the session and travel to the project's Game Default Map when the connection to a session is lost or traveling to a session fails.
	 * Turn it off to keep the player in place and handle it yourself.
	 * The reason is kept and can be read on the menu with Consume Last Easy Disconnect Info.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Recovery")
	bool bAutoReturnToMenuOnDisconnect = true;

	/**
	 * Automatically join the session when the player accepts an invite from the platform overlay (e.g. Steam).
	 * Turn it off to only receive the On Session Invite Accepted event and handle joining yourself.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Invites")
	bool bAutoJoinAcceptedInvites = true;

	/**
	 * Whether an invite can be accepted while this player is already in a session.
	 * With this on, one click in the platform overlay destroys the session they are in before joining the invited one.
	 * The On Session Invite Accepted event still fires either way, so the game can ask the player first and then join.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Invites")
	bool bAcceptInvitesWhileInSession = false;

	/**
	 * How long a session request may wait for the online subsystem before it completes with Timeout and the queue moves on.
	 * The online subsystem is not required to ever call back, so without this one request could block every request behind it.
	 * A search may replace it with its own Timeout Override Seconds.
	 * Timeout means the outcome is unknown. A session the request still creates afterwards is destroyed, so the next request starts clean. 0 waits forever.
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
	 * Initial Map Name is ignored. The server keeps the map it was launched with.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Dedicated Server")
	FEasySessionHostParams DedicatedServerHostParams;
};
