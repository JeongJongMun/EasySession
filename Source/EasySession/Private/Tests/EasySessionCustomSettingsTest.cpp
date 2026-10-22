// Copyright (c) 2026 Langerak.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionStatics.h"
#include "EasySessionTypes.h"
#include "OnlineSessionSettings.h"

/**
 * A custom setting keeps its type through the online subsystem: written by ApplyTo, read by ReadFrom and by FEasySessionSearchResult::FromNative.
 * A number that came back as text would stop matching a search for the number, and the GetEasySetting functions would fail on it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionCustomSettingTypeTest, "EasySession.CustomSettings.TypeSurvivesTheOnlineSubsystem", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionCustomSettingTypeTest::RunTest(const FString& Parameters)
{
	FEasySessionSettings Settings;
	Settings.MaxPlayers = 4;
	Settings.CustomSettings.Add(TEXT("Mode"), FEasySessionSettingValue::MakeString(TEXT("Deathmatch")));
	Settings.CustomSettings.Add(TEXT("Rounds"), FEasySessionSettingValue::MakeInt(5));

	FOnlineSessionSettings Online;
	Settings.ApplyTo(Online);

	const FOnlineSessionSetting* Mode = Online.Settings.Find(TEXT("Mode"));
	const FOnlineSessionSetting* Rounds = Online.Settings.Find(TEXT("Rounds"));
	if (!TestNotNull(TEXT("The text setting is advertised"), Mode) || !TestNotNull(TEXT("The number setting is advertised"), Rounds))
	{
		return false;
	}
	TestEqual(TEXT("Text is advertised as a string"), Mode->Data.GetType(), EOnlineKeyValuePairDataType::String);
	TestEqual(TEXT("A number is advertised as an Int32, which Steam can search on"), Rounds->Data.GetType(), EOnlineKeyValuePairDataType::Int32);

	FEasySessionSettings ReadBack;
	ReadBack.ReadFrom(Online);
	TestTrue(TEXT("ReadFrom keeps the text"), ReadBack.CustomSettings.FindRef(TEXT("Mode")) == FEasySessionSettingValue::MakeString(TEXT("Deathmatch")));
	TestTrue(TEXT("ReadFrom keeps the number as a number"), ReadBack.CustomSettings.FindRef(TEXT("Rounds")) == FEasySessionSettingValue::MakeInt(5));

	FOnlineSessionSearchResult Native;
	Native.Session.SessionSettings = Online;
	const FEasySessionSearchResult Result = FEasySessionSearchResult::FromNative(Native);
	TestTrue(TEXT("A search result keeps the text"), Result.CustomSettings.FindRef(TEXT("Mode")) == FEasySessionSettingValue::MakeString(TEXT("Deathmatch")));
	TestTrue(TEXT("A search result keeps the number as a number"), Result.CustomSettings.FindRef(TEXT("Rounds")) == FEasySessionSettingValue::MakeInt(5));
	return true;
}

/**
 * RequiredCustomSettings matches a pair only when the type, the value and the case of the text are equal.
 * The online subsystem treats the number 3 and the text "3" as different values, so ShouldInclude must not match them either.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionCustomSettingFilterTest, "EasySession.CustomSettings.FilterComparesTypeAndCase", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionCustomSettingFilterTest::RunTest(const FString& Parameters)
{
	FEasySessionSearchResult Result;
	Result.CustomSettings.Add(TEXT("Mode"), FEasySessionSettingValue::MakeString(TEXT("Deathmatch")));
	Result.CustomSettings.Add(TEXT("Rounds"), FEasySessionSettingValue::MakeInt(3));

	const auto Includes = [&Result](const TCHAR* Key, const FEasySessionSettingValue& Value)
	{
		FEasySessionSearchParams Params;
		Params.RequiredCustomSettings.Add(Key, Value);
		return Params.ShouldInclude(Result);
	};

	TestTrue(TEXT("The same number matches"), Includes(TEXT("Rounds"), FEasySessionSettingValue::MakeInt(3)));
	TestFalse(TEXT("Another number does not match"), Includes(TEXT("Rounds"), FEasySessionSettingValue::MakeInt(4)));
	TestFalse(TEXT("The number's text does not match the number"), Includes(TEXT("Rounds"), FEasySessionSettingValue::MakeString(TEXT("3"))));
	TestTrue(TEXT("The same text matches"), Includes(TEXT("Mode"), FEasySessionSettingValue::MakeString(TEXT("Deathmatch"))));
	TestFalse(TEXT("Text in another case does not match"), Includes(TEXT("Mode"), FEasySessionSettingValue::MakeString(TEXT("deathmatch"))));
	TestFalse(TEXT("A key the session does not advertise does not match"), Includes(TEXT("Map"), FEasySessionSettingValue::MakeString(TEXT("Dust"))));
	return true;
}

/**
 * The MakeEasySetting and GetEasySetting functions round-trip each type, and a GetEasySetting function of the other type fails instead of converting.
 * MakeEasySettingBool stores a whole number, so GetEasySettingInt reads it too.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionCustomSettingNodesTest, "EasySession.CustomSettings.NodesKeepTheType", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionCustomSettingNodesTest::RunTest(const FString& Parameters)
{
	const FEasySessionSettingValue Text = UEasySessionStatics::MakeEasySettingString(TEXT("Deathmatch"));
	const FEasySessionSettingValue Number = UEasySessionStatics::MakeEasySettingInt(7);
	const FEasySessionSettingValue Flag = UEasySessionStatics::MakeEasySettingBool(true);

	FString TextValue;
	int32 IntValue = -1;
	bool BoolValue = false;

	TestTrue(TEXT("Get String reads text"), UEasySessionStatics::GetEasySettingString(Text, TextValue));
	TestEqual(TEXT("Get String returns the text"), TextValue, FString(TEXT("Deathmatch")));
	TestFalse(TEXT("Get Int fails on text"), UEasySessionStatics::GetEasySettingInt(Text, IntValue));
	TestEqual(TEXT("A failed Get Int returns 0"), IntValue, 0);

	TestTrue(TEXT("Get Int reads a number"), UEasySessionStatics::GetEasySettingInt(Number, IntValue));
	TestEqual(TEXT("Get Int returns the number"), IntValue, 7);
	TestFalse(TEXT("Get String fails on a number"), UEasySessionStatics::GetEasySettingString(Number, TextValue));
	TestTrue(TEXT("A failed Get String returns empty text"), TextValue.IsEmpty());

	TestTrue(TEXT("Get Bool reads a bool"), UEasySessionStatics::GetEasySettingBool(Flag, BoolValue));
	TestTrue(TEXT("Get Bool returns true"), BoolValue);
	TestTrue(TEXT("Get Int reads a bool as a number"), UEasySessionStatics::GetEasySettingInt(Flag, IntValue));
	TestEqual(TEXT("True is stored as 1"), IntValue, 1);
	TestFalse(TEXT("Get Bool fails on text"), UEasySessionStatics::GetEasySettingBool(Text, BoolValue));

	TestEqual(TEXT("To String shows text as it is"), UEasySessionStatics::Conv_EasySettingToString(Text), FString(TEXT("Deathmatch")));
	TestEqual(TEXT("To String shows a number"), UEasySessionStatics::Conv_EasySettingToString(Number), FString(TEXT("7")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
