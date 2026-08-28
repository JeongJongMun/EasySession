// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionDiagnostics.h"

#include "EasySession.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"

namespace
{
	using EasySessionDiagnostics::EFindingKind;
	using EasySessionDiagnostics::FFinding;
	using EasySessionDiagnostics::FReport;

	/** Append a problem plus the exact ini lines that fix it. */
	void AddFix(FReport& Report, FString Problem, TArray<FString> IniLines = {}, FString Postscript = FString())
	{
		FFinding Finding;
		Finding.Message = MoveTemp(Problem);
		Finding.IniLines = MoveTemp(IniLines);
		Finding.Postscript = MoveTemp(Postscript);
		Report.Findings.Add(MoveTemp(Finding));
	}

	/** Append an informational line. */
	void AddInfo(FReport& Report, EFindingKind Kind, FString Message)
	{
		FFinding Finding;
		Finding.Kind = Kind;
		Finding.Message = MoveTemp(Message);
		Report.Findings.Add(MoveTemp(Finding));
	}

	void DiagnoseSteam(UWorld* World, const IOnlineSubsystem& OnlineSub, FReport& Report)
	{
		// [OnlineSubsystemSteam] keys that beginners forget most often. bEnabled is not
		// checked here: a missing key counts as enabled, and Steam being active - the
		// only way into this function - already proves the key did not stop it.
		int32 AppId = 0;
		GConfig->GetInt(TEXT("OnlineSubsystemSteam"), TEXT("SteamDevAppId"), AppId, GEngineIni);
		if (AppId <= 0)
		{
			AddFix(Report, TEXT("SteamDevAppId is not set - Steam cannot initialize without an app id (use 480 for testing)."),
				{ TEXT("[OnlineSubsystemSteam]"), TEXT("SteamDevAppId=480") });
		}

		bool bInitServerOnClient = false;
		GConfig->GetBool(TEXT("OnlineSubsystemSteam"), TEXT("bInitServerOnClient"), bInitServerOnClient, GEngineIni);
		if (!bInitServerOnClient)
		{
			AddFix(Report, TEXT("bInitServerOnClient is not set - listen servers will not register with Steam, so other players cannot find your session."),
				{ TEXT("[OnlineSubsystemSteam]"), TEXT("bInitServerOnClient=true") });
		}

		// The game net driver must be a Steam one, or joins resolve steam.<id> hosts as
		// DNS names and fail. Mirror the engine's lookup exactly: it takes the FIRST
		// definition named GameNetDriver, then silently falls back to the IP driver
		// when that class fails to load (e.g. the legacy SteamNetDriver, which no
		// longer exists in newer engine versions).
		if (GEngine != nullptr)
		{
			const FNetDriverDefinition* GameDriver = GEngine->NetDriverDefinitions.FindByPredicate(
				[](const FNetDriverDefinition& Definition)
				{
					return Definition.DefName == FName(TEXT("GameNetDriver"));
				});

			const TArray<FString> SteamSocketsFix =
				{ TEXT("[/Script/Engine.GameEngine]"),
				  TEXT("!NetDriverDefinitions=ClearArray"),
				  TEXT("+NetDriverDefinitions=(DefName=\"GameNetDriver\",DriverClassName=\"/Script/SteamSockets.SteamSocketsNetDriver\",DriverClassNameFallback=\"/Script/OnlineSubsystemUtils.IpNetDriver\")") };
			const FString SteamSocketsAdvice = TEXT("Also enable the 'SteamSockets' plugin in the .uproject.");

			const FString DriverClass = GameDriver ? GameDriver->DriverClassName.ToString() : FString();
			if (!DriverClass.Contains(TEXT("Steam")))
			{
				AddFix(Report, TEXT("GameNetDriver is not a Steam net driver - sessions are advertised but clients cannot connect."), SteamSocketsFix, SteamSocketsAdvice);
			}
			else if (StaticLoadClass(UNetDriver::StaticClass(), nullptr, *DriverClass, nullptr, LOAD_Quiet) == nullptr)
			{
				AddFix(Report, FString::Printf(TEXT("GameNetDriver class '%s' does not exist, so the engine silently falls back to the IP driver and Steam joins fail. Use the SteamSockets plugin instead."), *DriverClass), SteamSocketsFix, SteamSocketsAdvice);
			}
			else if (DriverClass.Contains(TEXT("OnlineSubsystemSteam.SteamNetDriver")))
			{
				// Legacy driver on an engine that still ships it: it needs its connection class.
				FString ConnectionClass;
				GConfig->GetString(TEXT("/Script/OnlineSubsystemSteam.SteamNetDriver"), TEXT("NetConnectionClassName"), ConnectionClass, GEngineIni);
				if (!ConnectionClass.Contains(TEXT("SteamNetConnection")))
				{
					AddFix(Report, TEXT("SteamNetDriver has no SteamNetConnection class configured."),
						{ TEXT("[/Script/OnlineSubsystemSteam.SteamNetDriver]"),
						  TEXT("NetConnectionClassName=\"OnlineSubsystemSteam.SteamNetConnection\"") });
				}
			}
		}

		// Login state: the Steam client must be running and logged in.
		const IOnlineIdentityPtr Identity = OnlineSub.GetIdentityInterface();
		if (Identity.IsValid() && Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
		{
			AddInfo(Report, EFindingKind::Ok, FString::Printf(TEXT("Steam is logged in as '%s'."), *Identity->GetPlayerNickname(0)));
		}
		else
		{
			AddFix(Report, TEXT("Steam is not logged in. Make sure the Steam client is running and logged in before launching the game."));
		}

		// Steam sessions cannot be tested inside PIE.
		if (World != nullptr && World->WorldType == EWorldType::PIE)
		{
			AddInfo(Report, EFindingKind::Note, TEXT("Running under PIE - Steam sessions need Standalone or packaged builds, and two machines with different Steam accounts for full testing."));
		}
	}
}

EasySessionDiagnostics::FReport EasySessionDiagnostics::RunDiagnostics(UWorld* World)
{
	FReport Report;

	FString ConfiguredService;
	GConfig->GetString(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), ConfiguredService, GEngineIni);

	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(World);
	const FName ActualService = OnlineSub ? OnlineSub->GetSubsystemName() : NAME_None;

	Report.Summary = FString::Printf(TEXT("configured: %s, active: %s"),
		ConfiguredService.IsEmpty() ? TEXT("<unset>") : *ConfiguredService, *ActualService.ToString());

	if (OnlineSub == nullptr)
	{
		AddFix(Report, TEXT("No online subsystem is active - sessions cannot work at all."),
			{ TEXT("[OnlineSubsystem]"), TEXT("DefaultPlatformService=NULL") });
		return Report;
	}

	// The configured service failed to load and a different service loaded instead.
	if (!ConfiguredService.IsEmpty() && ActualService != FName(*ConfiguredService))
	{
		FString Causes;
		if (ConfiguredService.Equals(TEXT("Steam"), ESearchCase::IgnoreCase))
		{
			Causes = TEXT("Likely causes: the OnlineSubsystemSteam plugin is not enabled in the .uproject, [OnlineSubsystemSteam] bEnabled=false in DefaultEngine.ini, or the Steam client is not running.");
		}

		AddFix(Report, FString::Printf(TEXT("DefaultPlatformService is '%s' but the active subsystem is '%s' - the configured service failed to load."),
			*ConfiguredService, *ActualService.ToString()), {}, MoveTemp(Causes));
	}

	if (ActualService == STEAM_SUBSYSTEM)
	{
		DiagnoseSteam(World, *OnlineSub, Report);
	}
	else if (ActualService == NULL_SUBSYSTEM)
	{
		AddInfo(Report, EFindingKind::Note, TEXT("NULL (LAN) subsystem active - sessions work on the local network only, and invites/friends/presence are unsupported. This is the expected mode for local testing."));
	}

	// The join approval beacon needs a BeaconNetDriver definition. The engine ships one
	// in BaseEngine.ini, but a project that clears NetDriverDefinitions (the Steam
	// setup does) removes it along with the rest.
	if (GEngine != nullptr && !GEngine->NetDriverDefinitions.ContainsByPredicate(
		[](const FNetDriverDefinition& Definition) { return Definition.DefName == FName(TEXT("BeaconNetDriver")); }))
	{
		AddFix(Report, TEXT("No BeaconNetDriver definition - the join approval beacon cannot start, so hosts fall back to refusing players after they have already traveled."),
			{ TEXT("[/Script/Engine.GameEngine]"),
			  TEXT("+NetDriverDefinitions=(DefName=\"BeaconNetDriver\",DriverClassName=\"/Script/OnlineSubsystemUtils.IpNetDriver\",DriverClassNameFallback=\"/Script/OnlineSubsystemUtils.IpNetDriver\")") });
	}

	return Report;
}

void EasySessionDiagnostics::LogReport(const FReport& Report)
{
	UE_LOG(LogEasySession, Log, TEXT("===== EasySession diagnostics (%s) ====="), *Report.Summary);

	for (const FFinding& Finding : Report.Findings)
	{
		switch (Finding.Kind)
		{
			case EFindingKind::Fix:
			{
				UE_LOG(LogEasySession, Warning, TEXT("[FIX] %s"), *Finding.Message);
				if (Finding.IniLines.Num() > 0)
				{
					UE_LOG(LogEasySession, Warning, TEXT("      Add to DefaultEngine.ini:"));
					for (const FString& Line : Finding.IniLines)
					{
						UE_LOG(LogEasySession, Warning, TEXT("        %s"), *Line);
					}
				}
				if (!Finding.Postscript.IsEmpty())
				{
					UE_LOG(LogEasySession, Warning, TEXT("      %s"), *Finding.Postscript);
				}
				break;
			}

			case EFindingKind::Note:
			{
				UE_LOG(LogEasySession, Log, TEXT("[NOTE] %s"), *Finding.Message);
				break;
			}

			case EFindingKind::Ok:
			{
				UE_LOG(LogEasySession, Log, TEXT("[OK]  %s"), *Finding.Message);
				break;
			}
		}
	}

	UE_LOG(LogEasySession, Log, TEXT("===== EasySession diagnostics complete ====="));
}
