// Copyright Langerak. All Rights Reserved.

#include "EasySessionClientComponent.h"

#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UEasySessionClientComponent::UEasySessionClientComponent()
{
	SetIsReplicatedByDefault(true);
}

namespace
{
	UEasySessionSubsystem* GetEasySubsystem(const UActorComponent& Component)
	{
		const UWorld* World = Component.GetWorld();
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;
	}
}

void UEasySessionClientComponent::ClientReturnToMenu_Implementation(const FText& Reason)
{
	if (UEasySessionSubsystem* Subsystem = GetEasySubsystem(*this))
	{
		Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::HostEndedSession, Reason);
	}
}

void UEasySessionClientComponent::ClientSessionStarted_Implementation()
{
	if (UEasySessionSubsystem* Subsystem = GetEasySubsystem(*this))
	{
		Subsystem->StartEasySession();
	}
}

void UEasySessionClientComponent::ClientSessionEnded_Implementation()
{
	if (UEasySessionSubsystem* Subsystem = GetEasySubsystem(*this))
	{
		Subsystem->EndEasySession();
	}
}
