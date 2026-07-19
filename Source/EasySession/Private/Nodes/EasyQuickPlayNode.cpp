// Copyright Langerak. All Rights Reserved.

#include "Nodes/EasyQuickPlayNode.h"

#include "EasyMatchmakingPolicy.h"

UEasyQuickPlayNode* UEasyQuickPlayNode::QuickPlayEasySession(UObject* WorldContextObject, const FEasyQuickPlayParams& QuickPlayParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass)
{
	UEasyQuickPlayNode* Node = NewObject<UEasyQuickPlayNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->QuickPlayParams = QuickPlayParams;
	Node->PolicyClass = PolicyClass;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyQuickPlayNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."));
		return;
	}

	Subsystem->StartQuickPlay(QuickPlayParams, PolicyClass, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyQuickPlayNode::HandleComplete));
}

void UEasyQuickPlayNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
