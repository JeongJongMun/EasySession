// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionTypes.h"

#include "EasySession.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OnlineBeaconHost.h"
#include "Online/OnlineSessionNames.h"

namespace EasySession
{
	/** Custom session setting key holding the session display name. */
	const FName SettingKey_DisplayName = TEXT("EASYDISPLAYNAME");

	/** Custom session setting key marking a hidden session. */
	const FName SettingKey_Hidden = TEXT("EASYHIDDEN");

	/** Custom session setting key marking a password protected session. */
	const FName SettingKey_PasswordProtected = TEXT("EASYPASSWORDPROTECTED");

	/** Custom session setting key holding the advertised region. */
	const FName SettingKey_Region = TEXT("EASYREGION");

	/** Custom session setting key marking a session whose match is in progress. */
	const FName SettingKey_MatchInProgress = TEXT("EASYINPROGRESS");

	/** Custom session setting key holding the shareable join code. */
	const FName SettingKey_JoinCode = TEXT("EASYJOINCODE");

	/** Custom session setting key marking a session whose host runs join approval over a beacon. */
	const FName SettingKey_JoinApproval = TEXT("EASYJOINAPPROVAL");

	/** Travel URL option carrying the password a client supplies when joining. */
	const TCHAR* TravelOption_Password = TEXT("EasySessionPassword");

	int32 GetJoinApprovalBeaconPort()
	{
		// AOnlineBeaconHost::InitHost reads this override too, but writes it on the listener rather than on the class default below.
		int32 PortOverride = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("BeaconPort="), PortOverride) && PortOverride != 0)
		{
			return PortOverride;
		}

		return GetDefault<AOnlineBeaconHost>()->ListenPort;
	}

	bool IsReservedSettingKey(FName Key)
	{
		return Key == SettingKey_DisplayName
			|| Key == SettingKey_Hidden
			|| Key == SettingKey_PasswordProtected
			|| Key == SettingKey_Region
			|| Key == SettingKey_MatchInProgress
			|| Key == SettingKey_JoinCode
			|| Key == SettingKey_JoinApproval
			|| Key == SETTING_BEACONPORT;
	}

	FString GenerateJoinCode()
	{
		// Codes are read over voice chat and typed on gamepads, so every character must survive both.
		static const TCHAR Alphabet[] = TEXT("23456789ACDEFGHJKMNPQRSTUVWXYZ");
		static constexpr int32 AlphabetSize = UE_ARRAY_COUNT(Alphabet) - 1;

		FString Code;
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Code.AppendChar(Alphabet[FMath::RandRange(0, AlphabetSize - 1)]);
		}
		return Code;
	}

	FString ResultToString(EEasySessionResult Result)
	{
		switch (Result)
		{
			case EEasySessionResult::Success:					return TEXT("Success");
			case EEasySessionResult::NoOnlineSubsystem:			return TEXT("NoOnlineSubsystem");
			case EEasySessionResult::InvalidParams:				return TEXT("InvalidParams");
			case EEasySessionResult::SessionAlreadyExists:		return TEXT("SessionAlreadyExists");
			case EEasySessionResult::NoSessionExists:			return TEXT("NoSessionExists");
			case EEasySessionResult::CreateFailure:				return TEXT("CreateFailure");
			case EEasySessionResult::SearchFailure:				return TEXT("SearchFailure");
			case EEasySessionResult::NoSessionsFound:			return TEXT("NoSessionsFound");
			case EEasySessionResult::MatchmakingAlreadyInProgress: return TEXT("MatchmakingAlreadyInProgress");
			case EEasySessionResult::JoinFailure:				return TEXT("JoinFailure");
			case EEasySessionResult::JoinSessionFull:			return TEXT("JoinSessionFull");
			case EEasySessionResult::JoinSessionDoesNotExist:	return TEXT("JoinSessionDoesNotExist");
			case EEasySessionResult::WrongPassword:				return TEXT("WrongPassword");
			case EEasySessionResult::JoinRefused:				return TEXT("JoinRefused");
			case EEasySessionResult::ResolveFailure:			return TEXT("ResolveFailure");
			case EEasySessionResult::DestroyFailure:			return TEXT("DestroyFailure");
			case EEasySessionResult::UpdateFailure:				return TEXT("UpdateFailure");
			case EEasySessionResult::StateChangeFailure:		return TEXT("StateChangeFailure");
			case EEasySessionResult::Canceled:					return TEXT("Canceled");
			case EEasySessionResult::RequiresSessionAuthority:	return TEXT("RequiresSessionAuthority");
			case EEasySessionResult::FriendSearchAlreadyInProgress:	return TEXT("FriendSearchAlreadyInProgress");
			case EEasySessionResult::NotSupportedByService:		return TEXT("NotSupportedByService");
			default:											return TEXT("UnknownFailure");
		}
	}
}

bool FEasySessionSettings::IsValid() const
{
	return MaxPlayers > 0;
}

void FEasySessionSettings::ReadFrom(const FOnlineSessionSettings& Settings)
{
	MaxPlayers = Settings.NumPublicConnections;
	bShouldAdvertise = Settings.bShouldAdvertise;
	bAllowJoinInProgress = Settings.bAllowJoinInProgress;
	bAllowInvites = Settings.bAllowInvites;

	for (const TPair<FName, FOnlineSessionSetting>& Setting : Settings.Settings)
	{
		if (Setting.Key == EasySession::SettingKey_DisplayName)
		{
			SessionDisplayName = Setting.Value.Data.ToString();
		}
		else if (Setting.Key == EasySession::SettingKey_Hidden)
		{
			int32 Hidden = 0;
			Setting.Value.Data.GetValue(Hidden);
			bHidden = Hidden != 0;
		}
		else if (Setting.Key == EasySession::SettingKey_Region)
		{
			int32 RegionValue = 0;
			Setting.Value.Data.GetValue(RegionValue);
			Region = static_cast<EEasySessionRegion>(RegionValue);
		}
		else if (Setting.Key == EasySession::SettingKey_JoinCode)
		{
			FString JoinCode;
			Setting.Value.Data.GetValue(JoinCode);
			bUseJoinCode = !JoinCode.IsEmpty();
		}
		else if (!EasySession::IsReservedSettingKey(Setting.Key))
		{
			CustomSettings.Add(Setting.Key.ToString(), Setting.Value.Data.ToString());
		}
	}
}

void FEasySessionSettings::ApplyTo(FOnlineSessionSettings& OutSettings) const
{
	OutSettings.NumPublicConnections = MaxPlayers;
	OutSettings.bShouldAdvertise = bShouldAdvertise;
	OutSettings.bAllowJoinInProgress = bAllowJoinInProgress;
	OutSettings.bAllowInvites = bAllowInvites;

	// Every key is written even at its default value, because an advertised key cannot be deleted later.
	OutSettings.Set(EasySession::SettingKey_DisplayName, SessionDisplayName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	OutSettings.Set(EasySession::SettingKey_Hidden, bHidden ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	OutSettings.Set(EasySession::SettingKey_Region, static_cast<int32>(Region), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// Only the flag is advertised, and the server gate keeps the password. A whitespace-only password counts as none in both.
	OutSettings.Set(EasySession::SettingKey_PasswordProtected, Password.TrimStartAndEnd().IsEmpty() ? 0 : 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// A join code stays once generated, so players who already have it can still join.
	FString ExistingJoinCode;
	OutSettings.Get(EasySession::SettingKey_JoinCode, ExistingJoinCode);
	if (bUseJoinCode)
	{
		OutSettings.Set(EasySession::SettingKey_JoinCode, ExistingJoinCode.IsEmpty() ? EasySession::GenerateJoinCode() : ExistingJoinCode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}
	else if (!ExistingJoinCode.IsEmpty())
	{
		OutSettings.Set(EasySession::SettingKey_JoinCode, FString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

	// A custom setting left out of CustomSettings is removed. Reserved keys are not custom settings.
	TArray<FName> DroppedKeys;
	for (const TPair<FName, FOnlineSessionSetting>& Existing : OutSettings.Settings)
	{
		if (!EasySession::IsReservedSettingKey(Existing.Key) && !CustomSettings.Contains(Existing.Key.ToString()))
		{
			DroppedKeys.Add(Existing.Key);
		}
	}

	for (const FName& Key : DroppedKeys)
	{
		OutSettings.Remove(Key);
	}

	for (const TPair<FString, FString>& Custom : CustomSettings)
	{
		const FName Key(*Custom.Key);
		if (EasySession::IsReservedSettingKey(Key))
		{
			UE_LOG(LogEasySession, Warning, TEXT("Custom Setting '%s' is a key this plugin uses for itself. It was not written."), *Custom.Key);
			continue;
		}

		OutSettings.Set(Key, Custom.Value, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}
}

bool FEasySessionHostParams::IsValid() const
{
	return FEasySessionSettings::IsValid() && !InitialMapName.TrimStartAndEnd().IsEmpty();
}

bool FEasySessionSearchParams::IsValid() const
{
	// The mode and the target id must be set together: one without the other asks about no one or ignores the id.
	if ((SearchMode != EEasySessionSearchMode::Default) != SearchTargetId.IsValid())
	{
		return false;
	}

	return MaxResults > 0;
}

bool FEasySessionSearchParams::ShouldInclude(const FEasySessionSearchResult& Result) const
{
	// Hidden sessions are advertised for invites and targeted searches but never listed in searches.
	if (Result.bIsHidden && !bIncludeHiddenSessions)
	{
		return false;
	}
	if (Region != EEasySessionRegion::Any && Result.Region != Region)
	{
		return false;
	}
	if (!bIncludeInProgressSessions && Result.bMatchInProgress)
	{
		return false;
	}
	if (Result.OpenSlots < MinOpenSlots)
	{
		return false;
	}
	if (MaxPingMs > 0 && Result.PingInMs > MaxPingMs)
	{
		return false;
	}
	if (OwnerId.IsValid() &&
		(!Result.NativeResult.Session.OwningUserId.IsValid() || *Result.NativeResult.Session.OwningUserId != *OwnerId.GetUniqueNetId()))
	{
		return false;
	}
	if (!JoinCode.IsEmpty() && !Result.JoinCode.Equals(JoinCode, ESearchCase::IgnoreCase))
	{
		return false;
	}

	for (const TPair<FString, FString>& Required : RequiredCustomSettings)
	{
		const FString* FoundValue = Result.CustomSettings.Find(Required.Key);
		if (FoundValue == nullptr || *FoundValue != Required.Value)
		{
			return false;
		}
	}

	return true;
}

bool FEasySessionSearchResult::IsValid() const
{
	return NativeResult.IsValid();
}

FEasySessionSearchResult FEasySessionSearchResult::FromNative(const FOnlineSessionSearchResult& InNativeResult)
{
	FEasySessionSearchResult Result;
	Result.NativeResult = InNativeResult;
	Result.HostName = InNativeResult.Session.OwningUserName;
	Result.PingInMs = InNativeResult.PingInMs;
	Result.MaxPlayers = InNativeResult.Session.SessionSettings.NumPublicConnections;
	Result.OpenSlots = InNativeResult.Session.NumOpenPublicConnections;
	Result.bIsDedicatedServer = InNativeResult.Session.SessionSettings.bIsDedicated;

	for (const TPair<FName, FOnlineSessionSetting>& Setting : InNativeResult.Session.SessionSettings.Settings)
	{
		if (Setting.Key == EasySession::SettingKey_DisplayName)
		{
			Result.SessionDisplayName = Setting.Value.Data.ToString();
		}
		else if (Setting.Key == EasySession::SettingKey_Hidden)
		{
			// The value, not the key's presence. Both flags are written either way.
			int32 Hidden = 0;
			Setting.Value.Data.GetValue(Hidden);
			Result.bIsHidden = Hidden != 0;
		}
		else if (Setting.Key == EasySession::SettingKey_PasswordProtected)
		{
			int32 Protected = 0;
			Setting.Value.Data.GetValue(Protected);
			Result.bPasswordProtected = Protected != 0;
		}
		else if (Setting.Key == EasySession::SettingKey_Region)
		{
			int32 RegionValue = 0;
			Setting.Value.Data.GetValue(RegionValue);
			Result.Region = static_cast<EEasySessionRegion>(RegionValue);
		}
		else if (Setting.Key == EasySession::SettingKey_JoinCode)
		{
			Result.JoinCode = Setting.Value.Data.ToString();
		}
		else if (Setting.Key == EasySession::SettingKey_MatchInProgress)
		{
			int32 MatchInProgress = 0;
			Setting.Value.Data.GetValue(MatchInProgress);
			Result.bMatchInProgress = MatchInProgress != 0;
		}
		else if (!EasySession::IsReservedSettingKey(Setting.Key))
		{
			Result.CustomSettings.Add(Setting.Key.ToString(), Setting.Value.Data.ToString());
		}
	}

	return Result;
}
