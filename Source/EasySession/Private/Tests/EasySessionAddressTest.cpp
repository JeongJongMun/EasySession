// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionAddress.h"

/**
 * Port 0 detection. The cases that matter are the ones where the string cannot
 * be read with certainty: those must report "no problem", because a wrong
 * complaint rejects a join that would have worked.
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
 * The ?listen option must be matched as a URL option. Substring matching used to
 * get both of these wrong: it missed "?Listen" and it mistook "?listenport" for
 * the option itself.
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
 * Reading an option out of the URL a joining player arrives with. This decides
 * whether a password matches, so every shape the engine can hand us has to read
 * the same way it does: the map path in front, several options, no options.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionAddressParseOptionTest, "EasySession.Address.ParseTravelOption", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionAddressParseOptionTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* RequestURL;
		const TCHAR* Expected;
		const TCHAR* Why;
	};

	static const FCase Cases[] =
	{
		{ TEXT("?Pw=secret"),                          TEXT("secret"), TEXT("options only") },
		{ TEXT("/Game/Maps/Lobby?Pw=secret"),          TEXT("secret"), TEXT("map path in front, as PreLogin sees it") },
		{ TEXT("/Game/Maps/Lobby?Team=1?Pw=secret"),   TEXT("secret"), TEXT("not the first option") },
		{ TEXT("?Pw=secret?Team=1"),                   TEXT("secret"), TEXT("not the last option") },
		{ TEXT("/Game/Maps/Lobby"),                    TEXT(""),       TEXT("no options at all") },
		{ TEXT("?Team=1"),                             TEXT(""),       TEXT("option absent") },
		{ TEXT("?Pw="),                                TEXT(""),       TEXT("option present but empty") },
		{ TEXT("?Pw"),                                 TEXT(""),       TEXT("option present with no value") },
		{ TEXT("?Pw=  secret  "),                      TEXT("secret"), TEXT("trimmed, matching what the sender trims") },
		{ TEXT("?Pw=a b"),                             TEXT("a b"),    TEXT("a space inside survives - the engine allows it on purpose") },
		{ TEXT("?Pw=a=b"),                             TEXT("a=b"),    TEXT("only the first = splits key from value") },
		{ TEXT("?Pwx=other?Pw=secret"),                TEXT("secret"), TEXT("a longer key that starts the same is a different option") },
	};

	for (const FCase& Case : Cases)
	{
		const FString Actual = EasySessionAddress::ParseTravelOption(Case.RequestURL, TEXT("Pw"));
		TestEqual(FString::Printf(TEXT("'%s' (%s)"), Case.RequestURL, Case.Why), Actual, FString(Case.Expected));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
