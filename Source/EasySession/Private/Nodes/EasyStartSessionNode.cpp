// Copyright Langerak. All Rights Reserved.

#include "Nodes/EasyStartSessionNode.h"

UEasyStartSessionNode* UEasyStartSessionNode::StartEasySession(UObject* WorldContextObject)
{
	UEasyStartSessionNode* Node = NewObject<UEasyStartSessionNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyStartSessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."));
		return;
	}

	Subsystem->StartEasySession(FEasySessionCompleteDelegate::CreateUObject(this, &UEasyStartSessionNode::HandleComplete));
}

void UEasyStartSessionNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
