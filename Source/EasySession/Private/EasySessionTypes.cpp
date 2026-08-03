// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionTypes.h"

namespace EasySession
{
	/** Custom session setting key holding the session display name. */
	const FName SettingKey_DisplayName = TEXT("EASYDISPLAYNAME");

	/** Custom session setting key marking a hidden session. */
	const FName SettingKey_Hidden = TEXT("EASYHIDDEN");

	/** Custom session setting key marking a password protected session. */
	const FName SettingKey_PasswordProtected = TEXT("EASYPASSWORDPROTECTED");

	/** Travel URL option carrying the password a client supplies when joining. */
	const TCHAR* TravelOption_Password = TEXT("EasySessionPassword");

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
			Result.bIsHidden = true;
		}
		else if (Setting.Key == EasySession::SettingKey_PasswordProtected)
		{
			Result.bPasswordProtected = true;
		}
		else
		{
			Result.CustomSettings.Add(Setting.Key.ToString(), Setting.Value.Data.ToString());
		}
	}

	return Result;
}
