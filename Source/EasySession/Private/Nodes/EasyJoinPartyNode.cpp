// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyJoinPartyNode.h"

#include "EasySessionMessages.h"

UEasyJoinPartyNode* UEasyJoinPartyNode::JoinEasyParty(UObject* WorldContextObject, const FEasySessionSearchResult& SearchResult)
{
	UEasyJoinPartyNode* Node = NewObject<UEasyJoinPartyNode>();
	Node->WorldContext = WorldContextObject;
	Node->SearchResult = SearchResult;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyJoinPartyNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage);
		return;
	}

	Subsystem->JoinParty(SearchResult, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyJoinPartyNode::HandleComplete));
}

void UEasyJoinPartyNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
