// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyQuickMatchNode.h"

#include "EasyQuickMatchPolicy.h"

UEasyQuickMatchNode* UEasyQuickMatchNode::QuickMatchEasySession(UObject* WorldContextObject, const FEasyQuickMatchParams& QuickMatchParams, TSubclassOf<UEasyQuickMatchPolicy> PolicyClass)
{
	UEasyQuickMatchNode* Node = NewObject<UEasyQuickMatchNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->QuickMatchParams = QuickMatchParams;
	Node->PolicyClass = PolicyClass;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyQuickMatchNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."));
		return;
	}

	Subsystem->StartQuickMatch(QuickMatchParams, PolicyClass, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyQuickMatchNode::HandleComplete));
}

void UEasyQuickMatchNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
