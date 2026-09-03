// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionTypes.h"
#include "EasySessionUIStatics.h"

/**
 * The UI helpers are pure text functions, so they are checked without a session.
 * Every result must have a sentence of its own, the region helpers must agree with the enum, and the formats must read as the row shows them.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionUIStaticsTest, "EasySession.UI.HelpersCoverEveryResultAndRegion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionUIStaticsTest::RunTest(const FString& Parameters)
{
	// Every result gets a non-empty sentence, and no two results share one apart from the unknown fallback.
	const UEnum* ResultEnum = StaticEnum<EEasySessionResult>();
	TSet<FString> Seen;
	for (int32 Index = 0; Index < ResultEnum->NumEnums() - 1; ++Index)
	{
		const EEasySessionResult Result = static_cast<EEasySessionResult>(ResultEnum->GetValueByIndex(Index));
		const FString Message = UEasySessionUIStatics::GetResultMessage(Result).ToString();
		TestFalse(FString::Printf(TEXT("%s has a message"), *ResultEnum->GetNameStringByIndex(Index)), Message.IsEmpty());
		TestFalse(FString::Printf(TEXT("%s has its own message"), *ResultEnum->GetNameStringByIndex(Index)), Seen.Contains(Message));
		Seen.Add(Message);
	}
	TestEqual(TEXT("Success reads Done"), UEasySessionUIStatics::GetResultMessage(EEasySessionResult::Success).ToString(), FString(TEXT("Done")));

	// Every activity but None has a sentence of its own. None is the empty text a status line clears with.
	const UEnum* ActivityEnum = StaticEnum<EEasySessionActivity>();
	TSet<FString> SeenActivity;
	for (int32 Index = 0; Index < ActivityEnum->NumEnums() - 1; ++Index)
	{
		const EEasySessionActivity Activity = static_cast<EEasySessionActivity>(ActivityEnum->GetValueByIndex(Index));
		const FString Message = UEasySessionUIStatics::GetActivityMessage(Activity).ToString();
		if (Activity == EEasySessionActivity::None)
		{
			TestTrue(TEXT("None gives empty text"), Message.IsEmpty());
			continue;
		}
		TestFalse(FString::Printf(TEXT("%s has a message"), *ActivityEnum->GetNameStringByIndex(Index)), Message.IsEmpty());
		TestFalse(FString::Printf(TEXT("%s has its own message"), *ActivityEnum->GetNameStringByIndex(Index)), SeenActivity.Contains(Message));
		SeenActivity.Add(Message);
	}
	TestEqual(TEXT("Creating reads as a status line"), UEasySessionUIStatics::GetActivityMessage(EEasySessionActivity::Creating).ToString(), FString(TEXT("Creating the session...")));

	// Matchmaking status carries the elapsed seconds while a run is active, and nothing while idle.
	TestEqual(TEXT("Searching status"), UEasySessionUIStatics::FormatMatchmakingStatus(EEasyMatchmakingState::Searching, 12).ToString(), FString(TEXT("Searching... 12s")));
	TestEqual(TEXT("Idle status"), UEasySessionUIStatics::FormatMatchmakingStatus(EEasyMatchmakingState::Idle, 99).ToString(), FString(TEXT("Ready")));
	TestEqual(TEXT("Negative seconds clamp to zero"), UEasySessionUIStatics::FormatMatchmakingStatus(EEasyMatchmakingState::Hosting, -5).ToString(), FString(TEXT("Hosting a session... 0s")));

	// Slots read players/max with the ping, and never go negative when the open count is stale.
	FEasySessionSearchResult Result;
	Result.MaxPlayers = 4;
	Result.OpenSlots = 3;
	Result.PingInMs = 32;
	TestEqual(TEXT("Slots line"), UEasySessionUIStatics::FormatSessionSlots(Result).ToString(), FString(TEXT("1/4   ping 32ms")));
	Result.OpenSlots = 9;
	TestEqual(TEXT("Stale open count clamps to zero players"), UEasySessionUIStatics::FormatSessionSlots(Result).ToString(), FString(TEXT("0/4   ping 32ms")));

	// Region options mirror the enum, and the index round-trips.
	const UEnum* RegionEnum = StaticEnum<EEasySessionRegion>();
	const TArray<FText> Options = UEasySessionUIStatics::GetRegionOptions();
	TestEqual(TEXT("One option per region"), Options.Num(), RegionEnum->NumEnums() - 1);
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		const EEasySessionRegion Region = UEasySessionUIStatics::RegionFromIndex(Index);
		TestEqual(FString::Printf(TEXT("Option %d names its region"), Index), Options[Index].ToString(), UEasySessionUIStatics::GetRegionDisplayName(Region).ToString());
	}
	TestEqual(TEXT("North America East display name"), UEasySessionUIStatics::GetRegionDisplayName(EEasySessionRegion::NorthAmericaEast).ToString(), FString(TEXT("North America East")));
	TestEqual(TEXT("Out of range index falls back to Any"), UEasySessionUIStatics::RegionFromIndex(Options.Num() + 3), EEasySessionRegion::Any);
	TestEqual(TEXT("Negative index falls back to Any"), UEasySessionUIStatics::RegionFromIndex(-1), EEasySessionRegion::Any);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
