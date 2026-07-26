// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionStateActor.h"

#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AEasySessionStateActor::AEasySessionStateActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.0f);
	PrimaryActorTick.bCanEverTick = false;
}

void AEasySessionStateActor::SetHostSessionState(EEasySessionState NewState)
{
	if (HasAuthority() && HostSessionState != NewState)
	{
		HostSessionState = NewState;
		ForceNetUpdate();
	}
}

void AEasySessionStateActor::MulticastReturnToMenu_Implementation(const FText& Reason)
{
	// The host tears its own session down separately - only remote players react.
	if (HasAuthority())
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (UEasySessionSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr)
	{
		Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::HostEndedSession, Reason);
	}
}

void AEasySessionStateActor::PostNetInit()
{
	Super::PostNetInit();

	// Covers the initial replication on late joiners, where the property may
	// arrive with the actor before any OnRep fires.
	PushStateToSubsystem();
}

void AEasySessionStateActor::OnRep_HostSessionState()
{
	PushStateToSubsystem();
}

void AEasySessionStateActor::PushStateToSubsystem()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (UEasySessionSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr)
	{
		Subsystem->HandleReplicatedHostSessionState(HostSessionState);
	}
}

void AEasySessionStateActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEasySessionStateActor, HostSessionState);
}
