// Copyright Langerak. All Rights Reserved.

#include "EasySessionStatics.h"

#include "EasySessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UEasySessionSubsystem* UEasySessionStatics::GetEasySessionSubsystem(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;
}

bool UEasySessionStatics::IsInEasySession(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsInSession();
}

bool UEasySessionStatics::IsEasySessionHost(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsHost();
}

bool UEasySessionStatics::IsEasySessionBusy(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsBusy();
}

TArray<FEasySessionSearchResult> UEasySessionStatics::GetLastEasySearchResults(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetLastSearchResults() : TArray<FEasySessionSearchResult>();
}

TArray<FString> UEasySessionStatics::GetEasySessionPlayerNames(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionPlayerNames() : TArray<FString>();
}

int32 UEasySessionStatics::GetEasySessionPlayerCount(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionPlayerCount() : 0;
}

int32 UEasySessionStatics::GetEasySessionMaxPlayers(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionMaxPlayers() : 0;
}

FName UEasySessionStatics::GetOnlineSubsystemName(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetOnlineSubsystemName() : NAME_None;
}

FString UEasySessionStatics::ResultToString(EEasySessionResult Result)
{
	return EasySession::ResultToString(Result);
}
