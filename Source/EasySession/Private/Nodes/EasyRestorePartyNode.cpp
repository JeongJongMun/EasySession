// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyRestorePartyNode.h"

#include "EasySessionMessages.h"

UEasyRestorePartyNode* UEasyRestorePartyNode::RestoreEasyParty(UObject* WorldContextObject, int32 MaxSearchPasses)
{
	UEasyRestorePartyNode* Node = NewObject<UEasyRestorePartyNode>();
	Node->WorldContext = WorldContextObject;
	Node->MaxSearchPasses = MaxSearchPasses;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyRestorePartyNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage);
		return;
	}

	Subsystem->RestoreParty(MaxSearchPasses, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyRestorePartyNode::HandleComplete));
}

void UEasyRestorePartyNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
