// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionPartyRestoreRequest.h"

#include "EasySession.h"
#include "EasySessionCreateRequest.h"
#include "EasySessionFindRequest.h"
#include "EasySessionJoinRequest.h"
#include "EasySessionParty.h"
#include "EasySessionSubsystem.h"

namespace
{
	// Seconds between two searches for the host's party.
	constexpr float PassDelaySeconds = 2.0f;
}

FEasySessionPartyRestoreRequest::FEasySessionPartyRestoreRequest(int32 InMaxSearchPasses, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::PartyRestore, NAME_PartySession)
	, MaxSearchPasses(InMaxSearchPasses)
	, OnComplete(MoveTemp(InOnComplete))
{
}

FEasySessionPartyRestoreRequest::~FEasySessionPartyRestoreRequest()
{
	FTSTicker::GetCoreTicker().RemoveTicker(PassDelayTickerHandle);
}

void FEasySessionPartyRestoreRequest::Execute()
{
	const TOptional<FEasySessionParty::FLastParty>& LastParty = GetContext().Party.GetLastParty();
	if (!LastParty.IsSet())
	{
		Complete(EEasySessionResult::NoSessionExists, TEXT("There is no party of the last match to go back to."));
		return;
	}

	if (GetContext().Subsystem.IsInSession(EEasySessionType::PartySession) || GetContext().Subsystem.IsInSession())
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("Already in a party or a session. A party comes back only outside both."));
		return;
	}

	// The host of the last party creates it again, and its members find it by the host's id, hidden or not.
	if (LastParty->HostId == GetContext().Party.GetLocalPlayerId())
	{
		UE_LOG(LogEasySession, Log, TEXT("Creating the party of the last match again."));
		RunSubRequest(MakeShared<FEasySessionCreateRequest>(LastParty->Settings,
			FEasySessionCompleteDelegate::CreateSP(this, &FEasySessionPartyRestoreRequest::HandleCreateComplete)));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Looking for the party of the last match, hosted by '%s' (up to %d searches)."), *LastParty->HostId.ToString(), MaxSearchPasses);
	StartSearchPass();
}

void FEasySessionPartyRestoreRequest::Cleanup()
{
	FTSTicker::GetCoreTicker().RemoveTicker(PassDelayTickerHandle);
	PassDelayTickerHandle.Reset();
}

void FEasySessionPartyRestoreRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
}

void FEasySessionPartyRestoreRequest::HandleCancel()
{
	// A join that started finishes in the online subsystem anyway, so only a search or the pause between two searches ends here.
	if (bJoining)
	{
		FEasySessionRequest::HandleCancel();
		return;
	}

	Complete(EEasySessionResult::Canceled, TEXT("Restoring the party was canceled."));
}

FString FEasySessionPartyRestoreRequest::GetProgressText() const
{
	return FString::Printf(TEXT(" (search %d of %d)"), FMath::Min(PassesCompleted + 1, MaxSearchPasses), MaxSearchPasses);
}

void FEasySessionPartyRestoreRequest::HandleCreateComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	Complete(Result, ErrorMessage);
}

void FEasySessionPartyRestoreRequest::StartSearchPass()
{
	const FEasySessionParty::FLastParty& LastParty = GetContext().Party.GetLastParty().GetValue();

	FEasySessionSearchParams Search;
	Search.OwnerId = LastParty.HostId;
	Search.bLANQuery = LastParty.bIsLANMatch;
	RunSubRequest(MakeShared<FEasySessionFindRequest>(Search,
		FEasySessionFindCompleteDelegate::CreateSP(this, &FEasySessionPartyRestoreRequest::HandleSearchComplete), NAME_PartySession));
}

void FEasySessionPartyRestoreRequest::HandleSearchComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
{
	if (Result == EEasySessionResult::Success && !Results.IsEmpty())
	{
		bJoining = true;
		RunSubRequest(MakeShared<FEasySessionJoinRequest>(Results[0],
			FEasySessionCompleteDelegate::CreateSP(this, &FEasySessionPartyRestoreRequest::HandleJoinComplete)));
		return;
	}

	// The host may still be in the match, so a pass that found nothing is no failure until the last one.
	++PassesCompleted;
	if (PassesCompleted >= MaxSearchPasses)
	{
		Complete(EEasySessionResult::NoSessionsFound, TEXT("The party host did not come back. Call Restore Easy Party again once the host is back."));
		return;
	}

	PassDelayTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &FEasySessionPartyRestoreRequest::HandlePassDelayElapsed), PassDelaySeconds);
}

void FEasySessionPartyRestoreRequest::HandleJoinComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	bJoining = false;
	Complete(Result, ErrorMessage);
}

bool FEasySessionPartyRestoreRequest::HandlePassDelayElapsed(float DeltaTime)
{
	PassDelayTickerHandle.Reset();
	StartSearchPass();
	return false;
}
