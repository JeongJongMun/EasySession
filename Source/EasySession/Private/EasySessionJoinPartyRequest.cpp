// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionJoinPartyRequest.h"

#include "EasySession.h"
#include "EasySessionDestroyRequest.h"
#include "EasySessionMessages.h"
#include "EasySessionParty.h"
#include "EasySessionSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"

namespace
{
	// Seconds the leader gets to complete the login, from the start of the connection until the member list holds the local player.
	constexpr float LoginTimeoutSeconds = 10.0f;
}

FEasySessionJoinPartyRequest::FEasySessionJoinPartyRequest(const FEasySessionSearchResult& InTarget, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::JoinParty)
	, Target(InTarget)
	, OnComplete(MoveTemp(InOnComplete))
{
}

void FEasySessionJoinPartyRequest::Execute()
{
	if (!Target.IsValid() || !FEasySessionParty::IsPartySession(Target.NativeResult.Session.SessionSettings))
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("The search result to join is not a party. Use one Find Easy Parties returned."));
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, EasySession::NoOnlineSubsystemMessage);
		return;
	}

	if (Sessions->GetNamedSession(SessionName) != nullptr)
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("Already in a party. Call Leave Easy Party first."));
		return;
	}

	// A party closes when its players enter a game session, so none is joined inside one.
	if (GetContext().Subsystem.IsInSession())
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("A party is joined outside a game session. Call Leave Easy Session first."));
		return;
	}

	// The leader tells members apart by their ids, and the login sends this one.
	const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(GetWorld());
	if (!Identity.IsValid() || !Identity->GetUniquePlayerId(0).IsValid())
	{
		Complete(EEasySessionResult::JoinFailure, TEXT("No player is logged in to the online subsystem, so the party leader could not tell who joins."));
		return;
	}

	JoinCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateSP(this, &FEasySessionJoinPartyRequest::HandleJoinSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Joining the party of '%s'"), *Target.SessionDisplayName);

	if (!Sessions->JoinSession(0, SessionName, Target.NativeResult))
	{
		Complete(EEasySessionResult::JoinFailure, TEXT("JoinSession request was rejected by the online subsystem."));
	}
}

void FEasySessionJoinPartyRequest::Cleanup()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
	}

	FTSTicker::GetCoreTicker().RemoveTicker(TimeoutHandle);
	TimeoutHandle.Reset();
}

void FEasySessionJoinPartyRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
}

void FEasySessionJoinPartyRequest::HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type JoinResult)
{
	if (!IsRunning() || InSessionName != SessionName)
	{
		return;
	}

	switch (JoinResult)
	{
		case EOnJoinSessionCompleteResult::Success:
			break;

		case EOnJoinSessionCompleteResult::SessionIsFull:
			Complete(EEasySessionResult::JoinSessionFull, TEXT("The party is full."));
			return;

		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			Complete(EEasySessionResult::JoinSessionDoesNotExist, TEXT("The party no longer exists."));
			return;

		default:
			Complete(EEasySessionResult::JoinFailure, TEXT("The online subsystem failed to join the party."));
			return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	FString ConnectString;
	if (!Sessions.IsValid() || !Sessions->GetResolvedConnectString(SessionName, ConnectString, NAME_BeaconPort) || ConnectString.IsEmpty())
	{
		LeaveAndComplete(EEasySessionResult::ResolveFailure, TEXT("Could not retrieve the address of the party leader."));
		return;
	}

	const FString PartySessionId = Target.NativeResult.Session.SessionInfo.IsValid() ? Target.NativeResult.Session.SessionInfo->GetSessionId().ToString() : FString();
	if (!GetContext().Party.ConnectToLeader(ConnectString, PartySessionId, FEasyPartyConnectComplete::CreateSP(this, &FEasySessionJoinPartyRequest::HandleConnectComplete)))
	{
		LeaveAndComplete(EEasySessionResult::ResolveFailure, TEXT("Could not reach the party leader."));
		return;
	}

	TimeoutHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FEasySessionJoinPartyRequest::HandleTimeout), LoginTimeoutSeconds);
}

void FEasySessionJoinPartyRequest::HandleConnectComplete(bool bSuccess, const FText& Reason)
{
	if (!IsRunning())
	{
		return;
	}

	if (!bSuccess)
	{
		LeaveAndComplete(EEasySessionResult::JoinRefused, Reason.ToString());
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Joined the party."));
	Complete(EEasySessionResult::Success);
}

bool FEasySessionJoinPartyRequest::HandleTimeout(float DeltaTime)
{
	TimeoutHandle.Reset();
	if (IsRunning())
	{
		LeaveAndComplete(EEasySessionResult::JoinRefused, TEXT("The party leader did not answer."));
	}
	return false;
}

void FEasySessionJoinPartyRequest::LeaveAndComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	FTSTicker::GetCoreTicker().RemoveTicker(TimeoutHandle);
	TimeoutHandle.Reset();

	// The party session stays until it is destroyed, and the next join or create would be refused for it.
	GetContext().Party.Close();
	RunSubRequest(MakeShared<FEasySessionDestroyRequest>(FEasySessionCompleteDelegate::CreateSPLambda(this,
		[this, Result, ErrorMessage](EEasySessionResult /*DestroyResult*/, const FString& /*DestroyError*/)
		{
			Complete(Result, ErrorMessage);
		})));
}
