// Copyright (c) 2026 Langerak.

#include "EasySessionOnlineSession.h"

#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"

void UEasySessionOnlineSession::HandleDisconnect(UWorld* World, UNetDriver* NetDriver)
{
	// The GameInstance creates the online session before its subsystems and clears it before them, so a disconnect can arrive while no subsystem exists.
	const UGameInstance* GameInstance = Cast<UGameInstance>(GetOuter());
	UEasySessionSubsystem* Subsystem = GameInstance != nullptr ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;
	if (Subsystem == nullptr)
	{
		Super::HandleDisconnect(World, NetDriver);
		return;
	}

	Subsystem->HandleDisconnect(World, NetDriver);
}
