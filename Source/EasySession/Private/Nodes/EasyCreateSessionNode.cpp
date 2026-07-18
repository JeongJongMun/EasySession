// Copyright Langerak. All Rights Reserved.

#include "Nodes/EasyCreateSessionNode.h"

UEasyCreateSessionNode* UEasyCreateSessionNode::CreateEasySession(UObject* WorldContextObject, const FEasySessionHostParams& HostParams)
{
	UEasyCreateSessionNode* Node = NewObject<UEasyCreateSessionNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->HostParams = HostParams;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyCreateSessionNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."));
		return;
	}

	Subsystem->CreateEasySession(HostParams, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyCreateSessionNode::HandleComplete));
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
