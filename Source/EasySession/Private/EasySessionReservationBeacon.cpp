// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionReservationBeacon.h"

#include "EasySession.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "TimerManager.h"

namespace
{
	/**
	 * How long the whole request gets before it completes as Unreachable: reaching the host's beacon and its response.
	 * A host that advertises a beacon port nothing listens on never refuses the connection, so the joining player would wait for the engine's own retries.
	 */
	constexpr float RequestTimeoutSeconds = 5.0f;
}

FPlayerReservation EasySessionReservation::MakeReservation(const FUniqueNetIdRepl& PlayerId)
{
	FPlayerReservation Member;
	Member.UniqueId = PlayerId;
	Member.Platform = IOnlineSubsystem::GetLocalPlatformName();
	return Member;
}

TArray<FPlayerReservation> EasySessionReservation::MakeReservations(const FUniqueNetIdRepl& LeaderId, const TArray<FUniqueNetIdRepl>& GroupMembers)
{
	TArray<FPlayerReservation> Members;
	Members.Add(MakeReservation(LeaderId));

	for (const FUniqueNetIdRepl& MemberId : GroupMembers)
	{
		const bool bAlreadyListed = Members.ContainsByPredicate([&MemberId](const FPlayerReservation& Listed) { return Listed.UniqueId == MemberId; });
		if (MemberId.IsValid() && !bAlreadyListed)
		{
			Members.Add(MakeReservation(MemberId));
		}
	}

	return Members;
}

FEasyReservationResponse FEasyReservationResponse::Unreachable()
{
	FEasyReservationResponse Response;
	Response.Result = EEasyReservationResult::Unreachable;
	Response.ReasonText = TEXT("Could not reach the host to ask about joining.");
	return Response;
}

FEasyReservationResponse FEasyReservationResponse::NotAnswering()
{
	FEasyReservationResponse Response;
	Response.Result = EEasyReservationResult::Refused;
	Response.ReasonText = TEXT("The host is not answering join requests.");
	return Response;
}

AEasySessionReservationBeaconClient::AEasySessionReservationBeaconClient()
{
}

bool AEasySessionReservationBeaconClient::RequestJoin(const FEasySessionSearchResult& Target, const FString& Password, const TArray<FUniqueNetIdRepl>& GroupMembers, const FEasyReservationRequestComplete& OnComplete)
{
	CompleteDelegate = OnComplete;
	PasswordToSend = Password;

	OnReservationRequestComplete().BindUObject(this, &AEasySessionReservationBeaconClient::HandleReservationComplete);

	const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	const FUniqueNetIdRepl LocalPlayerId = LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl();

	const TArray<FPlayerReservation> Members = EasySessionReservation::MakeReservations(LocalPlayerId, GroupMembers);

	// The parent resolves the beacon address, opens the connection, and holds the request until OnConnected.
	if (!RequestReservation(Target.NativeResult, LocalPlayerId, Members))
	{
		SignalUnreachable(TEXT("the search result does not resolve to a beacon address"));
		return false;
	}

	// Started here rather than on connect, because reaching the beacon at all is the part that can hang.
	GetWorldTimerManager().SetTimer(RequestTimeoutHandle, this, &AEasySessionReservationBeaconClient::HandleRequestTimeout, RequestTimeoutSeconds, false);
	return true;
}

void AEasySessionReservationBeaconClient::OnConnected()
{
	ServerSendPassword(PasswordToSend);

	Super::OnConnected();
}

void AEasySessionReservationBeaconClient::ServerSendPassword_Implementation(const FString& Password)
{
	// Runs on the host, on the copy of this actor the beacon host created for this connection.
	ReceivedPassword = Password;
}

void AEasySessionReservationBeaconClient::ClientReceiveRefusal_Implementation(EEasyReservationResult Result, const FString& Reason)
{
	GetWorldTimerManager().ClearTimer(RequestTimeoutHandle);

	FEasyReservationResponse Response;
	Response.Result = Result;
	Response.ReasonText = Reason;
	Signal(Response);
}

FEasyReservationResponse AEasySessionReservationBeaconClient::MakeResponseFromReservationResult(EPartyReservationResult::Type Result)
{
	FEasyReservationResponse Response;
	switch (Result)
	{
		case EPartyReservationResult::ReservationAccepted:
			Response.Result = EEasyReservationResult::Approved;
			return Response;

		case EPartyReservationResult::ReservationDuplicate:
			Response.Result = EEasyReservationResult::Approved;
			return Response;

		case EPartyReservationResult::PartyLimitReached:
		case EPartyReservationResult::IncorrectPlayerCount:
			Response.Result = EEasyReservationResult::SessionFull;
			Response.ReasonText = TEXT("The session is full.");
			return Response;

		case EPartyReservationResult::RequestTimedOut:
			return FEasyReservationResponse::Unreachable();

		case EPartyReservationResult::ReservationDenied_ContainsExistingPlayers:
			Response.Result = EEasyReservationResult::Refused;
			Response.ReasonText = TEXT("Some of these players are already in the session.");
			return Response;

		default:
			Response.Result = EEasyReservationResult::Refused;
			Response.ReasonText = FString::Printf(TEXT("The host refused the join (%s)."), EPartyReservationResult::ToString(Result));
			return Response;
	}
}

void AEasySessionReservationBeaconClient::HandleReservationComplete(EPartyReservationResult::Type Result)
{
	GetWorldTimerManager().ClearTimer(RequestTimeoutHandle);
	Signal(MakeResponseFromReservationResult(Result));
}

void AEasySessionReservationBeaconClient::OnFailure()
{
	// Covers refused, unreachable and timed out connections.
	// Deliberately after the Super call, which marks the connection Invalid and destroys the beacon's net driver.
	// Signal keeps a late failure from reporting twice.
	Super::OnFailure();

	GetWorldTimerManager().ClearTimer(RequestTimeoutHandle);
	SignalUnreachable(TEXT("the beacon connection failed"));
}

void AEasySessionReservationBeaconClient::DestroyBeacon()
{
	// A destroyed request must not respond. Dropping the delegate here means that destroying the beacon cancels the request.
	CompleteDelegate.Unbind();
	GetWorldTimerManager().ClearTimer(RequestTimeoutHandle);

	Super::DestroyBeacon();
}

void AEasySessionReservationBeaconClient::HandleRequestTimeout()
{
	SignalUnreachable(TEXT("the host's beacon did not answer in time"));
}

void AEasySessionReservationBeaconClient::Signal(const FEasyReservationResponse& Response)
{
	if (bCompleted)
	{
		return;
	}

	bCompleted = true;

	// The handler may destroy this actor, whose DestroyBeacon unbinds CompleteDelegate.
	// A moved local keeps the executing delegate out of that unbind.
	FEasyReservationRequestComplete LocalDelegate = MoveTemp(CompleteDelegate);
	LocalDelegate.ExecuteIfBound(Response);
}

void AEasySessionReservationBeaconClient::SignalUnreachable(const TCHAR* LogWhy)
{
	if (!bCompleted)
	{
		UE_LOG(LogEasySession, Warning, TEXT("Reservation request could not reach the host: %s."), LogWhy);
	}

	Signal(FEasyReservationResponse::Unreachable());
}

AEasySessionReservationBeaconHost::AEasySessionReservationBeaconHost()
{
	ClientBeaconActorClass = AEasySessionReservationBeaconClient::StaticClass();
	BeaconTypeName = ClientBeaconActorClass->GetName();

	// The parent refuses every reservation without a platform auth ticket, which this plugin never sends.
	bIsValidationStrRequired = false;
}

void AEasySessionReservationBeaconHost::WaitForEveryoneToArrive()
{
	if (State == nullptr)
	{
		return;
	}

	// The parent clears this only when a player it still waits for arrives, so without this every map change adds to the last one's wait.
	for (FPartyReservation& Reservation : State->GetReservations())
	{
		for (FPlayerReservation& Member : Reservation.PartyMembers)
		{
			Member.ElapsedTime = 0.0f;
			NewPlayerAdded(Member);
		}
	}
}

void AEasySessionReservationBeaconHost::RemovePlayerReservation(const FUniqueNetIdRepl& PlayerId)
{
	// The same two calls the parent's Tick makes when a player times out.
	// A player who leaves in the frame they arrived is still in PlayersPendingJoin, and left there their next join is refused.
	FPlayerReservation LeavingPlayer;
	LeavingPlayer.UniqueId = PlayerId;
	PlayerRemoved(LeavingPlayer);
	HandlePlayerLogout(PlayerId);
}

void AEasySessionReservationBeaconHost::ProcessReservationRequest(APartyBeaconClient* Client, const FString& SessionId, const FPartyReservation& ReservationRequest)
{
	AEasySessionReservationBeaconClient* ReservationClient = Cast<AEasySessionReservationBeaconClient>(Client);
	if (ReservationClient == nullptr)
	{
		Super::ProcessReservationRequest(Client, SessionId, ReservationRequest);
		return;
	}

	// The password arrived on the RPC before this one, so the join is decided before any reservation is added.
	// GetUniqueId is the id the joining player presented at beacon login, which the engine already checked.
	const FEasyReservationResponse Response = ApproveJoinDelegate.IsBound()
		? ApproveJoinDelegate.Execute(ReservationClient->GetReceivedPassword(), ReservationClient->GetUniqueId())
		: FEasyReservationResponse::NotAnswering();
	if (Response.Result != EEasyReservationResult::Approved)
	{
		ReservationClient->ClientReceiveRefusal(Response.Result, Response.ReasonText);
		return;
	}

	// The parent checks that the session has room, adds the reservation, and sends the result.
	Super::ProcessReservationRequest(Client, SessionId, ReservationRequest);
}
