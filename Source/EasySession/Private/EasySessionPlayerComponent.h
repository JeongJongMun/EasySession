// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/OnlineReplStructs.h"
#include "EasySessionPlayerComponent.generated.h"

class UEasySessionSubsystem;

/**
 * UEasySessionPlayerComponent is the host's channel to one player: a Client RPC on it reaches that player only.
 * AEasySessionStateActor carries what every player receives, and this component carries what one player receives.
 *
 * The host adds one to every player controller of its world, whatever the controller's class, so the game needs no parent class.
 * It holds no state, so a new controller after a travel only needs a new component.
 */
UCLASS(NotBlueprintable, Transient)
class UEasySessionPlayerComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	/** Replicated by default, because the host adds it to a controller at runtime. */
	UEasySessionPlayerComponent();

	/** Server: join the session of the host that holds a reservation for this player. */
	UFUNCTION(Client, Reliable)
	void ClientFollowHost(const FUniqueNetIdRepl& HostId, bool bLANQuery);

	/** Server: tell this player that the host kicked them from the session, and why. The host closes the connection right after. */
	UFUNCTION(Client, Reliable)
	void ClientKicked(const FText& Reason);

private:

	/** @return The subsystem of the game instance this component's world belongs to. Null while the world has none. */
	UEasySessionSubsystem* GetSubsystem() const;
};
