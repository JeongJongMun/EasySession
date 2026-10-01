// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyFindPartiesNode.h"

#include "EasySessionMessages.h"

UEasyFindPartiesNode* UEasyFindPartiesNode::FindEasyParties(UObject* WorldContextObject, const FEasySessionSearchParams& SearchParams)
{
	UEasyFindPartiesNode* Node = NewObject<UEasyFindPartiesNode>();
	Node->WorldContext = WorldContextObject;
	Node->SearchParams = SearchParams;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyFindPartiesNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage, {});
		return;
	}

	Subsystem->FindParties(SearchParams, FEasySessionFindCompleteDelegate::CreateUObject(this, &UEasyFindPartiesNode::HandleComplete));
}

void UEasyFindPartiesNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
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
