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

EEasySessionState UEasySessionStatics::GetEasySessionState(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionState() : EEasySessionState::NoSession;
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

FName UEasySessionStatics::GetOnlineSubsystemName(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetOnlineSubsystemName() : NAME_None;
}

TArray<FEasySessionInvite> UEasySessionStatics::GetPendingEasySessionInvites(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetPendingSessionInvites() : TArray<FEasySessionInvite>();
}

void UEasySessionStatics::AcceptEasySessionInvite(const UObject* WorldContextObject, const FEasySessionInvite& Invite)
{
	if (UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject))
	{
		Subsystem->AcceptSessionInvite(Invite);
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
