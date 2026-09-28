// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionAddress.h"
#include "Engine/EngineBaseTypes.h"

/**
 * Port 0 detection.
 * The cases that matter are the ones where the string cannot be read with certainty.
 * Those must report "no problem", because a wrong complaint refuses a join that would have worked.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionAddressZeroPortTest, "EasySession.Address.HasZeroPort", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionAddressZeroPortTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Address;
		bool bExpected;
		const TCHAR* Why;
	};

	static const FCase Cases[] =
	{
		{ TEXT("127.0.0.1:0"),                    true,  TEXT("NULL subsystem, host never started listening") },
		{ TEXT("127.0.0.1:7777"),                 false, TEXT("NULL subsystem, listening") },
		{ TEXT("steam.76561198000000000:0"),      true,  TEXT("Steam P2P, host never started listening") },
		{ TEXT("steam.76561198000000000:7777"),   false, TEXT("Steam P2P, listening") },
		{ TEXT("[fe80::1]:0"),                    true,  TEXT("bracketed IPv6, port 0") },
		{ TEXT("[fe80::1]:7777"),                 false, TEXT("bracketed IPv6, listening") },
		{ TEXT("fe80::0"),                        false, TEXT("bare IPv6: the trailing group is address, not port") },
		{ TEXT("127.0.0.1:00"),                   true,  TEXT("padded zero is still zero") },
		{ TEXT("127.0.0.1:"),                     false, TEXT("no port digits to judge") },
		{ TEXT("127.0.0.1"),                      false, TEXT("no port at all") },
		{ TEXT("EOS:0002abcd"),                   false, TEXT("not an address: trailing token is not numeric") },
		{ TEXT(""),                               false, TEXT("empty is handled by the resolve check, not here") },
	};

	for (const FCase& Case : Cases)
	{
		const bool bActual = EasySessionAddress::HasZeroPort(Case.Address);
		TestEqual(FString::Printf(TEXT("'%s' (%s)"), Case.Address, Case.Why), bActual, Case.bExpected);
	}

	return true;
}

/**
 * The ?listen option must be matched as a URL option.
 * Substring matching used to get both of these wrong: it missed "?Listen" and it mistook "?listenport" for the option itself.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionAddressListenOptionTest, "EasySession.Address.HasListenOption", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionAddressListenOptionTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* URL;
		bool bExpected;
		const TCHAR* Why;
	};

	static const FCase Cases[] =
	{
		{ TEXT("/Game/Maps/Lobby?listen"),              true,  TEXT("plain option") },
		{ TEXT("/Game/Maps/Lobby?Listen"),              true,  TEXT("options are case insensitive") },
		{ TEXT("/Game/Maps/Lobby?listen?Password=hunter"), true, TEXT("option among others") },
		{ TEXT("/Game/Maps/Lobby"),                     false, TEXT("no options") },
		{ TEXT("/Game/Maps/Lobby?listenport=7777"),     false, TEXT("different option that starts with the same letters") },
		{ TEXT("/Game/Maps/Listen"),                    false, TEXT("map name is not an option") },
	};

	for (const FCase& Case : Cases)
	{
		const bool bActual = EasySessionAddress::HasListenOption(Case.URL);
		TestEqual(FString::Printf(TEXT("'%s' (%s)"), Case.URL, Case.Why), bActual, Case.bExpected);
	}

	return true;
}

/**
 * The capacity the engine enforces comes from this option, so it has to be on every host URL.
 * A game that set it itself in its map name or extra travel options keeps its own value.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionAddressMaxPlayersOptionTest, "EasySession.Address.AppendMaxPlayersOption", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionAddressMaxPlayersOptionTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* TravelURL;
		const TCHAR* Expected;
		const TCHAR* Why;
	};

	static const FCase Cases[] =
	{
		{ TEXT("/Game/Maps/Lobby?listen"),              TEXT("/Game/Maps/Lobby?listen?MaxPlayers=4"),        TEXT("appended after the options already there") },
		{ TEXT("/Game/Maps/Lobby"),                     TEXT("/Game/Maps/Lobby?MaxPlayers=4"),               TEXT("first option on a bare map path") },
		{ TEXT("/Game/Maps/Lobby?listen?MaxPlayers=8"), TEXT("/Game/Maps/Lobby?listen?MaxPlayers=8"),        TEXT("the game's own value wins") },
		{ TEXT("/Game/Maps/Lobby?maxplayers=8"),        TEXT("/Game/Maps/Lobby?maxplayers=8"),               TEXT("matched case insensitively, as the engine reads it") },
		{ TEXT("/Game/Maps/Lobby?MaxPlayersX=8"),       TEXT("/Game/Maps/Lobby?MaxPlayersX=8?MaxPlayers=4"), TEXT("a longer key that starts the same is a different option") },
	};

	for (const FCase& Case : Cases)
	{
		FString Actual = Case.TravelURL;
		EasySessionAddress::AppendMaxPlayersOption(Actual, 4);
		TestEqual(FString::Printf(TEXT("'%s' (%s)"), Case.TravelURL, Case.Why), Actual, FString(Case.Expected));
	}

	// What was written has to read back through the engine's own URL parser, which is what fills AGameSession::MaxPlayers.
	FString RoundTrip = TEXT("/Game/Maps/Lobby?listen");
	EasySessionAddress::AppendMaxPlayersOption(RoundTrip, 4);
	const FURL URL(nullptr, *RoundTrip, TRAVEL_Absolute);
	TestEqual(TEXT("the option reads back"), FString(URL.GetOption(TEXT("MaxPlayers="), TEXT(""))), FString(TEXT("4")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
