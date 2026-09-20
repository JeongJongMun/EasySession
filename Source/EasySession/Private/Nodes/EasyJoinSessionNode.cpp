// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyJoinSessionNode.h"

#include "EasySessionMessages.h"

UEasyJoinSessionNode* UEasyJoinSessionNode::JoinEasySession(UObject* WorldContextObject, const FEasySessionSearchResult& SearchResult, const FString& Password, const FString& AdditionalTravelOptions)
{
	UEasyJoinSessionNode* Node = NewObject<UEasyJoinSessionNode>();
	Node->WorldContext = WorldContextObject;
	Node->SearchResult = SearchResult;
	Node->Password = Password;
	Node->AdditionalTravelOptions = AdditionalTravelOptions;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyJoinSessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage);
		return;
	}

	Subsystem->JoinSession(SearchResult, Password, AdditionalTravelOptions, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyJoinSessionNode::HandleComplete));
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
