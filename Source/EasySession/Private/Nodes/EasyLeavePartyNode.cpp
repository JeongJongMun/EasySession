// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyLeavePartyNode.h"

#include "EasySessionMessages.h"

UEasyLeavePartyNode* UEasyLeavePartyNode::LeaveEasyParty(UObject* WorldContextObject)
{
	UEasyLeavePartyNode* Node = NewObject<UEasyLeavePartyNode>();
	Node->WorldContext = WorldContextObject;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyLeavePartyNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage);
		return;
	}

	Subsystem->LeaveParty(FEasySessionCompleteDelegate::CreateUObject(this, &UEasyLeavePartyNode::HandleComplete));
}

void UEasyLeavePartyNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
