// Copyright Langerak. All Rights Reserved.

#include "Nodes/EasyJoinSessionNode.h"

UEasyJoinSessionNode* UEasyJoinSessionNode::JoinEasySession(UObject* WorldContextObject, const FEasySessionSearchResult& SearchResult, bool bTravelOnSuccess)
{
	UEasyJoinSessionNode* Node = NewObject<UEasyJoinSessionNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->SearchResult = SearchResult;
	Node->bTravelOnSuccess = bTravelOnSuccess;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyJoinSessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."));
		return;
	}

	Subsystem->JoinEasySession(SearchResult, bTravelOnSuccess, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyJoinSessionNode::HandleComplete));
}

void UEasyJoinSessionNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (Result == EEasySessionResult::Success)
	{
		OnSuccess.Broadcast(Result, ErrorMessage);
	}
	else
	{
		OnFailure.Broadcast(Result, ErrorMessage);
	}

	SetReadyToDestroy();
}
