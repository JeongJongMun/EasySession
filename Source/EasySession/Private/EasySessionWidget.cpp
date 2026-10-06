// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionWidget.h"

#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"

UEasySessionSubsystem* UEasySessionWidget::GetEasySessionSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;
}

void UEasySessionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem();
	if (Subsystem == nullptr)
	{
		return;
	}

	// The implementable events are UFunctions, so the dynamic delegates call them by name and the Blueprint override runs.
	Subsystem->OnBusyChanged.AddUniqueDynamic(this, &UEasySessionWidget::OnBusyChanged);
	Subsystem->OnSessionStateChanged.AddUniqueDynamic(this, &UEasySessionWidget::OnSessionStateChanged);
	Subsystem->OnSessionSettingsChanged.AddUniqueDynamic(this, &UEasySessionWidget::OnSessionSettingsChanged);
	Subsystem->OnSessionPlayersChanged.AddUniqueDynamic(this, &UEasySessionWidget::OnSessionPlayersChanged);
	Subsystem->OnSessionFailure.AddUniqueDynamic(this, &UEasySessionWidget::OnSessionFailure);
	Subsystem->OnSessionInviteAccepted.AddUniqueDynamic(this, &UEasySessionWidget::OnSessionInviteAccepted);
	Subsystem->OnMatchmakingStarted.AddUniqueDynamic(this, &UEasySessionWidget::OnMatchmakingStarted);
	Subsystem->OnMatchmakingStateChanged.AddUniqueDynamic(this, &UEasySessionWidget::OnMatchmakingStateChanged);
	Subsystem->OnMatchmakingUpdated.AddUniqueDynamic(this, &UEasySessionWidget::OnMatchmakingUpdated);
	Subsystem->OnMatchmakingComplete.AddUniqueDynamic(this, &UEasySessionWidget::OnMatchmakingComplete);
	Subsystem->OnPartyMembersChanged.AddUniqueDynamic(this, &UEasySessionWidget::OnPartyMembersChanged);
	Subsystem->OnPartyLeft.AddUniqueDynamic(this, &UEasySessionWidget::OnPartyLeft);
}

void UEasySessionWidget::NativeDestruct()
{
	if (UEasySessionSubsystem* Subsystem = GetEasySessionSubsystem())
	{
		Subsystem->OnBusyChanged.RemoveAll(this);
		Subsystem->OnSessionStateChanged.RemoveAll(this);
		Subsystem->OnSessionSettingsChanged.RemoveAll(this);
		Subsystem->OnSessionPlayersChanged.RemoveAll(this);
		Subsystem->OnSessionFailure.RemoveAll(this);
		Subsystem->OnSessionInviteAccepted.RemoveAll(this);
		Subsystem->OnMatchmakingStarted.RemoveAll(this);
		Subsystem->OnMatchmakingStateChanged.RemoveAll(this);
		Subsystem->OnMatchmakingUpdated.RemoveAll(this);
		Subsystem->OnMatchmakingComplete.RemoveAll(this);
		Subsystem->OnPartyMembersChanged.RemoveAll(this);
		Subsystem->OnPartyLeft.RemoveAll(this);
	}

	Super::NativeDestruct();
}
