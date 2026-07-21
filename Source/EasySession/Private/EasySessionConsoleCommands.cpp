// Copyright Langerak. All Rights Reserved.

// Debug console commands for testing EasySession without any UI.
// Available in PIE and development builds, stripped from shipping builds.
//
//   EasySession.Host [Map]      Create a session, optionally traveling to the map.
//   EasySession.Find            Search for sessions and list the results.
//   EasySession.Join [Index] [Password]  Join a result of the last search (default index 0).
//   EasySession.QuickPlay [Map] Search, join the best session, or host one.
//   EasySession.Travel <Map>    ServerTravel the current session to a new map.
//   EasySession.Leave           Leave / destroy the current session.
//   EasySession.Start           Start the match (session state -> InProgress).
//   EasySession.End             End the match (session state -> Ended).
//   EasySession.Cancel          Cancel the running matchmaking.
//   EasySession.Status          Print the current session state.
//   EasySession.Friends         Read and print the friends list.
//   EasySession.Invites         Print the pending session invites.
//   EasySession.InviteUI        Open the platform invite overlay.
//   EasySession.Diagnose        Run the online configuration diagnostics.

#if !UE_BUILD_SHIPPING

#include "EasySession.h"
#include "EasySessionDiagnostics.h"
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
		TEXT("Create a session. Optional arg: map to travel to, e.g. EasySession.Host /Game/Maps/Lobby"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				FEasySessionHostParams HostParams;
				HostParams.SessionDisplayName = FString::Printf(TEXT("%s's Session"), FPlatformProcess::UserName());
				if (Args.Num() > 0)
				{
					HostParams.MapName = Args[0];
				}

				Print(FString::Printf(TEXT("Hosting session%s..."), HostParams.MapName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (map: %s)"), *HostParams.MapName)));
				Subsystem->CreateEasySession(HostParams, MakePrintDelegate(TEXT("Host")));
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
				Subsystem->FindEasySessions(FEasySessionSearchParams(), FEasySessionFindCompleteDelegate::CreateLambda(
					[](EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
					{
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
				const TArray<FEasySessionSearchResult>& Results = Subsystem->GetLastSearchResults();
				if (!Results.IsValidIndex(Index))
				{
					Print(FString::Printf(TEXT("Join: no search result at index %d. Run EasySession.Find first."), Index));
					return;
				}

				const FString Password = Args.Num() > 1 ? Args[1] : FString();
				Print(FString::Printf(TEXT("Joining '%s'..."), *Results[Index].SessionDisplayName));
				Subsystem->JoinEasySession(Results[Index], true, Password, FString(), MakePrintDelegate(TEXT("Join")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GQuickPlayCommand(
		TEXT("EasySession.QuickPlay"),
		TEXT("Search, join the best session, or host one. Optional arg: map for the fallback host session."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				FEasyQuickPlayParams Params;
				Params.Host.SessionDisplayName = FString::Printf(TEXT("%s's Session"), FPlatformProcess::UserName());
				if (Args.Num() > 0)
				{
					Params.Host.MapName = Args[0];
				}

				Print(TEXT("QuickPlay started..."));
				Subsystem->StartQuickPlay(Params, nullptr, MakePrintDelegate(TEXT("QuickPlay")));
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

				const bool bStarted = Subsystem->ServerTravelToMap(Args[0]);
				Print(FString::Printf(TEXT("Travel to '%s': %s"), *Args[0], bStarted ? TEXT("started") : TEXT("failed (host only)")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GLeaveCommand(
		TEXT("EasySession.Leave"),
		TEXT("Leave / destroy the current session."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				Print(TEXT("Leaving session..."));
				Subsystem->DestroyEasySession(MakePrintDelegate(TEXT("Leave")));
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
				Subsystem->StartEasySession(MakePrintDelegate(TEXT("Start")));
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
				Subsystem->EndEasySession(MakePrintDelegate(TEXT("End")));
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
				Print(FString::Printf(TEXT("OSS=%s | InSession=%d | Host=%d | Busy=%d | Matchmaking=%d | LastResults=%d"),
					*Subsystem->GetOnlineSubsystemName().ToString(),
					Subsystem->IsInSession() ? 1 : 0,
					Subsystem->IsHost() ? 1 : 0,
					Subsystem->IsBusy() ? 1 : 0,
					Subsystem->IsMatchmaking() ? 1 : 0,
					Subsystem->GetLastSearchResults().Num()));
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

	static FAutoConsoleCommandWithWorldAndArgs GInvitesCommand(
		TEXT("EasySession.Invites"),
		TEXT("Print the pending session invites."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				const TArray<FEasySessionInvite>& Invites = Subsystem->GetPendingSessionInvites();
				Print(FString::Printf(TEXT("Pending invites: %d"), Invites.Num()));
				for (int32 Index = 0; Index < Invites.Num(); ++Index)
				{
					Print(FString::Printf(TEXT("  [%d] From=%s | Session='%s'"),
						Index, *Invites[Index].FromUserId, *Invites[Index].Session.SessionDisplayName));
				}
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GInviteUICommand(
		TEXT("EasySession.InviteUI"),
		TEXT("Open the platform invite overlay for the current session."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEasySessionSubsystem* Subsystem = GetSubsystem(World))
			{
				Print(FString::Printf(TEXT("InviteUI: %s"), Subsystem->ShowInviteUI() ? TEXT("opened") : TEXT("not supported")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GDiagnoseCommand(
		TEXT("EasySession.Diagnose"),
		TEXT("Run the online configuration diagnostics and log the results."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			EasySessionDiagnostics::RunDiagnostics(World);
		}));
}

#endif // !UE_BUILD_SHIPPING
