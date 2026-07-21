// Copyright Langerak. All Rights Reserved.

#include "EasySessionDiagnostics.h"

#include "EasySession.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"

namespace
{
	/** Log a problem plus the exact ini lines that fix it. */
	void LogFix(const FString& Problem, const TArray<FString>& IniLines)
	{
		UE_LOG(LogEasySession, Warning, TEXT("[FIX] %s"), *Problem);
		if (IniLines.Num() > 0)
		{
			UE_LOG(LogEasySession, Warning, TEXT("      Add to DefaultEngine.ini:"));
			for (const FString& Line : IniLines)
			{
				UE_LOG(LogEasySession, Warning, TEXT("        %s"), *Line);
			}
		}
	}

	void DiagnoseSteam(UWorld* World, const IOnlineSubsystem& OnlineSub)
	{
		// [OnlineSubsystemSteam] keys that beginners forget most often.
		bool bEnabled = false;
		GConfig->GetBool(TEXT("OnlineSubsystemSteam"), TEXT("bEnabled"), bEnabled, GEngineIni);
		if (!bEnabled)
		{
			LogFix(TEXT("[OnlineSubsystemSteam] bEnabled is not set."),
				{ TEXT("[OnlineSubsystemSteam]"), TEXT("bEnabled=true") });
		}

		int32 AppId = 0;
		GConfig->GetInt(TEXT("OnlineSubsystemSteam"), TEXT("SteamDevAppId"), AppId, GEngineIni);
		if (AppId <= 0)
		{
			LogFix(TEXT("SteamDevAppId is not set - Steam cannot initialize without an app id (use 480 for testing)."),
				{ TEXT("[OnlineSubsystemSteam]"), TEXT("SteamDevAppId=480") });
		}

		bool bInitServerOnClient = false;
		GConfig->GetBool(TEXT("OnlineSubsystemSteam"), TEXT("bInitServerOnClient"), bInitServerOnClient, GEngineIni);
		if (!bInitServerOnClient)
		{
			LogFix(TEXT("bInitServerOnClient is not set - listen servers will not register with Steam, so other players cannot find your session."),
				{ TEXT("[OnlineSubsystemSteam]"), TEXT("bInitServerOnClient=true") });
		}

		// The game net driver must be the Steam one, or joins resolve to raw IPs and fail.
		if (GEngine != nullptr)
		{
			const FNetDriverDefinition* GameDriver = GEngine->NetDriverDefinitions.FindByPredicate(
				[](const FNetDriverDefinition& Definition)
				{
					return Definition.DefName == FName(TEXT("GameNetDriver"));
				});

			const FString DriverClass = GameDriver ? GameDriver->DriverClassName.ToString() : FString();
			if (!DriverClass.Contains(TEXT("SteamNetDriver")))
			{
				LogFix(TEXT("GameNetDriver is not the SteamNetDriver - sessions may be advertised but clients cannot connect."),
					{ TEXT("[/Script/Engine.GameEngine]"),
					  TEXT("+NetDriverDefinitions=(DefName=\"GameNetDriver\",DriverClassName=\"OnlineSubsystemSteam.SteamNetDriver\",DriverClassNameFallback=\"OnlineSubsystemUtils.IpNetDriver\")") });
			}
		}

		FString ConnectionClass;
		GConfig->GetString(TEXT("/Script/OnlineSubsystemSteam.SteamNetDriver"), TEXT("NetConnectionClassName"), ConnectionClass, GEngineIni);
		if (!ConnectionClass.Contains(TEXT("SteamNetConnection")))
		{
			LogFix(TEXT("SteamNetDriver has no SteamNetConnection class configured."),
				{ TEXT("[/Script/OnlineSubsystemSteam.SteamNetDriver]"),
				  TEXT("NetConnectionClassName=\"OnlineSubsystemSteam.SteamNetConnection\"") });
		}

		// Login state: the Steam client must be running and logged in.
		const IOnlineIdentityPtr Identity = OnlineSub.GetIdentityInterface();
		if (Identity.IsValid() && Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
		{
			UE_LOG(LogEasySession, Log, TEXT("[OK]  Steam is logged in as '%s'."), *Identity->GetPlayerNickname(0));
		}
		else
		{
			UE_LOG(LogEasySession, Warning, TEXT("[FIX] Steam is not logged in. Make sure the Steam client is running and logged in before launching the game."));
		}

		// Steam sessions cannot be tested inside PIE.
		if (World != nullptr && World->WorldType == EWorldType::PIE)
		{
			UE_LOG(LogEasySession, Warning, TEXT("[NOTE] Running under PIE - Steam sessions need Standalone or packaged builds, and two machines with different Steam accounts for full testing."));
		}
	}
}

void EasySessionDiagnostics::RunDiagnostics(UWorld* World)
{
	FString ConfiguredService;
	GConfig->GetString(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), ConfiguredService, GEngineIni);

	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(World);
	const FName ActualService = OnlineSub ? OnlineSub->GetSubsystemName() : NAME_None;

	UE_LOG(LogEasySession, Log, TEXT("===== EasySession diagnostics (configured: %s, active: %s) ====="),
		ConfiguredService.IsEmpty() ? TEXT("<unset>") : *ConfiguredService, *ActualService.ToString());

	if (OnlineSub == nullptr)
	{
		LogFix(TEXT("No online subsystem is active - sessions cannot work at all."),
			{ TEXT("[OnlineSubsystem]"), TEXT("DefaultPlatformService=NULL") });
		return;
	}

	// The configured service failed to load and something else took over.
	if (!ConfiguredService.IsEmpty() && ActualService != FName(*ConfiguredService))
	{
		UE_LOG(LogEasySession, Warning, TEXT("[FIX] DefaultPlatformService is '%s' but the active subsystem is '%s' - the configured service failed to load."),
			*ConfiguredService, *ActualService.ToString());

		if (ConfiguredService.Equals(TEXT("Steam"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogEasySession, Warning, TEXT("      Likely causes: the OnlineSubsystemSteam plugin is not enabled in the .uproject, or the Steam client is not running."));
		}
	}

	if (ActualService == STEAM_SUBSYSTEM)
	{
		DiagnoseSteam(World, *OnlineSub);
	}
	else if (ActualService == NULL_SUBSYSTEM)
	{
		UE_LOG(LogEasySession, Log, TEXT("[NOTE] NULL (LAN) subsystem active - sessions work on the local network only, and invites/friends/presence are unsupported. This is the expected mode for local testing."));
	}

	UE_LOG(LogEasySession, Log, TEXT("===== EasySession diagnostics complete ====="));
}
