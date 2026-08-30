// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyMatchmakingNode.h"

#include "EasyMatchmakingPolicy.h"

UEasyMatchmakingNode* UEasyMatchmakingNode::StartEasyMatchmaking(UObject* WorldContextObject, const FEasyMatchmakingParams& MatchmakingParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass)
{
	UEasyMatchmakingNode* Node = NewObject<UEasyMatchmakingNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->MatchmakingParams = MatchmakingParams;
	Node->PolicyClass = PolicyClass;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyMatchmakingNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."));
		return;
	}

	Subsystem->StartMatchmaking(MatchmakingParams, PolicyClass, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyMatchmakingNode::HandleComplete));
}

void UEasyMatchmakingNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
