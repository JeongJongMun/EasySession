// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyStartSessionNode.h"

#include "EasySessionMessages.h"

UEasyStartSessionNode* UEasyStartSessionNode::StartEasySession(UObject* WorldContextObject)
{
	UEasyStartSessionNode* Node = NewObject<UEasyStartSessionNode>();
	Node->WorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyStartSessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage);
		return;
	}

	Subsystem->StartSession(FEasySessionCompleteDelegate::CreateUObject(this, &UEasyStartSessionNode::HandleComplete));
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
