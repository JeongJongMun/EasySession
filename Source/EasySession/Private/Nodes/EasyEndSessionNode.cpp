// Copyright Langerak. All Rights Reserved.

#include "Nodes/EasyEndSessionNode.h"

UEasyEndSessionNode* UEasyEndSessionNode::EndEasySession(UObject* WorldContextObject)
{
	UEasyEndSessionNode* Node = NewObject<UEasyEndSessionNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyEndSessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."));
		return;
	}

	Subsystem->EndEasySession(FEasySessionCompleteDelegate::CreateUObject(this, &UEasyEndSessionNode::HandleComplete));
}

void UEasyEndSessionNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
