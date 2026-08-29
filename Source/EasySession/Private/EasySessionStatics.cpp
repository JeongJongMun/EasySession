// Copyright (c) 2026 Langerak. Licensed under the MIT License.

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

bool UEasySessionStatics::IsEasySessionAuthority(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsSessionAuthority();
}

EEasySessionState UEasySessionStatics::GetEasySessionState(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionState() : EEasySessionState::NoSession;
}

FString UEasySessionStatics::GetEasySessionPassword(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionPassword() : FString();
}

bool UEasySessionStatics::IsEasyQuickMatchRunning(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsQuickMatchRunning();
}

EEasyQuickMatchState UEasySessionStatics::GetEasyQuickMatchState(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetQuickMatchState() : EEasyQuickMatchState::Idle;
}

FString UEasySessionStatics::GetEasySessionStateLabel(const UObject* WorldContextObject)
{
	switch (GetEasySessionState(WorldContextObject))
	{
	case EEasySessionState::NoSession:  return TEXT("No Session");
	case EEasySessionState::Creating:   return TEXT("Creating");
	case EEasySessionState::Pending:    return TEXT("Waiting (Pending)");
	case EEasySessionState::Starting:   return TEXT("Starting");
	case EEasySessionState::InProgress: return TEXT("In Match (InProgress)");
	case EEasySessionState::Ending:     return TEXT("Ending");
	case EEasySessionState::Ended:      return TEXT("Waiting (Ended)");
	case EEasySessionState::Destroying: return TEXT("Destroying");
	}
	return TEXT("Unknown");
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

FString UEasySessionStatics::GetEasySessionDisplayName(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionDisplayName() : FString();
}

TArray<FEasySessionPlayerInfo> UEasySessionStatics::GetEasySessionPlayerInfos(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionPlayerInfos() : TArray<FEasySessionPlayerInfo>();
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

bool UEasySessionStatics::HasPendingEasyDisconnectInfo(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->HasPendingDisconnectInfo();
}

FEasyDisconnectInfo UEasySessionStatics::ConsumeLastEasyDisconnectInfo(const UObject* WorldContextObject)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->ConsumeLastDisconnectInfo() : FEasyDisconnectInfo();
}

bool UEasySessionStatics::IsOnlineSubsystemAvailable(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsOnlineSubsystemAvailable();
}

FString UEasySessionStatics::GetEasySessionQueueStatus(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetQueueStatus() : FString();
}

FEasySessionHostParams UEasySessionStatics::GetEasySessionHostParams(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionHostParams() : FEasySessionHostParams();
}

FString UEasySessionStatics::GetEasySessionJoinCode(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionJoinCode() : FString();
}

void UEasySessionStatics::CancelEasyQuickMatch(const UObject* WorldContextObject)
{
	if (UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject))
	{
		Subsystem->CancelQuickMatch();
	}
}

FName UEasySessionStatics::GetOnlineSubsystemName(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetOnlineSubsystemName() : NAME_None;
}

bool UEasySessionStatics::ServerTravelEasySession(const UObject* WorldContextObject, const FString& MapName)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->ServerTravelToMap(MapName);
}

void UEasySessionStatics::DestroyEasySessionForEveryone(const UObject* WorldContextObject, FText Reason)
{
	if (UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject))
	{
		Subsystem->DestroyEasySessionForEveryone(MoveTemp(Reason));
	}
}

bool UEasySessionStatics::SendEasySessionInviteToFriend(const UObject* WorldContextObject, const FEasySessionFriend& Friend)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->SendSessionInviteToFriend(Friend);
}

bool UEasySessionStatics::ShowEasyInviteUI(const UObject* WorldContextObject)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->ShowInviteUI();
}

bool UEasySessionStatics::ShowEasyProfileUI(const UObject* WorldContextObject, const FEasySessionFriend& Friend)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->ShowProfileUI(Friend);
}

bool UEasySessionStatics::ShowEasyProfileUIForPlayer(const UObject* WorldContextObject, const FEasySessionPlayerInfo& Player)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->ShowProfileUIForPlayer(Player);
}

FString UEasySessionStatics::ResultToString(EEasySessionResult Result)
{
	return EasySession::ResultToString(Result);
}
