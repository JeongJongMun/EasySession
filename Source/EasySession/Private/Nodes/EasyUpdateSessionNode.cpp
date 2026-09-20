// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyUpdateSessionNode.h"

#include "EasySessionMessages.h"

UEasyUpdateSessionNode* UEasyUpdateSessionNode::UpdateEasySession(UObject* WorldContextObject, const FEasySessionSettings& NewSettings)
{
	UEasyUpdateSessionNode* Node = NewObject<UEasyUpdateSessionNode>();
	Node->WorldContext = WorldContextObject;
	Node->NewSettings = NewSettings;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyUpdateSessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::InvalidParams, EasySession::NoEasySessionSubsystemMessage);
		return;
	}

	Subsystem->UpdateSession(NewSettings, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyUpdateSessionNode::HandleComplete));
}

void UEasyUpdateSessionNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
