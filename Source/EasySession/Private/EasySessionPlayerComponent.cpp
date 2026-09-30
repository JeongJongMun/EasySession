// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionPlayerComponent.h"

#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UEasySessionPlayerComponent::UEasySessionPlayerComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UEasySessionPlayerComponent::ClientFollowHost_Implementation(const FUniqueNetIdRepl& HostId, bool bLANQuery)
{
	if (UEasySessionSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->FollowHost(HostId, bLANQuery);
	}
}

void UEasySessionPlayerComponent::ClientKicked_Implementation(const FText& Reason)
{
	// The lost connection that follows is ignored, because the first reason recorded is the one the menu shows.
	if (UEasySessionSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->HandleDisconnect(EEasyDisconnectReason::Kicked, Reason);
	}
}

UEasySessionSubsystem* UEasySessionPlayerComponent::GetSubsystem() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;
}
