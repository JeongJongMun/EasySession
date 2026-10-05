// Copyright (c) 2026 Langerak. Licensed under the MIT License.

// Debug console commands for testing EasySession without any UI.
// Available in PIE and development builds, stripped from shipping builds.
//
//   EasySession.Host [Map]      Create a session, optionally traveling to the map.
//   EasySession.Find            Search for sessions and list the results.
//   EasySession.Join [Index] [Password]  Join a result of the last EasySession.Find (default index 0).
//   EasySession.Matchmaking [Map] Search, join the best session, or host one.
//   EasySession.Travel <Map>    ServerTravel the current session to a new map.
//   EasySession.Destroy         Destroy the current session (host closes it, client leaves).
//   EasySession.Start           Start the match (session state -> InProgress).
//   EasySession.End             End the match (session state -> Ended).
//   EasySession.Cancel          Cancel the running matchmaking.
//   EasySession.Status          Print the current session state.
//   EasySession.Players         List the players in the session, numbered for EasySession.Kick.
//   EasySession.Kick <Index> [Reason]  Kick a player listed by EasySession.Players (host only).
//   EasySession.Ready <0|1>     Change whether the local player is ready in the session.
//   EasySession.CreateParty [MaxMembers] [public] [code]  Create a party, hidden unless public.
//   EasySession.LeaveParty      Leave the party.
//   EasySession.Party           List the members of the party.
//   EasySession.PartyReady <0|1>  Change whether the local player is ready in the party.
//   EasySession.FindParties [Code]  Search for parties and list the results.
//   EasySession.JoinParty [Index]  Join a result of the last EasySession.FindParties (default index 0).
//   EasySession.KickParty <Index> [Reason]  Kick a member listed by EasySession.Party (leader only).
//   EasySession.Friends         Read and print the friends list.
//   EasySession.InviteUI        Open the platform invite overlay.
//   EasySession.PartyInviteUI   Open the platform invite overlay for the party.
//   EasySession.Diagnose        Run the online configuration diagnostics.

// UE_BUILD_SHIPPING only exists after Misc/Build.h fills in the configuration macros UBT did not pass.
// Testing it before any include works in a unity build, where some earlier file's includes land first.
// Built standalone, the same test compiles this file against an undefined macro, which the packaging build treats as an error (C4668).
#include "Misc/Build.h"

#if !UE_BUILD_SHIPPING

#include "EasySession.h"
#include "EasySessionDiagnostics.h"
#include "EasySessionStatics.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace EasySessionConsole
{
	static void Print(const FString& Message)
	{
		UE_LOG(LogEasySession, Display, TEXT("%s"), *Message);
		if (GEngine != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 8.0f, FColor::Cyan, FString::Printf(TEXT("[EasySession] %s"), *Message));
		}
	}

	static UEasySessionSubsystem* GetSubsystem(const UWorld* World)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		UEasySessionSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;
		if (Subsystem == nullptr)
		{
			Print(TEXT("EasySession subsystem is not available in this world."));
		}
		return Subsystem;
	}

	// The results of the last EasySession.Find, which EasySession.Join picks from by index.
	static TArray<FEasySessionSearchResult> LastFoundSessions;

	static FEasySessionCompleteDelegate MakePrintDelegate(const FString& Operation)
	{
		return FEasySessionCompleteDelegate::CreateLambda([Operation](EEasySessionResult Result, const FString& ErrorMessage)
		{
			if (Result == EEasySessionResult::Success)
			{
				Print(FString::Printf(TEXT("%s: Success"), *Operation));
			}
			else
			{
				Print(FString::Printf(TEXT("%s: %s (%s)"), *Operation, *EasySession::ResultToString(Result), *ErrorMessage));
			}
		});
	}

	static FAutoConsoleCommandWithWorldAndArgs GHostCommand(
		TEXT("EasySession.Host"),
		TEXT("Create a session and travel to its map, e.g. EasySession.Host /Game/Maps/Lobby"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				FEasySessionHostParams HostParams;
				HostParams.SessionDisplayName = FString::Printf(TEXT("%s's Session"), FPlatformProcess::UserName());
				if (Args.Num() > 0)
				{
					HostParams.InitialMapName = Args[0];
				}

				Print(FString::Printf(TEXT("Hosting session (map: %s)..."), *HostParams.InitialMapName));
				Subsystem->CreateSession(HostParams, MakePrintDelegate(TEXT("Host")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GFindCommand(
		TEXT("EasySession.Find"),
		TEXT("Search for sessions and list the results."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				Print(TEXT("Searching for sessions..."));
				LastFoundSessions.Reset();
				Subsystem->FindSessions(FEasySessionSearchParams(), FEasySessionFindCompleteDelegate::CreateLambda(
					[](EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
					{
						LastFoundSessions = Results;
						if (Result != EEasySessionResult::Success)
						{
							Print(FString::Printf(TEXT("Find: %s (%s)"), *EasySession::ResultToString(Result), *ErrorMessage));
							return;
						}

						Print(FString::Printf(TEXT("Find: %d session(s) found."), Results.Num()));
						for (int32 Index = 0; Index < Results.Num(); ++Index)
						{
							const FEasySessionSearchResult& Session = Results[Index];
							Print(FString::Printf(TEXT("  [%d] '%s' host=%s ping=%dms slots=%d/%d%s"),
								Index,
								*Session.SessionDisplayName,
								*Session.HostName,
								Session.PingInMs,
								Session.MaxPlayers - Session.OpenSlots,
								Session.MaxPlayers,
								Session.bIsDedicatedServer ? TEXT(" (dedicated)") : TEXT("")));
						}
					}));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GJoinCommand(
		TEXT("EasySession.Join"),
		TEXT("Join a result of the last search. Optional arg: result index (default 0)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
				const TArray<FEasySessionSearchResult>& Results = LastFoundSessions;
				if (!Results.IsValidIndex(Index))
				{
					Print(FString::Printf(TEXT("Join: no search result at index %d. Run EasySession.Find first."), Index));
					return;
				}

				const FString Password = Args.Num() > 1 ? Args[1] : FString();
				Print(FString::Printf(TEXT("Joining '%s'..."), *Results[Index].SessionDisplayName));
				Subsystem->JoinSession(Results[Index], Password, FString(), MakePrintDelegate(TEXT("Join")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GMatchmakingCommand(
		TEXT("EasySession.Matchmaking"),
		TEXT("Search and join the best session. Optional arg: map to host a session on when none is found."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				FEasyMatchmakingParams Params;
				Params.Host.SessionDisplayName = FString::Printf(TEXT("%s's Session"), FPlatformProcess::UserName());
				Params.bAllowHostFallback = Args.Num() > 0;
				if (Args.Num() > 0)
				{
					Params.Host.InitialMapName = Args[0];
				}

				Print(TEXT("Matchmaking started..."));
				Subsystem->StartMatchmaking(Params, nullptr, MakePrintDelegate(TEXT("Matchmaking")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GTravelCommand(
		TEXT("EasySession.Travel"),
		TEXT("ServerTravel the current session to a new map, e.g. EasySession.Travel /Game/Maps/Arena"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				if (Args.IsEmpty())
				{
					Print(TEXT("Travel: missing map argument."));
					return;
				}

				const bool bStarted = Subsystem->ServerTravel(Args[0]);
				Print(FString::Printf(TEXT("Travel to '%s': %s"), *Args[0], bStarted ? TEXT("started") : TEXT("failed (host only)")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GDestroyCommand(
		TEXT("EasySession.Destroy"),
		TEXT("Destroy the current session: the host closes it, a client leaves."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				Print(TEXT("Destroying session..."));
				Subsystem->DestroySession(MakePrintDelegate(TEXT("Destroy")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GStartCommand(
		TEXT("EasySession.Start"),
		TEXT("Start the match (session state -> InProgress)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				Print(TEXT("Starting session..."));
				Subsystem->StartSession(MakePrintDelegate(TEXT("Start")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GEndCommand(
		TEXT("EasySession.End"),
		TEXT("End the match (session state -> Ended)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				Print(TEXT("Ending session..."));
				Subsystem->EndSession(MakePrintDelegate(TEXT("End")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GCancelCommand(
		TEXT("EasySession.Cancel"),
		TEXT("Cancel the running matchmaking."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				Subsystem->CancelMatchmaking();
				Print(TEXT("Cancel requested."));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GStatusCommand(
		TEXT("EasySession.Status"),
		TEXT("Print the current session state."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				Print(FString::Printf(TEXT("OSS=%s | InSession=%d | Host=%d | Busy=%d | Matchmaking=%d"),
					*UEasySessionStatics::GetOnlineSubsystemName(World).ToString(),
					Subsystem->IsInSession() ? 1 : 0,
					Subsystem->IsHost() ? 1 : 0,
					Subsystem->IsBusy() ? 1 : 0,
					Subsystem->IsMatchmakingRunning() ? 1 : 0));
				Print(FString::Printf(TEXT("Queue: %s"), *Subsystem->GetQueueStatus()));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GPlayersCommand(
		TEXT("EasySession.Players"),
		TEXT("List the players in the session, numbered for EasySession.Kick."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				const TArray<FEasySessionPlayerInfo> Players = Subsystem->GetSessionPlayerInfos();
				Print(FString::Printf(TEXT("Players: %d in the session."), Players.Num()));
				for (int32 Index = 0; Index < Players.Num(); ++Index)
				{
					Print(FString::Printf(TEXT("  [%d] '%s'%s%s%s"), Index, *Players[Index].PlayerName,
						Players[Index].bIsHost ? TEXT(" (host)") : TEXT(""),
						Players[Index].bIsLocalPlayer ? TEXT(" (you)") : TEXT(""),
						Players[Index].bIsReady ? TEXT(" (ready)") : TEXT("")));
				}
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GKickCommand(
		TEXT("EasySession.Kick"),
		TEXT("Kick a player listed by EasySession.Players. Args: index, then an optional reason."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				const TArray<FEasySessionPlayerInfo> Players = Subsystem->GetSessionPlayerInfos();
				const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : INDEX_NONE;
				if (!Players.IsValidIndex(Index))
				{
					Print(TEXT("Kick: no player at that index. Run EasySession.Players first."));
					return;
				}

				// The reason is every word after the index, so it can hold spaces.
				FString Reason;
				for (int32 ArgIndex = 1; ArgIndex < Args.Num(); ++ArgIndex)
				{
					Reason += (ArgIndex > 1 ? TEXT(" ") : TEXT("")) + Args[ArgIndex];
				}

				const EEasySessionResult Result = Subsystem->KickPlayer(Players[Index], FText::FromString(Reason));
				Print(FString::Printf(TEXT("Kick '%s': %s"), *Players[Index].PlayerName, *EasySession::ResultToString(Result)));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GCreatePartyCommand(
		TEXT("EasySession.CreateParty"),
		TEXT("Create a party. Args: optional max members, then optional words: public to list it, code to advertise a join code."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				FEasyPartySettings PartySettings;
				if (Args.Num() > 0)
				{
					PartySettings.MaxMembers = FCString::Atoi(*Args[0]);
				}
				for (int32 Index = 1; Index < Args.Num(); ++Index)
				{
					if (Args[Index] == TEXT("public"))
					{
						PartySettings.bHidden = false;
					}
					else if (Args[Index] == TEXT("code"))
					{
						PartySettings.bUseJoinCode = true;
					}
				}

				Print(FString::Printf(TEXT("Creating a party (max %d members)..."), PartySettings.MaxMembers));
				Subsystem->CreateParty(PartySettings, MakePrintDelegate(TEXT("CreateParty")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GLeavePartyCommand(
		TEXT("EasySession.LeaveParty"),
		TEXT("Leave the party. A leader who leaves ends it for every member."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				Subsystem->LeaveParty(MakePrintDelegate(TEXT("LeaveParty")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GPartyCommand(
		TEXT("EasySession.Party"),
		TEXT("List the members of the party."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				if (!Subsystem->IsInParty())
				{
					Print(TEXT("Party: not in a party."));
					return;
				}

				const TArray<FEasyPartyMemberInfo> Members = Subsystem->GetPartyMembers();
				Print(FString::Printf(TEXT("Party: %d member(s)."), Members.Num()));
				const FString JoinCode = Subsystem->GetPartyJoinCode();
				if (!JoinCode.IsEmpty())
				{
					Print(FString::Printf(TEXT("  Join code: %s"), *JoinCode));
				}
				for (int32 Index = 0; Index < Members.Num(); ++Index)
				{
					Print(FString::Printf(TEXT("  [%d] '%s'%s%s%s"), Index, *Members[Index].PlayerName,
						Members[Index].bIsLeader ? TEXT(" (leader)") : TEXT(""),
						Members[Index].bIsLocalPlayer ? TEXT(" (you)") : TEXT(""),
						Members[Index].bIsReady ? TEXT(" (ready)") : TEXT("")));
				}
			}
		}));

	// The results of the last EasySession.FindParties, which EasySession.JoinParty picks from by index.
	static TArray<FEasySessionSearchResult> LastFoundParties;

	static FAutoConsoleCommandWithWorldAndArgs GFindPartiesCommand(
		TEXT("EasySession.FindParties"),
		TEXT("Search for parties and list the results. Args: an optional join code."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				FEasySessionSearchParams SearchParams;
				if (Args.Num() > 0)
				{
					SearchParams.JoinCode = Args[0];
				}

				Print(TEXT("Searching for parties..."));
				LastFoundParties.Reset();
				Subsystem->FindParties(SearchParams, FEasySessionFindCompleteDelegate::CreateLambda(
					[](EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
					{
						LastFoundParties = Results;
						if (Result != EEasySessionResult::Success)
						{
							Print(FString::Printf(TEXT("FindParties: %s (%s)"), *EasySession::ResultToString(Result), *ErrorMessage));
							return;
						}

						Print(FString::Printf(TEXT("FindParties: %d party(s) found."), Results.Num()));
						for (int32 Index = 0; Index < Results.Num(); ++Index)
						{
							Print(FString::Printf(TEXT("  [%d] '%s' %d/%d"), Index, *Results[Index].SessionDisplayName,
								Results[Index].MaxPlayers - Results[Index].OpenSlots, Results[Index].MaxPlayers));
						}
					}));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GJoinPartyCommand(
		TEXT("EasySession.JoinParty"),
		TEXT("Join a party listed by EasySession.FindParties. Args: index (default 0)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
				if (!LastFoundParties.IsValidIndex(Index))
				{
					Print(TEXT("JoinParty: no party at that index. Run EasySession.FindParties first."));
					return;
				}

				Subsystem->JoinParty(LastFoundParties[Index], MakePrintDelegate(TEXT("JoinParty")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GKickPartyCommand(
		TEXT("EasySession.KickParty"),
		TEXT("Kick a member listed by EasySession.Party. Args: index, then an optional reason."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				const TArray<FEasyPartyMemberInfo> Members = Subsystem->GetPartyMembers();
				const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : INDEX_NONE;
				if (!Members.IsValidIndex(Index))
				{
					Print(TEXT("KickParty: no member at that index. Run EasySession.Party first."));
					return;
				}

				// The reason is every word after the index, so it can hold spaces.
				FString Reason;
				for (int32 ArgIndex = 1; ArgIndex < Args.Num(); ++ArgIndex)
				{
					Reason += (ArgIndex > 1 ? TEXT(" ") : TEXT("")) + Args[ArgIndex];
				}

				const EEasySessionResult Result = Subsystem->KickPartyMember(Members[Index], FText::FromString(Reason));
				Print(FString::Printf(TEXT("KickParty '%s': %s"), *Members[Index].PlayerName, *EasySession::ResultToString(Result)));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GReadyCommand(
		TEXT("EasySession.Ready"),
		TEXT("Change whether the local player is ready in the session. Args: 1 or 0 (default 1)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				const bool bReady = Args.Num() == 0 || Args[0] != TEXT("0");
				Print(FString::Printf(TEXT("Ready %d: %s"), bReady ? 1 : 0, *EasySession::ResultToString(Subsystem->SetSessionReady(bReady))));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GPartyReadyCommand(
		TEXT("EasySession.PartyReady"),
		TEXT("Change whether the local player is ready in the party. Args: 1 or 0 (default 1)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				const bool bReady = Args.Num() == 0 || Args[0] != TEXT("0");
				Print(FString::Printf(TEXT("PartyReady %d: %s"), bReady ? 1 : 0, *EasySession::ResultToString(Subsystem->SetPartyReady(bReady))));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GFriendsCommand(
		TEXT("EasySession.Friends"),
		TEXT("Read and print the friends list."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				Subsystem->ReadFriends(FEasyFriendsCompleteDelegate::CreateLambda(
					[](EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionFriend>& Friends)
					{
						if (Result != EEasySessionResult::Success)
						{
							Print(FString::Printf(TEXT("Friends: %s (%s)"), *EasySession::ResultToString(Result), *ErrorMessage));
							return;
						}

						Print(FString::Printf(TEXT("Friends: %d"), Friends.Num()));
						for (int32 Index = 0; Index < Friends.Num(); ++Index)
						{
							Print(FString::Printf(TEXT("  [%d] %s | Online=%d | PlayingThisGame=%d"),
								Index, *Friends[Index].DisplayName, Friends[Index].bIsOnline ? 1 : 0, Friends[Index].bIsPlayingThisGame ? 1 : 0));
						}
					}));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GInviteUICommand(
		TEXT("EasySession.InviteUI"),
		TEXT("Open the platform invite overlay for the current session."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				Print(FString::Printf(TEXT("InviteUI: %s"), *EasySession::ResultToString(Subsystem->ShowInviteUI())));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GPartyInviteUICommand(
		TEXT("EasySession.PartyInviteUI"),
		TEXT("Open the platform invite overlay for the party."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				Print(FString::Printf(TEXT("PartyInviteUI: %s"), *EasySession::ResultToString(Subsystem->ShowPartyInviteUI())));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GDiagnoseCommand(
		TEXT("EasySession.Diagnose"),
		TEXT("Run the online configuration diagnostics and log the results."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			// The findings go to the log.
			// The headline goes on screen too, so the command says whether the online subsystem loaded without a log window.
			const EasySessionDiagnostics::FReport Report = EasySessionDiagnostics::RunDiagnostics(World);
			EasySessionDiagnostics::LogReport(Report);
			Print(FString::Printf(TEXT("Diagnose: %s (details in the log)"), *Report.Summary));
		}));

}

#endif // !UE_BUILD_SHIPPING
