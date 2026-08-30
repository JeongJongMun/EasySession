// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyFindFriendSessionsNode.h"

UEasyFindFriendSessionsNode* UEasyFindFriendSessionsNode::FindEasyFriendSessions(UObject* WorldContextObject)
{
	UEasyFindFriendSessionsNode* Node = NewObject<UEasyFindFriendSessionsNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyFindFriendSessionsNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."), {});
		return;
	}

	Subsystem->FindEasyFriendSessions(FEasyFriendSessionsCompleteDelegate::CreateUObject(this, &UEasyFindFriendSessionsNode::HandleComplete));
}

void UEasyFindFriendSessionsNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasyFriendSession>& FriendSessions)
{
	if (Result == EEasySessionResult::Success)
	{
		OnSuccess.Broadcast(Result, ErrorMessage, FriendSessions);
	}
	else
	{
		OnFailure.Broadcast(Result, ErrorMessage, FriendSessions);
	}

	SetReadyToDestroy();
}
