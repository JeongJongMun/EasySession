// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "EasySessionGameInstance.generated.h"

/**
 * UEasySessionGameInstance gives the game the EasySession online session, which lets EasySession destroy a lost session before the engine travels to the menu.
 * Set it as the Game Instance Class, or as the parent class of the project's GameInstance Blueprint.
 * A C++ GameInstance can return UEasySessionOnlineSession from its own GetOnlineSessionClass instead.
 *
 * Keep in mind that without it every EasySession session and party request fails with NoEasySessionGameInstance.
 *
 * @see UEasySessionOnlineSession
 */
UCLASS(Blueprintable)
class EASYSESSION_API UEasySessionGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:

	//~ Begin UGameInstance Interface
	virtual TSubclassOf<UOnlineSession> GetOnlineSessionClass() override;
	//~ End UGameInstance Interface
};
