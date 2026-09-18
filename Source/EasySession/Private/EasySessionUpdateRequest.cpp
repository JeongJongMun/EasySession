// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionUpdateRequest.h"

#include "EasySession.h"
#include "EasySessionHost.h"
#include "EasySessionSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "OnlineSessionSettings.h"

FEasySessionUpdateRequest::FEasySessionUpdateRequest(const FEasySessionSettings& InSettings, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::Update)
	, Settings(InSettings)
	, OnComplete(MoveTemp(InOnComplete))
{
}

void FEasySessionUpdateRequest::Execute()
{
	if (!Settings.IsValid())
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("Update settings are invalid."));
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	const FNamedOnlineSession* NamedSession = Sessions->GetNamedSession(SessionName);
	if (NamedSession == nullptr)
	{
		Complete(EEasySessionResult::NoSessionExists, TEXT("There is no session to update."));
		return;
	}

	if (!GetContext().Subsystem.IsSessionAuthority())
	{
		Complete(EEasySessionResult::RequiresSessionAuthority,
			FString::Printf(TEXT("Only the game hosting the session can update it. %s"), EasySession::RequiresSessionAuthorityFix));
		return;
	}

	FOnlineSessionSettings UpdatedSettings = MakeUpdatedSettings(NamedSession->SessionSettings, Settings);

	UpdateCompleteHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(
		FOnUpdateSessionCompleteDelegate::CreateSP(this, &FEasySessionUpdateRequest::HandleUpdateSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Updating session."));

	if (!Sessions->UpdateSession(SessionName, UpdatedSettings, true))
	{
		Complete(EEasySessionResult::UpdateFailure, TEXT("UpdateSession request was rejected by the online subsystem."));
	}
}

void FEasySessionUpdateRequest::HandleUpdateSessionComplete(FName InSessionName, bool bWasSuccessful)
{
	if (!IsActive() || InSessionName != SessionName)
	{
		return;
	}

	if (!bWasSuccessful)
	{
		Complete(EEasySessionResult::UpdateFailure, TEXT("The online subsystem failed to update the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session updated successfully."));

	// The engine's player cap is set to the advertised one, so its "Server full" refusal uses the new Max Players.
	UWorld* World = GetContext().GetWorld();
	AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
	if (GameMode && GameMode->GameSession)
	{
		GameMode->GameSession->MaxPlayers = Settings.MaxPlayers;
	}

	// UpdateSession never re-derives the open slot count, so recompute it from the registered players.
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(SessionName) : nullptr)
	{
		NamedSession->NumOpenPublicConnections =
			FMath::Max(0, NamedSession->SessionSettings.NumPublicConnections - NamedSession->RegisteredPlayers.Num());
	}

	// The server gate gets the credentials only now, so a refused update leaves it matching what is advertised.
	GetContext().Host.OnSettingsUpdated(Settings);

	Complete(EEasySessionResult::Success);
}

void FEasySessionUpdateRequest::Cleanup(bool bAbandoned)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateCompleteHandle);
	}
}

void FEasySessionUpdateRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
	GetContext().Subsystem.OnSessionUpdated.Broadcast(Result, ErrorMessage);
}

FOnlineSessionSettings FEasySessionUpdateRequest::MakeUpdatedSettings(const FOnlineSessionSettings& Current, const FEasySessionSettings& NewSettings)
{
	FOnlineSessionSettings UpdatedSettings = Current;
	UpdatedSettings.NumPublicConnections = NewSettings.MaxPlayers;
	UpdatedSettings.bShouldAdvertise = NewSettings.bShouldAdvertise;
	UpdatedSettings.bAllowJoinInProgress = NewSettings.bAllowJoinInProgress;
	UpdatedSettings.bAllowInvites = NewSettings.bAllowInvites;
	UpdatedSettings.Set(EasySession::SettingKey_DisplayName, NewSettings.SessionDisplayName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	UpdatedSettings.Set(EasySession::SettingKey_Hidden, NewSettings.bHidden ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// Only the flag is advertised here.
	// The server gate gets the password itself in HandleUpdateSessionComplete, so the two never differ if this request fails.
	UpdatedSettings.Set(EasySession::SettingKey_PasswordProtected, NewSettings.Password.TrimStartAndEnd().IsEmpty() ? 0 : 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	UpdatedSettings.Set(EasySession::SettingKey_Region, static_cast<int32>(NewSettings.Region), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// An update keeps the join code.
	// Turning Use Join Code on during the session generates one.
	// Turning it off writes an empty code, because an advertised key cannot be deleted.
	FString ExistingJoinCode;
	Current.Get(EasySession::SettingKey_JoinCode, ExistingJoinCode);
	if (NewSettings.bUseJoinCode)
	{
		UpdatedSettings.Set(EasySession::SettingKey_JoinCode, ExistingJoinCode.IsEmpty() ? EasySession::GenerateJoinCode() : ExistingJoinCode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}
	else if (!ExistingJoinCode.IsEmpty())
	{
		UpdatedSettings.Set(EasySession::SettingKey_JoinCode, FString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

	// A custom setting left out of the map is removed.
	// Reserved keys are skipped because Update does not write them again.
	TArray<FName> DroppedKeys;
	for (const TPair<FName, FOnlineSessionSetting>& Existing : UpdatedSettings.Settings)
	{
		if (!EasySession::IsReservedSettingKey(Existing.Key) && !NewSettings.CustomSettings.Contains(Existing.Key.ToString()))
		{
			DroppedKeys.Add(Existing.Key);
		}
	}

	for (const FName& Key : DroppedKeys)
	{
		UpdatedSettings.Remove(Key);
	}

	for (const TPair<FString, FString>& Custom : NewSettings.CustomSettings)
	{
		const FName Key(*Custom.Key);
		if (EasySession::IsReservedSettingKey(Key))
		{
			UE_LOG(LogEasySession, Warning, TEXT("Custom Setting '%s' is a key this plugin uses for itself. It was not written."), *Custom.Key);
			continue;
		}

		UpdatedSettings.Set(Key, Custom.Value, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

	return UpdatedSettings;
}
