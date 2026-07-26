// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyJoinSessionNode.h"

UEasyJoinSessionNode* UEasyJoinSessionNode::JoinEasySession(UObject* WorldContextObject, const FEasySessionSearchResult& SearchResult, bool bTravelOnSuccess, const FString& Password, const FString& AdditionalTravelOptions)
{
	UEasyJoinSessionNode* Node = NewObject<UEasyJoinSessionNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->SearchResult = SearchResult;
	Node->bTravelOnSuccess = bTravelOnSuccess;
	Node->Password = Password;
	Node->AdditionalTravelOptions = AdditionalTravelOptions;
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

	Subsystem->JoinEasySession(SearchResult, bTravelOnSuccess, Password, AdditionalTravelOptions, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyJoinSessionNode::HandleComplete));
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
