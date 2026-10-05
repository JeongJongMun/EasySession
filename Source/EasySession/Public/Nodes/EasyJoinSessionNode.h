// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyJoinSessionNode.generated.h"

/**
 * Async node that joins a session found by a search.
 */
UCLASS()
class EASYSESSION_API UEasyJoinSessionNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the session was joined successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the session could not be joined. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Join the given session and travel to the host.
	 * A player in another session destroys it first, once the new host approved the join.
	 * A join that fails after that destroy travels the player to the menu.
	 * Joining the session this player is already in fails with Session Already Exists, and so does a join by the host of a match in progress.
	 * A player in a session stays in it when the reservation beacon cannot reach the new host.
	 * A party leader, or the host of a match that has not started, brings the group, which follows before this player joins.
	 * A party member fails with In Party.
	 *
	 * @param SearchResult A search result returned by Find Easy Sessions.
	 * @param Password Password for password protected sessions (see Password Protected on the search result).
	 * @param AdditionalTravelOptions Extra options appended to the travel URL (e.g. "Team=1?MyOption=2").
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Join Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "SearchResult", AdvancedDisplay = "Password,AdditionalTravelOptions"))
	static UEasyJoinSessionNode* JoinEasySession(UObject* WorldContextObject, const FEasySessionSearchResult& SearchResult, const FString& Password = TEXT(""), const FString& AdditionalTravelOptions = TEXT(""));

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the join request. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The session to join. */
	FEasySessionSearchResult SearchResult;

	/** Password for password protected sessions. */
	FString Password;

	/** Extra options appended to the travel URL. */
	FString AdditionalTravelOptions;
};
