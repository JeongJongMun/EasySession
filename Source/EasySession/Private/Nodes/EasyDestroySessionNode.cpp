// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyDestroySessionNode.h"

UEasyDestroySessionNode* UEasyDestroySessionNode::DestroyEasySession(UObject* WorldContextObject)
{
	UEasyDestroySessionNode* Node = NewObject<UEasyDestroySessionNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyDestroySessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."));
		return;
	}

	Subsystem->DestroyEasySession(FEasySessionCompleteDelegate::CreateUObject(this, &UEasyDestroySessionNode::HandleComplete));
}

void UEasyDestroySessionNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
