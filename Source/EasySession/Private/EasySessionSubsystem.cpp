// Copyright Langerak. All Rights Reserved.

#include "EasySessionSubsystem.h"

#include "EasySession.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"

void UEasySessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub == nullptr)
	{
		UE_LOG(LogEasySession, Warning, TEXT("No online subsystem found. Check [OnlineSubsystem] DefaultPlatformService in DefaultEngine.ini."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("EasySessionSubsystem initialized. Online subsystem: %s"), *OnlineSub->GetSubsystemName().ToString());
}

void UEasySessionSubsystem::Deinitialize()
{
	UE_LOG(LogEasySession, Log, TEXT("EasySessionSubsystem deinitialized."));

	Super::Deinitialize();
}

FName UEasySessionSubsystem::GetOnlineSubsystemName() const
{
	const IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	return OnlineSub ? OnlineSub->GetSubsystemName() : NAME_None;
}

bool UEasySessionSubsystem::IsOnlineSubsystemAvailable() const
{
	const IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	return OnlineSub != nullptr && OnlineSub->GetSessionInterface().IsValid();
}
