// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyLeaveSessionNode.h"

UEasyLeaveSessionNode* UEasyLeaveSessionNode::LeaveEasySession(UObject* WorldContextObject)
{
	UEasyLeaveSessionNode* Node = NewObject<UEasyLeaveSessionNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyLeaveSessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."));
		return;
	}

	Subsystem->LeaveEasySession(FEasySessionCompleteDelegate::CreateUObject(this, &UEasyLeaveSessionNode::HandleComplete));
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
