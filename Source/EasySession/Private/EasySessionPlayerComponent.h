// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/OnlineReplStructs.h"
#include "EasySessionPlayerComponent.generated.h"

class UEasySessionSubsystem;

/**
 * UEasySessionPlayerComponent is what the session knows about one player, on that player's PlayerState.
 * A PlayerState replicates to every player and belongs to its own player's connection.
 * So a Client RPC on this component reaches that player only, a Server RPC comes from that player only, and its properties reach everyone.
 * AEasySessionStateActor carries what every player receives, and this component carries what one player receives or sends.
 *
 * The host adds one to every PlayerState of its world, whatever the PlayerState's class, so the game needs no parent class.
 * A travel gives every player a new PlayerState, and the new one gets a new component, so the ready state starts unset in each map.
 */
UCLASS(NotBlueprintable, Transient)
class UEasySessionPlayerComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	/** Replicated by default, because the host adds it to a PlayerState at runtime. */
	UEasySessionPlayerComponent();

	/** Server: join the session of the host that holds a reservation for this player. */
	UFUNCTION(Client, Reliable)
	void ClientFollowHost(const FUniqueNetIdRepl& HostId, bool bLANQuery);

	/** Server: tell this player that the host kicked them from the session, and why. The host closes the connection right after. */
	UFUNCTION(Client, Reliable)
	void ClientKicked(const FText& Reason);

	/** Change whether this player is ready: directly on the host, and through the host on the player's own client. */
	void SetReady(bool bInReady);

	/** @return Whether this player is ready. */
	bool IsReady() const { return bReady; }

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent Interface

private:

	/** Client: the player asks the host to change whether they are ready. */
	UFUNCTION(Server, Reliable)
	void ServerSetReady(bool bInReady);

	/** The ready state arrived on a client. */
	UFUNCTION()
	void OnRep_Ready();

	/** @return The subsystem of the game instance this component's world belongs to. Null while the world has none. */
	UEasySessionSubsystem* GetSubsystem() const;

	/** Is this player ready. Only the host writes it. */
	UPROPERTY(ReplicatedUsing = OnRep_Ready)
	bool bReady = false;
};
