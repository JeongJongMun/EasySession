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
	 * Turn it off to keep the player in the map they are in and travel them yourself.
	 * The reason is kept and can be read on the menu with Consume Pending Easy Disconnect Info.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Recovery")
	bool bAutoReturnToMenuOnDisconnect = true;

	/**
	 * Automatically join the session or party when the player accepts an invite from the platform overlay (e.g. Steam).
	 * Turn it off to only receive the On Session Invite Accepted event and call Join Easy Session or Join Easy Party yourself.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Invites")
	bool bAutoJoinAcceptedInvites = true;

	/**
	 * Whether an accepted session invite is joined automatically while this player is already in a game session.
	 * The join runs as Join Easy Session does, so the current session is destroyed once the new host approved the join.
	 * A host whose match has not started brings its players along, and the host of a match in progress fails with Session Already Exists.
	 * An accepted party invite is never joined automatically during a game session.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Invites", meta = (EditCondition = "bAutoJoinAcceptedInvites"))
	bool bAcceptInvitesWhileInSession = false;

	/**
	 * Restore the party in the first map without a game session that this player reaches after the match the party entered.
	 * The leader creates the party again when that map loads, and every member searches for it until Party Restore Wait Seconds runs out.
	 * Calling Create, Join or Leave Easy Party, Create or Join Easy Session, or Start Easy Matchmaking stops it, because the player chose something else.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Party")
	bool bRestorePartyAfterMatch = true;

	/**
	 * Seconds the restore after a match keeps trying before On Party Left fires with Connection Lost.
	 * A member searches for the leader's party this long, because the leader may stay in the match longer.
	 * A leader whose create failed retries for the same time.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Party", meta = (ClampMin = "0", EditCondition = "bRestorePartyAfterMatch"))
	float PartyRestoreWaitSeconds = 120.0f;

	/**
	 * Seconds a member keeps connecting to the leader after the connection closed without a reason.
	 * A leader that changes maps closes every connection while its next map loads.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Party", meta = (ClampMin = "0"))
	float PartyReconnectSeconds = 15.0f;
};
