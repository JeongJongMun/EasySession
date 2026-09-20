// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyFindFriendSessionsNode.h"

#include "EasySessionMessages.h"

UEasyFindFriendSessionsNode* UEasyFindFriendSessionsNode::FindEasyFriendSessions(UObject* WorldContextObject)
{
	UEasyFindFriendSessionsNode* Node = NewObject<UEasyFindFriendSessionsNode>();
	Node->WorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyFindFriendSessionsNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage, {});
		return;
	}

	Subsystem->FindFriendSessions(FEasyFriendSessionsCompleteDelegate::CreateUObject(this, &UEasyFindFriendSessionsNode::HandleComplete));
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
