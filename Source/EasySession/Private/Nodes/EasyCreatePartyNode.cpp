// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyCreatePartyNode.h"

#include "EasySessionMessages.h"

UEasyCreatePartyNode* UEasyCreatePartyNode::CreateEasyParty(UObject* WorldContextObject, const FEasyPartyParams& PartyParams)
{
	UEasyCreatePartyNode* Node = NewObject<UEasyCreatePartyNode>();
	Node->WorldContext = WorldContextObject;
	Node->PartyParams = PartyParams;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyCreatePartyNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage);
		return;
	}

	Subsystem->CreateParty(PartyParams, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyCreatePartyNode::HandleComplete));
}

void UEasyCreatePartyNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
