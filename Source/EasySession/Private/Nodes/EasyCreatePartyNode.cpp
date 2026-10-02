// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyCreatePartyNode.h"

#include "EasySessionMessages.h"

UEasyCreatePartyNode* UEasyCreatePartyNode::CreateEasyParty(UObject* WorldContextObject, const FEasyPartySettings& PartySettings)
{
	UEasyCreatePartyNode* Node = NewObject<UEasyCreatePartyNode>();
	Node->WorldContext = WorldContextObject;
	Node->PartySettings = PartySettings;
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

	Subsystem->CreateParty(PartySettings, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyCreatePartyNode::HandleComplete));
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
