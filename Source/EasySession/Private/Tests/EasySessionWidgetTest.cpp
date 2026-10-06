// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestWidget.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

/**
 * UEasySessionWidget binds every subsystem event while it is constructed and unbinds them when it is destructed.
 * The widget has no Blueprint here, so the test reads the subsystem's delegates instead of waiting for an event.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionWidgetBindsEventsTest, "EasySession.Widget.BindsTheSubsystemEventsWhileConstructed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionWidgetBindsEventsTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(GameInstance);

	UEasySessionSubsystem* Subsystem = GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(GameInstance.Get());
		return false;
	}

	TStrongObjectPtr<UEasySessionTestWidget> Widget(CreateWidget<UEasySessionTestWidget>(GameInstance.Get()));
	if (!TestNotNull(TEXT("The widget was created"), Widget.Get()))
	{
		EasySessionTest::DestroyGameInstance(GameInstance.Get());
		return false;
	}

	TestEqual(TEXT("The widget finds the subsystem"), Widget->GetEasySessionSubsystem(), Subsystem);
	TestFalse(TEXT("Nothing is bound before the widget is constructed"), Subsystem->OnBusyChanged.Contains(Widget.Get(), FName(TEXT("OnBusyChanged"))));

	// Building the Slate widget constructs the user widget, and releasing it destructs the user widget.
	TSharedPtr<SWidget> SlateWidget = Widget->TakeWidget();
	TestTrue(TEXT("On Busy Changed is bound while constructed"), Subsystem->OnBusyChanged.Contains(Widget.Get(), FName(TEXT("OnBusyChanged"))));
	TestTrue(TEXT("On Party Left is bound while constructed"), Subsystem->OnPartyLeft.Contains(Widget.Get(), FName(TEXT("OnPartyLeft"))));
	TestTrue(TEXT("On Matchmaking Updated is bound while constructed"), Subsystem->OnMatchmakingUpdated.Contains(Widget.Get(), FName(TEXT("OnMatchmakingUpdated"))));

	SlateWidget.Reset();
	TestFalse(TEXT("On Busy Changed is unbound after destruct"), Subsystem->OnBusyChanged.Contains(Widget.Get(), FName(TEXT("OnBusyChanged"))));
	TestFalse(TEXT("On Party Left is unbound after destruct"), Subsystem->OnPartyLeft.Contains(Widget.Get(), FName(TEXT("OnPartyLeft"))));

	Widget.Reset();
	EasySessionTest::DestroyGameInstance(GameInstance.Get());
	return true;
}

#endif
