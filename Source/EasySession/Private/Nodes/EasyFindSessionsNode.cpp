// Copyright (c) 2026 Langerak.

#include "Nodes/EasyFindSessionsNode.h"

#include "EasySessionMessages.h"

UEasyFindSessionsNode* UEasyFindSessionsNode::FindEasySessions(UObject* WorldContextObject, EEasySessionType Type, const FEasySessionSearchParams& SearchParams)
{
	UEasyFindSessionsNode* Node = NewObject<UEasyFindSessionsNode>();
	Node->WorldContext = WorldContextObject;
	Node->Type = Type;
	Node->SearchParams = SearchParams;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyFindSessionsNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage, {});
		return;
	}

	Subsystem->FindSessions(Type, SearchParams, FEasySessionFindCompleteDelegate::CreateUObject(this, &UEasyFindSessionsNode::HandleComplete));
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
