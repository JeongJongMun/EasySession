// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyLeaveSessionNode.h"

#include "EasySessionMessages.h"

UEasyLeaveSessionNode* UEasyLeaveSessionNode::LeaveEasySession(UObject* WorldContextObject)
{
	UEasyLeaveSessionNode* Node = NewObject<UEasyLeaveSessionNode>();
	Node->WorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyLeaveSessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage);
		return;
	}

	Subsystem->LeaveSession(FEasySessionCompleteDelegate::CreateUObject(this, &UEasyLeaveSessionNode::HandleComplete));
}

void UEasyLeaveSessionNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
