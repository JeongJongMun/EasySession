// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionPlayerComponent.h"

#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

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
	// The reason of the lost connection that follows is dropped, because the first reason recorded is the one the menu shows.
	if (UEasySessionSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->HandleDisconnect(EEasyDisconnectReason::Kicked, Reason);
	}
}

void UEasySessionPlayerComponent::SetReady(bool bInReady)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	if (!Owner->HasAuthority())
	{
		ServerSetReady(bInReady);
		return;
	}

	if (bReady == bInReady)
	{
		return;
	}

	bReady = bInReady;

	// A PlayerState sends its properties about once a second, and a ready button should update the player list sooner.
	Owner->ForceNetUpdate();

	// OnRep only runs on clients, so the host reports its own change here.
	OnRep_Ready();
}

void UEasySessionPlayerComponent::BeginPlay()
{
	Super::BeginPlay();

	// A component starts with its PlayerState, which is when a player appears in the session on this machine.
	if (UEasySessionSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->HandleSessionPlayersChanged();
	}
}

void UEasySessionPlayerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UEasySessionSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->HandleSessionPlayersChanged();
	}

	Super::EndPlay(EndPlayReason);
}

void UEasySessionPlayerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UEasySessionPlayerComponent, bReady);
}

void UEasySessionPlayerComponent::ServerSetReady_Implementation(bool bInReady)
{
	SetReady(bInReady);
}

void UEasySessionPlayerComponent::OnRep_Ready()
{
	if (UEasySessionSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->HandleSessionPlayersChanged();
	}
}

UEasySessionSubsystem* UEasySessionPlayerComponent::GetSubsystem() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;
}
