// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionStatics.h"

#include "EasyMatchmakingPolicy.h"
#include "EasySessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

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
	return Subsystem != nullptr ? Subsystem->GetSessionSettings().Password : FString();
}

bool UEasySessionStatics::IsEasyMatchmakingRunning(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsMatchmakingRunning();
}

EEasyMatchmakingState UEasySessionStatics::GetEasyMatchmakingState(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetMatchmakingState() : EEasyMatchmakingState::Idle;
}

UEasyMatchmakingPolicy* UEasySessionStatics::GetActiveEasyMatchmakingPolicy(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetActiveMatchmakingPolicy() : nullptr;
}

bool UEasySessionStatics::IsEasySessionBusy(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsBusy();
}

EEasySessionActivity UEasySessionStatics::GetEasySessionActivity(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetActivity() : EEasySessionActivity::None;
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

EEasySessionResult UEasySessionStatics::SetEasySessionReady(const UObject* WorldContextObject, bool bReady)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->SetSessionReady(bReady) : EEasySessionResult::NoOnlineSubsystem;
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

bool UEasySessionStatics::IsInEasyParty(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsInParty();
}

bool UEasySessionStatics::IsEasyPartyLeader(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsPartyLeader();
}

TArray<FEasyPartyMemberInfo> UEasySessionStatics::GetEasyPartyMembers(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetPartyMembers() : TArray<FEasyPartyMemberInfo>();
}

bool UEasySessionStatics::IsEasyPartyRestoring(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsRestoringParty();
}

FEasyPartySettings UEasySessionStatics::GetEasyPartySettings(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetPartySettings() : FEasyPartySettings();
}

FString UEasySessionStatics::GetEasyPartyJoinCode(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetPartyJoinCode() : FString();
}

EEasySessionResult UEasySessionStatics::KickEasyPartyMember(const UObject* WorldContextObject, const FEasyPartyMemberInfo& Member, FText Reason)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->KickPartyMember(Member, Reason) : EEasySessionResult::NoOnlineSubsystem;
}

EEasySessionResult UEasySessionStatics::SetEasyPartyReady(const UObject* WorldContextObject, bool bReady)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->SetPartyReady(bReady) : EEasySessionResult::NoOnlineSubsystem;
}

bool UEasySessionStatics::HasPendingEasyDisconnectInfo(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->HasPendingDisconnectInfo();
}

FEasyDisconnectInfo UEasySessionStatics::ConsumePendingEasyDisconnectInfo(const UObject* WorldContextObject)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->ConsumePendingDisconnectInfo() : FEasyDisconnectInfo();
}

bool UEasySessionStatics::IsOnlineSubsystemAvailable(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return Online::GetSessionInterface(World).IsValid();
}

FString UEasySessionStatics::GetEasySessionQueueStatus(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetQueueStatus() : FString();
}

FEasySessionSettings UEasySessionStatics::GetEasySessionSettings(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionSettings() : FEasySessionSettings();
}

FString UEasySessionStatics::GetEasySessionJoinCode(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionJoinCode() : FString();
}

void UEasySessionStatics::CancelEasyFriendSearch(const UObject* WorldContextObject)
{
	if (UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject))
	{
		Subsystem->CancelFriendSearch();
	}
}

void UEasySessionStatics::CancelEasyMatchmaking(const UObject* WorldContextObject)
{
	if (UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject))
	{
		Subsystem->CancelMatchmaking();
	}
}

FName UEasySessionStatics::GetOnlineSubsystemName(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(World);
	return OnlineSub ? OnlineSub->GetSubsystemName() : NAME_None;
}

bool UEasySessionStatics::ServerTravelEasySession(const UObject* WorldContextObject, const FString& MapName)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->ServerTravel(MapName);
}

EEasySessionResult UEasySessionStatics::KickEasySessionPlayer(const UObject* WorldContextObject, const FEasySessionPlayerInfo& Player, FText Reason)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->KickPlayer(Player, Reason) : EEasySessionResult::NoOnlineSubsystem;
}

void UEasySessionStatics::DestroyEasySessionForEveryone(const UObject* WorldContextObject, FText Reason)
{
	if (UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject))
	{
		Subsystem->DestroySessionForEveryone(MoveTemp(Reason));
	}
}

EEasySessionResult UEasySessionStatics::SendEasySessionInviteToFriend(const UObject* WorldContextObject, const FEasySessionFriend& Friend)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->SendSessionInviteToFriend(Friend) : EEasySessionResult::NoOnlineSubsystem;
}

EEasySessionResult UEasySessionStatics::ShowEasyInviteUI(const UObject* WorldContextObject)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->ShowInviteUI() : EEasySessionResult::NoOnlineSubsystem;
}

EEasySessionResult UEasySessionStatics::SendEasyPartyInviteToFriend(const UObject* WorldContextObject, const FEasySessionFriend& Friend)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->SendPartyInviteToFriend(Friend) : EEasySessionResult::NoOnlineSubsystem;
}

EEasySessionResult UEasySessionStatics::ShowEasyPartyInviteUI(const UObject* WorldContextObject)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->ShowPartyInviteUI() : EEasySessionResult::NoOnlineSubsystem;
}

EEasySessionResult UEasySessionStatics::ShowEasyProfileUI(const UObject* WorldContextObject, const FEasySessionFriend& Friend)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->ShowProfileUI(Friend) : EEasySessionResult::NoOnlineSubsystem;
}

EEasySessionResult UEasySessionStatics::ShowEasyProfileUIForPlayer(const UObject* WorldContextObject, const FEasySessionPlayerInfo& Player)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->ShowProfileUIForPlayer(Player) : EEasySessionResult::NoOnlineSubsystem;
}
