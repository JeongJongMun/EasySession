// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EasySessionClientComponent.generated.h"

/**
 * Internal component the EasySessionSubsystem attaches to player controllers on the server.
 * Carries session RPCs (e.g. return to menu with a reason) without requiring the game
 * to use a custom PlayerController class.
 */
UCLASS(NotBlueprintable)
class UEasySessionClientComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UEasySessionClientComponent();

	/** Ask the owning client to leave the session and return to the menu, preserving the reason. */
	UFUNCTION(Client, Reliable)
	void ClientReturnToMenu(const FText& Reason);

	/** Mirror the host's StartSession locally so the client's session state matches. */
	UFUNCTION(Client, Reliable)
	void ClientSessionStarted();

	/** Mirror the host's EndSession locally so the client's session state matches. */
	UFUNCTION(Client, Reliable)
	void ClientSessionEnded();
};
