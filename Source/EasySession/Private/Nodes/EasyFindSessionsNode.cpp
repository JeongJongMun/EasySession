// Copyright Langerak. All Rights Reserved.

#include "Nodes/EasyFindSessionsNode.h"

UEasyFindSessionsNode* UEasyFindSessionsNode::FindEasySessions(UObject* WorldContextObject, const FEasySessionSearchParams& SearchParams)
{
	UEasyFindSessionsNode* Node = NewObject<UEasyFindSessionsNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->SearchParams = SearchParams;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyFindSessionsNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."), TArray<FEasySessionSearchResult>());
		return;
	}

	Subsystem->FindEasySessions(SearchParams, FEasySessionFindCompleteDelegate::CreateUObject(this, &UEasyFindSessionsNode::HandleComplete));
}

void UEasyFindSessionsNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
{
	if (Result == EEasySessionResult::Success)
	{
		OnSuccess.Broadcast(Result, ErrorMessage, Results);
	}
	else
	{
		OnFailure.Broadcast(Result, ErrorMessage, Results);
	}

	SetReadyToDestroy();
}
