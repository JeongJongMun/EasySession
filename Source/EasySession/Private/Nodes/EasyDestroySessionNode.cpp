// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyDestroySessionNode.h"

#include "EasySessionMessages.h"

UEasyDestroySessionNode* UEasyDestroySessionNode::DestroyEasySession(UObject* WorldContextObject)
{
	UEasyDestroySessionNode* Node = NewObject<UEasyDestroySessionNode>();
	Node->WorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyDestroySessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage);
		return;
	}

	Subsystem->DestroySession(FEasySessionCompleteDelegate::CreateUObject(this, &UEasyDestroySessionNode::HandleComplete));
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
