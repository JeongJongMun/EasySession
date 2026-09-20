// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyEndSessionNode.h"

#include "EasySessionMessages.h"

UEasyEndSessionNode* UEasyEndSessionNode::EndEasySession(UObject* WorldContextObject)
{
	UEasyEndSessionNode* Node = NewObject<UEasyEndSessionNode>();
	Node->WorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyEndSessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage);
		return;
	}

	Subsystem->EndSession(FEasySessionCompleteDelegate::CreateUObject(this, &UEasyEndSessionNode::HandleComplete));
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
