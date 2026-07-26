// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Nodes/EasySessionNodeBase.h"

#include "EasySessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UEasySessionSubsystem* UEasySessionNodeBase::GetEasySessionSubsystem() const
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(NodeWorldContext, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;
}
