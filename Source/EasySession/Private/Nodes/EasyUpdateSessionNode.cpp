// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyUpdateSessionNode.h"

UEasyUpdateSessionNode* UEasyUpdateSessionNode::UpdateEasySession(UObject* WorldContextObject, const FEasySessionHostParams& NewHostParams)
{
	UEasyUpdateSessionNode* Node = NewObject<UEasyUpdateSessionNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->NewHostParams = NewHostParams;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyUpdateSessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."));
		return;
	}

	Subsystem->UpdateEasySession(NewHostParams, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyUpdateSessionNode::HandleComplete));
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
