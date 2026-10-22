// Copyright (c) 2026 Langerak.

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
	 * Automatically join the session or party when the player accepts an invite from the platform overlay (e.g. Steam).
	 * Turn it off to only receive the On Session Invite Accepted event and call Join Easy Session yourself.
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
	 * Characters in a generated join code, for a session or a party.
	 * A shorter code is quicker to read aloud and type, and a longer one is harder to guess.
	 * Changing it affects codes generated after the change, and a session keeps the code it was created with.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Join Code", meta = (ClampMin = "4", ClampMax = "12"))
	int32 JoinCodeLength = 6;

	/**
	 * Seconds a member keeps connecting to the host after the connection closed without a reason.
	 * A host that changes maps closes every connection while its next map loads.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Party", meta = (ClampMin = "0"))
	float PartyReconnectSeconds = 15.0f;
};
