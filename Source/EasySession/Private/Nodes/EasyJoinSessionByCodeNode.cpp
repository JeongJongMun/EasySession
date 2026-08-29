// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasyJoinSessionByCodeNode.h"

UEasyJoinSessionByCodeNode* UEasyJoinSessionByCodeNode::JoinEasySessionByCode(UObject* WorldContextObject, const FString& JoinCode, const FString& Password)
{
	UEasyJoinSessionByCodeNode* Node = NewObject<UEasyJoinSessionByCodeNode>();
	Node->NodeWorldContext = WorldContextObject;
	Node->JoinCode = JoinCode;
	Node->Password = Password;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

void UEasyJoinSessionByCodeNode::Activate()
{
	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		HandleComplete(EEasySessionResult::NoOnlineSubsystem, TEXT("EasySession subsystem is not available."));
		return;
	}

	Subsystem->JoinEasySessionByCode(JoinCode, Password, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyJoinSessionByCodeNode::HandleComplete));
}

void UEasyJoinSessionByCodeNode::HandleComplete(EEasySessionResult Result, const FString& ErrorMessage)
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
