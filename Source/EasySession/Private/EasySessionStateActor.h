// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"
#include "GameFramework/Info.h"
#include "EasySessionStateActor.generated.h"

/**
 * Session-wide replicated state, spawned and managed by the hosting subsystem.
 *
 * The session lifecycle state lives on the host, and a client's own copy is only a guess until the host's value arrives.
 * Anything every player must agree on is replicated here as a variable rather than sent as a one-off event.
 * A player who joins late, rejoins, or travels to a new map therefore still ends up with the host's value.
 * The actor deliberately extends none of the framework classes (GameState, PlayerController, GameMode).
 * That way the plugin forces no base class on the game, and swapping a player controller cannot lose the state.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class AEasySessionStateActor : public AInfo
{
	GENERATED_BODY()

public:

	/** Sets the replication flags this actor needs. Spawned only by the hosting subsystem. */
	AEasySessionStateActor();

	/** Server: replicate the host's session lifecycle state to every client. */
	void SetHostSessionState(EEasySessionState NewState);

	/** Server: replicate the settings a session member is allowed to see, so an update reaches joined players. */
	void SetReplicatedSessionSettings(const FEasySessionReplicatedSettings& NewSettings);

	/** @return The settings payload this actor replicates. For the subsystem and tests. */
	const FEasySessionReplicatedSettings& GetReplicatedSessionSettings() const { return ReplicatedSessionSettings; }

	/** Server: send every remote player back to the menu with a reason. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastReturnToMenu(const FText& Reason);

	//~ Begin AActor Interface
	virtual void PostNetInit() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor Interface

private:

	/** Give the replicated state to the local subsystem, which caches it for its queries. */
	void PushStateToSubsystem();

	/** Give the replicated settings to the local subsystem, which reconciles its session copy. */
	void PushSettingsToSubsystem();

	/** Runs on clients whenever the host's state changes. */
	UFUNCTION()
	void OnRep_HostSessionState();

	/** Runs on clients whenever the host changes the session settings. */
	UFUNCTION()
	void OnRep_ReplicatedSessionSettings();

	/** The host's authoritative session lifecycle state. */
	UPROPERTY(ReplicatedUsing = OnRep_HostSessionState)
	EEasySessionState HostSessionState = EEasySessionState::NoSession;

	/** The host's authoritative session settings, trimmed to what members may see. */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedSessionSettings)
	FEasySessionReplicatedSettings ReplicatedSessionSettings;
};
