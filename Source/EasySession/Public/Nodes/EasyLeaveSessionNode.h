// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyLeaveSessionNode.generated.h"

/**
 * Async node that leaves a session of one type: destroys this game's named session, and for a game session travels to the menu map.
 */
UCLASS()
class EASYSESSION_API UEasyLeaveSessionNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the session was left successfully. For a game session the menu travel has already started. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when there was no party to leave, or the named session could not be destroyed. A game session's menu travel has started regardless. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Leave the session of this type: destroy this game's named session, and for a game session travel to the menu map (Game Default Map).
	 * A leaving host destroys the game session for everyone, and each client receives "The host has left the game." first.
	 * For a custom reason use Destroy Easy Session For Everyone.
	 * A host who leaves the party ends it for every member, and On Party Left fires with Left once the party is left.
	 *
	 * @param Type The game session or the party session to leave.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Leave Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEasyLeaveSessionNode* LeaveEasySession(UObject* WorldContextObject, EEasySessionType Type = EEasySessionType::GameSession);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the leave request. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The type of session to leave. */
	EEasySessionType Type = EEasySessionType::GameSession;
};
