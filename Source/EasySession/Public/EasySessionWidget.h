// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EasySessionTypes.h"
#include "EasySessionWidget.generated.h"

class UEasySessionSubsystem;

/**
 * UEasySessionWidget is a user widget that receives every event of the Easy Session Subsystem as an overridable event.
 * Reparent a widget to it and override the events it needs, such as On Busy Changed or On Party Members Changed.
 * The events are bound while the widget is constructed, so a widget that is not on screen receives none.
 *
 * Keep in mind that a widget whose parent class cannot change binds the subsystem events itself with Assign nodes instead.
 */
UCLASS(Abstract, Blueprintable)
class EASYSESSION_API UEasySessionWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/**
	 * The subsystem whose events this widget receives.
	 * Null before the widget has a game instance.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	UEasySessionSubsystem* GetEasySessionSubsystem() const;

protected:

	//~ Begin UUserWidget Interface
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget Interface

	/** Is Easy Session Busy changed, with the new value. */
	UFUNCTION(BlueprintImplementableEvent, Category = "EasySession|Events")
	void OnBusyChanged(bool bBusy);

	/** The session's lifecycle state changed, on the host and on every client. */
	UFUNCTION(BlueprintImplementableEvent, Category = "EasySession|Events")
	void OnSessionStateChanged(EEasySessionState OldState, EEasySessionState NewState);

	/** The advertised session settings changed, and Get Easy Session Settings already returns the new values. */
	UFUNCTION(BlueprintImplementableEvent, Category = "EasySession|Events")
	void OnSessionSettingsChanged();

	/**
	 * A player joined or left the session, or changed whether they are ready.
	 * Get Easy Session Player Infos already returns the new list.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "EasySession|Events")
	void OnSessionPlayersChanged();

	/** Something failed outside any node's result, such as a dropped connection, a failed travel or an invite that was not joined. */
	UFUNCTION(BlueprintImplementableEvent, Category = "EasySession|Events")
	void OnSessionFailure(const FString& Reason);

	/** The player accepted an invite in the platform overlay, to a session or to a party. */
	UFUNCTION(BlueprintImplementableEvent, Category = "EasySession|Events")
	void OnSessionInviteAccepted(const FEasySessionSearchResult& Session);

	/** A matchmaking run was accepted. */
	UFUNCTION(BlueprintImplementableEvent, Category = "EasySession|Events")
	void OnMatchmakingStarted();

	/** The state of the running matchmaking changed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "EasySession|Events")
	void OnMatchmakingStateChanged(EEasyMatchmakingState OldState, EEasyMatchmakingState NewState);

	/**
	 * The running matchmaking changed state, or another second of it passed.
	 * Elapsed Seconds counts from the start of the run.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "EasySession|Events")
	void OnMatchmakingUpdated(EEasyMatchmakingState State, int32 ElapsedSeconds);

	/** A matchmaking run completed, a canceled one included. */
	UFUNCTION(BlueprintImplementableEvent, Category = "EasySession|Events")
	void OnMatchmakingComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/**
	 * A member joined or left the party, or changed whether they are ready.
	 * Get Easy Party Members already returns the new list.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "EasySession|Events")
	void OnPartyMembersChanged();

	/**
	 * The local player is no longer in the party.
	 * Reason Text is the leader's reason for a kick, and a message for the player otherwise.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "EasySession|Events")
	void OnPartyLeft(EEasyPartyLeaveReason Reason, const FText& ReasonText);
};
