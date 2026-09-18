// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

/**
 * Starts or ends the match of the session this game hosts.
 * Start and End differ only in the online subsystem call, the advertised value and the event, so one class runs both.
 *
 * The request has two phases.
 * The first changes the session state with StartSession or EndSession.
 * The second advertises the Match In Progress key with UpdateSession, because the session state never leaves the host.
 * A refused second phase still completes with Success, because the match state itself already changed.
 */
class FEasySessionMatchStateRequest final : public FEasySessionRequest
{
public:

	/** @param InType Start or End. */
	FEasySessionMatchStateRequest(EType InType, FEasySessionCompleteDelegate InOnComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup(bool bAbandoned) override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** @return Whether this request starts the match. False when it ends the match. */
	bool IsStart() const { return Type == EType::Start; }

	/** First phase completion: StartSession or EndSession finished. Sessions with another name are ignored. */
	void HandleStateChangeComplete(FName InSessionName, bool bWasSuccessful);

	/**
	 * Second phase: advertise whether the match is running.
	 *
	 * @return Whether the online subsystem accepted the update.
	 */
	bool AdvertiseMatchInProgress();

	/** Second phase completion: UpdateSession finished. Sessions with another name are ignored. */
	void HandleAdvertiseComplete(FName InSessionName, bool bWasSuccessful);

	/** Replicate the new session state to the clients and complete with Success. */
	void Finish(bool bAdvertised);

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;

	/** Handle for the first phase completion, bound while the request runs. */
	FDelegateHandle StateChangeCompleteHandle;

	/** Handle for the second phase completion, bound while the request runs. */
	FDelegateHandle AdvertiseCompleteHandle;
};
