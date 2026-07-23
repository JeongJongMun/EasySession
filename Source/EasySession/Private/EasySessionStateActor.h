// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"
#include "GameFramework/Info.h"
#include "EasySessionStateActor.generated.h"

/**
 * Session-wide replicated state, spawned and managed by the hosting subsystem.
 *
 * The session lifecycle state lives on the host; clients only hold local
 * best-effort copies. Anything every player must agree on is replicated here as
 * state (not pushed as one-shot events), so late joiners, rejoiners and map
 * travels always converge on the host's truth. The actor deliberately avoids
 * every framework class (GameState, PlayerController, GameMode) so no base
 * class is forced on the game and player controller swaps cannot lose it.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class AEasySessionStateActor : public AInfo
{
	GENERATED_BODY()

public:

	AEasySessionStateActor();

	/** Server: replicate the host's session lifecycle state to every client. */
	void SetHostSessionState(EEasySessionState NewState);

	/** Server: send every remote player back to the menu with a reason. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastReturnToMenu(const FText& Reason);

	//~ Begin AActor Interface
	virtual void PostNetInit() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor Interface

private:

	/** Hand the replicated state to the local subsystem. */
	void PushStateToSubsystem();

	UFUNCTION()
	void OnRep_HostSessionState();

	/** The host's authoritative session lifecycle state. */
	UPROPERTY(ReplicatedUsing = OnRep_HostSessionState)
	EEasySessionState HostSessionState = EEasySessionState::NoSession;
};
