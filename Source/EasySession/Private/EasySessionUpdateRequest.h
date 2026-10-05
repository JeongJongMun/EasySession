// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

/**
 * FEasySessionUpdateRequest advertises new settings for the session this game hosts.
 * The host side gets the new settings only after the online subsystem accepted the update, so a refused update leaves both unchanged.
 *
 * The subsystem creates it for UpdateSession.
 */
class FEasySessionUpdateRequest final : public FEasySessionRequest
{
public:

	FEasySessionUpdateRequest(const FEasySessionSettings& InSettings, FEasySessionCompleteDelegate InOnComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** The online subsystem finished updating a session. Sessions with another name are ignored. */
	void HandleUpdateSessionComplete(FName InSessionName, bool bWasSuccessful);

	/** The settings to advertise in place of the current ones. */
	FEasySessionSettings Settings;

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;

	/** Handle for the online subsystem's update completion, bound while the request runs. */
	FDelegateHandle UpdateCompleteHandle;
};
