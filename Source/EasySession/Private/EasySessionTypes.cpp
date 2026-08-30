// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionTypes.h"

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

	/** Custom session setting key holding the shareable join code. */
	const FName SettingKey_JoinCode = TEXT("EASYJOINCODE");

	/** Custom session setting key marking a session whose host answers join approval over a beacon. */
	const FName SettingKey_JoinApproval = TEXT("EASYJOINAPPROVAL");

	/** Travel URL option carrying the password a client supplies when joining. */
	const TCHAR* TravelOption_Password = TEXT("EasySessionPassword");

	int32 GetJoinApprovalBeaconPort()
	{
		return GetDefault<AOnlineBeaconHost>()->ListenPort;
	}

	bool IsReservedSettingKey(FName Key)
	{
		return Key == SettingKey_DisplayName
			|| Key == SettingKey_Hidden
			|| Key == SettingKey_PasswordProtected
			|| Key == SettingKey_Region
			|| Key == SettingKey_JoinCode
			|| Key == SettingKey_JoinApproval
			|| Key == SETTING_MAPNAME
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
			case EEasySessionResult::Timeout:					return TEXT("Timeout");
			case EEasySessionResult::RequiresSessionAuthority:	return TEXT("RequiresSessionAuthority");
			default:											return TEXT("UnknownFailure");
		}
	}
}

bool FEasySessionHostParams::IsValid() const
{
	return MaxPlayers > 0;
}

bool FEasySessionSearchParams::IsValid() const
{
	return MaxResults > 0 && TimeoutSeconds > 0.0f;
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
			// The value, not the key's presence - both flags are written either way.
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
		else if (Setting.Key == SETTING_MAPNAME)
		{
			Result.MapName = Setting.Value.Data.ToString();
		}
		else if (!EasySession::IsReservedSettingKey(Setting.Key))
		{
			Result.CustomSettings.Add(Setting.Key.ToString(), Setting.Value.Data.ToString());
		}
	}

	return Result;
}
