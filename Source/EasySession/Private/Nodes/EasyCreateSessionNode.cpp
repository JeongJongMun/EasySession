// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyCreateSessionNode.h"

#include "EasySessionMessages.h"

UEasyCreateSessionNode* UEasyCreateSessionNode::CreateEasySession(UObject* WorldContextObject, const FEasySessionHostParams& HostParams)
{
	UEasyCreateSessionNode* Node = NewObject<UEasyCreateSessionNode>();
	Node->WorldContext = WorldContextObject;
	Node->HostParams = HostParams;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyCreateSessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage);
		return;
	}

	Subsystem->CreateSession(HostParams, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyCreateSessionNode::HandleComplete));
}

void UEasyCreateSessionNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
