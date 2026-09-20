// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyReadFriendsNode.h"

#include "EasySessionMessages.h"

UEasyReadFriendsNode* UEasyReadFriendsNode::ReadEasyFriends(UObject* WorldContextObject)
{
	UEasyReadFriendsNode* Node = NewObject<UEasyReadFriendsNode>();
	Node->WorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyReadFriendsNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage, {});
		return;
	}

	Subsystem->ReadFriends(FEasyFriendsCompleteDelegate::CreateUObject(this, &UEasyReadFriendsNode::HandleComplete));
}

void UEasyReadFriendsNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionFriend>& Friends)
{
	if (Result == EEasySessionResult::Success)
	{
		OnSuccess.Broadcast(Result, ErrorMessage, Friends);
	}
	else
	{
		OnFailure.Broadcast(Result, ErrorMessage, Friends);
	}

	SetReadyToDestroy();
}
