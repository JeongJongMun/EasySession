// Copyright (c) 2026 Langerak.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/OnlineSession.h"
#include "EasySessionOnlineSession.generated.h"

/**
 * UEasySessionOnlineSession receives the disconnects the engine reports to the GameInstance's online session, and passes them to the EasySession subsystem.
 * The engine reports a lost connection, a failed travel and a ReturnToMainMenu call here, in place of traveling to the Game Default Map itself.
 * It is created by the GameInstance that returns it from GetOnlineSessionClass, and holds no state of its own.
 *
 * @see UEasySessionGameInstance
 */
UCLASS()
class EASYSESSION_API UEasySessionOnlineSession : public UOnlineSession
{
	GENERATED_BODY()

public:

	//~ Begin UOnlineSession Interface
	virtual void HandleDisconnect(UWorld* World, UNetDriver* NetDriver) override;
	//~ End UOnlineSession Interface
};
