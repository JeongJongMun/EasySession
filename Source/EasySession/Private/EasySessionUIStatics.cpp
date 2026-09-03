// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionUIStatics.h"

#define LOCTEXT_NAMESPACE "EasySessionUI"

FText UEasySessionUIStatics::GetResultMessage(EEasySessionResult Result)
{
	switch (Result)
	{
		case EEasySessionResult::Success: return LOCTEXT("Result_Success", "Done");
		case EEasySessionResult::NoOnlineSubsystem: return LOCTEXT("Result_NoOnlineSubsystem", "No online service is available");
		case EEasySessionResult::InvalidParams: return LOCTEXT("Result_InvalidParams", "The settings are invalid");
		case EEasySessionResult::SessionAlreadyExists: return LOCTEXT("Result_SessionAlreadyExists", "You are already in a session");
		case EEasySessionResult::NoSessionExists: return LOCTEXT("Result_NoSessionExists", "There is no session");
		case EEasySessionResult::CreateFailure: return LOCTEXT("Result_CreateFailure", "Could not create the session");
		case EEasySessionResult::SearchFailure: return LOCTEXT("Result_SearchFailure", "The search failed");
		case EEasySessionResult::NoSessionsFound: return LOCTEXT("Result_NoSessionsFound", "No sessions found");
		case EEasySessionResult::MatchmakingAlreadyInProgress: return LOCTEXT("Result_MatchmakingAlreadyInProgress", "Matchmaking is already running");
		case EEasySessionResult::JoinFailure: return LOCTEXT("Result_JoinFailure", "Could not join the session");
		case EEasySessionResult::JoinSessionFull: return LOCTEXT("Result_JoinSessionFull", "The session is full");
		case EEasySessionResult::JoinSessionDoesNotExist: return LOCTEXT("Result_JoinSessionDoesNotExist", "The session no longer exists");
		case EEasySessionResult::WrongPassword: return LOCTEXT("Result_WrongPassword", "Wrong password");
		case EEasySessionResult::JoinRefused: return LOCTEXT("Result_JoinRefused", "The host refused the join");
		case EEasySessionResult::ResolveFailure: return LOCTEXT("Result_ResolveFailure", "Could not reach the host");
		case EEasySessionResult::DestroyFailure: return LOCTEXT("Result_DestroyFailure", "Could not leave the session");
		case EEasySessionResult::UpdateFailure: return LOCTEXT("Result_UpdateFailure", "Could not update the session");
		case EEasySessionResult::StateChangeFailure: return LOCTEXT("Result_StateChangeFailure", "Could not change the match state");
		case EEasySessionResult::Canceled: return LOCTEXT("Result_Canceled", "Canceled");
		case EEasySessionResult::Timeout: return LOCTEXT("Result_Timeout", "The online service did not respond");
		case EEasySessionResult::RequiresSessionAuthority: return LOCTEXT("Result_RequiresSessionAuthority", "Only the host can do that");
		case EEasySessionResult::FriendSearchAlreadyInProgress: return LOCTEXT("Result_FriendSearchAlreadyInProgress", "A friend search is already running");
		case EEasySessionResult::UnknownFailure:
		default:
			return LOCTEXT("Result_UnknownFailure", "Something went wrong");
	}
}

FText UEasySessionUIStatics::GetActivityMessage(EEasySessionActivity Activity)
{
	switch (Activity)
	{
		case EEasySessionActivity::Creating: return LOCTEXT("Activity_Creating", "Creating the session...");
		case EEasySessionActivity::Searching: return LOCTEXT("Activity_Searching", "Searching for sessions...");
		case EEasySessionActivity::Joining: return LOCTEXT("Activity_Joining", "Joining the session...");
		case EEasySessionActivity::Leaving: return LOCTEXT("Activity_Leaving", "Leaving the session...");
		case EEasySessionActivity::Updating: return LOCTEXT("Activity_Updating", "Updating the session...");
		case EEasySessionActivity::Starting: return LOCTEXT("Activity_Starting", "Starting the match...");
		case EEasySessionActivity::Ending: return LOCTEXT("Activity_Ending", "Ending the match...");
		case EEasySessionActivity::Matchmaking: return LOCTEXT("Activity_Matchmaking", "Looking for a match...");
		case EEasySessionActivity::Traveling: return LOCTEXT("Activity_Traveling", "Loading the map...");
		case EEasySessionActivity::None:
		default:
			return FText::GetEmpty();
	}
}

FText UEasySessionUIStatics::FormatMatchmakingStatus(EEasyMatchmakingState State, int32 ElapsedSeconds)
{
	const FText Seconds = FText::AsNumber(FMath::Max(0, ElapsedSeconds));
	switch (State)
	{
		case EEasyMatchmakingState::Searching: return FText::Format(LOCTEXT("Matchmaking_Searching", "Searching... {0}s"), Seconds);
		case EEasyMatchmakingState::Joining: return FText::Format(LOCTEXT("Matchmaking_Joining", "Joining... {0}s"), Seconds);
		case EEasyMatchmakingState::Hosting: return FText::Format(LOCTEXT("Matchmaking_Hosting", "Hosting a session... {0}s"), Seconds);
		case EEasyMatchmakingState::Idle:
		default:
			return LOCTEXT("Matchmaking_Idle", "Ready");
	}
}

FText UEasySessionUIStatics::FormatSessionSlots(const FEasySessionSearchResult& Result)
{
	const int32 Players = FMath::Max(0, Result.MaxPlayers - Result.OpenSlots);
	return FText::Format(LOCTEXT("SessionSlots", "{0}/{1}   ping {2}ms"),
		FText::AsNumber(Players), FText::AsNumber(Result.MaxPlayers), FText::AsNumber(Result.PingInMs));
}

FText UEasySessionUIStatics::GetRegionDisplayName(EEasySessionRegion Region)
{
	return StaticEnum<EEasySessionRegion>()->GetDisplayNameTextByValue(static_cast<int64>(Region));
}

TArray<FText> UEasySessionUIStatics::GetRegionOptions()
{
	const UEnum* Enum = StaticEnum<EEasySessionRegion>();
	TArray<FText> Options;

	// The last entry is the hidden _MAX the engine appends.
	const int32 Count = Enum->NumEnums() - 1;
	Options.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Options.Add(Enum->GetDisplayNameTextByIndex(Index));
	}
	return Options;
}

EEasySessionRegion UEasySessionUIStatics::RegionFromIndex(int32 Index)
{
	const UEnum* Enum = StaticEnum<EEasySessionRegion>();
	if (Index < 0 || Index >= Enum->NumEnums() - 1)
	{
		return EEasySessionRegion::Any;
	}
	return static_cast<EEasySessionRegion>(Enum->GetValueByIndex(Index));
}

#undef LOCTEXT_NAMESPACE
