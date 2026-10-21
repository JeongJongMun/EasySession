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

bool UEasySessionStatics::IsInEasySession(const UObject* WorldContextObject, EEasySessionType Type)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsInSession(Type);
}

bool UEasySessionStatics::IsEasyHost(const UObject* WorldContextObject, EEasySessionType Type)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsHost(Type);
}

EEasySessionState UEasySessionStatics::GetEasySessionState(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionState() : EEasySessionState::NoSession;
}

FString UEasySessionStatics::GetEasyPassword(const UObject* WorldContextObject, EEasySessionType Type)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionSettings(Type).Password : FString();
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

FString UEasySessionStatics::GetEasyDisplayName(const UObject* WorldContextObject, EEasySessionType Type)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetDisplayName(Type) : FString();
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

int32 UEasySessionStatics::GetEasyPlayerCount(const UObject* WorldContextObject, EEasySessionType Type)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetPlayerCount(Type) : 0;
}

int32 UEasySessionStatics::GetEasyMaxPlayers(const UObject* WorldContextObject, EEasySessionType Type)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetMaxPlayers(Type) : 0;
}

TArray<FEasyPartyMemberInfo> UEasySessionStatics::GetEasyPartyMembers(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetPartyMembers() : TArray<FEasyPartyMemberInfo>();
}

bool UEasySessionStatics::HasEasyLastParty(const UObject* WorldContextObject)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr && Subsystem->HasLastParty();
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

FEasySessionSettings UEasySessionStatics::GetEasySessionSettings(const UObject* WorldContextObject, EEasySessionType Type)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetSessionSettings(Type) : FEasySessionSettings();
}

FEasySessionHostParams UEasySessionStatics::MakeEasySessionHostParamsFromSettings(const FEasySessionSettings& Settings, const FString& InitialMapName, bool bIsLANMatch)
{
	FEasySessionHostParams HostParams;
	static_cast<FEasySessionSettings&>(HostParams) = Settings;
	HostParams.InitialMapName = InitialMapName;
	HostParams.bIsLANMatch = bIsLANMatch;
	return HostParams;
}

FEasySessionSettingValue UEasySessionStatics::MakeEasySettingString(const FString& Value)
{
	return FEasySessionSettingValue::MakeString(Value);
}

FEasySessionSettingValue UEasySessionStatics::MakeEasySettingInt(const int32 Value)
{
	return FEasySessionSettingValue::MakeInt(Value);
}

FEasySessionSettingValue UEasySessionStatics::MakeEasySettingBool(const bool Value)
{
	return FEasySessionSettingValue::MakeInt(Value ? 1 : 0);
}

bool UEasySessionStatics::GetEasySettingString(const FEasySessionSettingValue& Setting, FString& Value)
{
	const bool bIsString = Setting.GetType() == EEasySessionSettingType::String;
	Value = bIsString ? Setting.GetString() : FString();
	return bIsString;
}

bool UEasySessionStatics::GetEasySettingInt(const FEasySessionSettingValue& Setting, int32& Value)
{
	const bool bIsInteger = Setting.GetType() == EEasySessionSettingType::Integer;
	Value = bIsInteger ? Setting.GetInt() : 0;
	return bIsInteger;
}

bool UEasySessionStatics::GetEasySettingBool(const FEasySessionSettingValue& Setting, bool& Value)
{
	const bool bIsInteger = Setting.GetType() == EEasySessionSettingType::Integer;
	Value = bIsInteger && Setting.GetInt() != 0;
	return bIsInteger;
}

FString UEasySessionStatics::Conv_EasySettingToString(const FEasySessionSettingValue& Setting)
{
	return Setting.ToString();
}

FString UEasySessionStatics::GetEasyJoinCode(const UObject* WorldContextObject, EEasySessionType Type)
{
	const UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->GetJoinCode(Type) : FString();
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

EEasySessionResult UEasySessionStatics::ServerTravelEasySession(const UObject* WorldContextObject, const FString& MapName)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->ServerTravel(MapName) : EEasySessionResult::NoOnlineSubsystem;
}

EEasySessionResult UEasySessionStatics::KickEasySessionPlayer(const UObject* WorldContextObject, const FEasySessionPlayerInfo& Player, FText Reason)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->KickPlayer(Player, Reason) : EEasySessionResult::NoOnlineSubsystem;
}

EEasySessionResult UEasySessionStatics::DestroyEasySessionForEveryone(const UObject* WorldContextObject, FText Reason)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->DestroySessionForEveryone(Reason) : EEasySessionResult::NoOnlineSubsystem;
}

EEasySessionResult UEasySessionStatics::SendEasyInviteToFriend(const UObject* WorldContextObject, EEasySessionType Type, const FEasySessionFriend& Friend)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->SendInviteToFriend(Type, Friend) : EEasySessionResult::NoOnlineSubsystem;
}

EEasySessionResult UEasySessionStatics::ShowEasyInviteUI(const UObject* WorldContextObject, EEasySessionType Type)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->ShowInviteUI(Type) : EEasySessionResult::NoOnlineSubsystem;
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

EEasySessionResult UEasySessionStatics::ShowEasyProfileUIForPartyMember(const UObject* WorldContextObject, const FEasyPartyMemberInfo& Member)
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem(WorldContextObject);
	return Subsystem != nullptr ? Subsystem->ShowProfileUIForPartyMember(Member) : EEasySessionResult::NoOnlineSubsystem;
}
