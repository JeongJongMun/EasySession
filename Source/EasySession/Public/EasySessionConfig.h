// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
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
	 * Travel to the project's Game Default Map when the connection to a session is lost or traveling to a session fails.
	 * The lost session is destroyed either way, because it is gone for this player already.
	 * This setting only decides whether the travel follows.
	 * Turn it off to keep the player in the map they are in and travel them yourself.
	 * The reason is kept and can be read on the menu with Consume Pending Easy Disconnect Info.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Recovery")
	bool bAutoReturnToMenuOnDisconnect = true;

	/**
	 * Automatically join the session when the player accepts an invite from the platform overlay (e.g. Steam).
	 * Turn it off to only receive the On Session Invite Accepted event and join with Join Easy Session yourself.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Invites")
	bool bAutoJoinAcceptedInvites = true;

	/**
	 * Whether an accepted invite is joined automatically while this player is already in a session.
	 * With this on, one click in the platform overlay destroys the session they are in before joining the invited one.
	 * A host that leaves this way takes its session with it, and its players are told why before their connection closes.
	 * This decides the automatic join only.
	 * A Join Easy Session the game calls itself leaves the current session once the new host approved the join.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Invites", meta = (EditCondition = "bAutoJoinAcceptedInvites"))
	bool bAcceptInvitesWhileInSession = false;
};
