// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionStateActor.h"

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

void AEasySessionStateActor::SetReplicatedSessionSettings(const FEasySessionReplicatedSettings& NewSettings)
{
	if (HasAuthority() && !(ReplicatedSessionSettings == NewSettings))
	{
		ReplicatedSessionSettings = NewSettings;
		ForceNetUpdate();
	}
}

void AEasySessionStateActor::MulticastReturnToMenu_Implementation(const FText& Reason)
{
	// The host destroys its own session separately. Only remote players react.
	if (HasAuthority())
	{
		return;
	}

	if (UEasySessionSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->HandleDisconnect(EEasyDisconnectReason::HostDestroyedSession, Reason);
	}
}

void AEasySessionStateActor::PostNetInit()
{
	Super::PostNetInit();

	// A property that arrives with the actor and equals its default fires no OnRep.
	// Players who join late get both calls here, and the subsystem ignores a value it already has.
	OnRep_HostSessionState();
	OnRep_ReplicatedSessionSettings();
}

void AEasySessionStateActor::OnRep_HostSessionState()
{
	if (UEasySessionSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->HandleReplicatedSessionState(HostSessionState);
	}
}

void AEasySessionStateActor::OnRep_ReplicatedSessionSettings()
{
	if (UEasySessionSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->HandleReplicatedSessionSettings(ReplicatedSessionSettings);
	}
}

UEasySessionSubsystem* AEasySessionStateActor::GetSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;
}

void AEasySessionStateActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEasySessionStateActor, HostSessionState);
	DOREPLIFETIME(AEasySessionStateActor, ReplicatedSessionSettings);
}
