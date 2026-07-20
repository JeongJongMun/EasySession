// Copyright Langerak. All Rights Reserved.

#include "EasySessionClientComponent.h"

#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UEasySessionClientComponent::UEasySessionClientComponent()
{
	SetIsReplicatedByDefault(true);
}

void UEasySessionClientComponent::ClientReturnToMenu_Implementation(const FText& Reason)
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UEasySessionSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;

	if (Subsystem != nullptr)
	{
		Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::HostEndedSession, Reason);
	}
}
