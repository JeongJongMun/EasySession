// Copyright Langerak. All Rights Reserved.

#include "Nodes/EasyReadFriendsNode.h"

UEasyReadFriendsNode* UEasyReadFriendsNode::ReadEasyFriends(UObject* WorldContextObject)
{
	UEasyReadFriendsNode* Node = NewObject<UEasyReadFriendsNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyReadFriendsNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."), TArray<FEasySessionFriend>());
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
