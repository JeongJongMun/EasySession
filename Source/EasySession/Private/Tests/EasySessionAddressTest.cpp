// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionAddress.h"
#include "Engine/EngineBaseTypes.h"

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
		{ TEXT("?Pw=a%3Fb"),                           TEXT("a%3Fb"), TEXT("encoded values come back encoded - decoding is a separate step") },

		// Why senders encode: a raw ? ends the value early and the rest becomes
		// its own option.
		{ TEXT("?Pw=a?b"),                             TEXT("a"),      TEXT("a raw ? cuts the value in half") },
	};

	for (const FCase& Case : Cases)
	{
		const FString Actual = EasySessionAddress::ParseTravelOption(Case.RequestURL, TEXT("Pw"));
		TestEqual(FString::Printf(TEXT("'%s' (%s)"), Case.RequestURL, Case.Why), Actual, FString(Case.Expected));
	}

	return true;
}

/**
 * A session password is whatever the host typed, and it travels as a URL option.
 * Encoding has to give the exact string back, or a host locks a room with a
 * password nobody can enter - including the player who was told it correctly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionAddressEncodeOptionTest, "EasySession.Address.EncodeTravelOptionValue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionAddressEncodeOptionTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Value;
		const TCHAR* Encoded;
		const TCHAR* Why;
	};

	static const FCase Cases[] =
	{
		{ TEXT("1234"),      TEXT("1234"),          TEXT("the ordinary case is left alone") },
		{ TEXT(""),          TEXT(""),              TEXT("empty stays empty") },
		{ TEXT("a b"),       TEXT("a b"),           TEXT("spaces need no escaping - the engine allows them") },
		{ TEXT("a=b"),       TEXT("a=b"),           TEXT("only the first = splits an option, so = needs none either") },
		{ TEXT("a?b"),       TEXT("a%3Fb"),         TEXT("? would end the option early") },
		{ TEXT("a#b"),       TEXT("a%23b"),         TEXT("# would send the rest to the portal") },
		{ TEXT("100%"),      TEXT("100%25"),        TEXT("% is the escape character and has to escape itself") },
		{ TEXT("%3F"),       TEXT("%253F"),         TEXT("a value that already looks encoded must not decode to ?") },
		{ TEXT("?#%"),       TEXT("%3F%23%25"),     TEXT("all three at once") },
	};

	for (const FCase& Case : Cases)
	{
		const FString Encoded = EasySessionAddress::EncodeTravelOptionValue(Case.Value);
		TestEqual(FString::Printf(TEXT("encode '%s' (%s)"), Case.Value, Case.Why), Encoded, FString(Case.Encoded));

		// The property that actually matters: whatever was typed comes back.
		const FString RoundTripped = EasySessionAddress::DecodeTravelOptionValue(Encoded);
		TestEqual(FString::Printf(TEXT("round trip '%s'"), Case.Value), RoundTripped, FString(Case.Value));
	}

	return true;
}

/**
 * The same values through the engine's own URL parser, which is what splits an
 * unencoded password in half. Testing our two functions against each other would
 * agree with itself no matter what the engine does with the string in between.
 *
 * What this still cannot show: the trip over the wire from the joining client to
 * the host's PendingConnection. That needs two running games.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionAddressTravelURLTest, "EasySession.Address.EncodedOptionSurvivesTheEngineURL", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionAddressTravelURLTest::RunTest(const FString& Parameters)
{
	static const TCHAR* Passwords[] =
	{
		TEXT("1234"),
		TEXT("a b"),
		TEXT("a=b"),
		TEXT("a?b"),
		TEXT("a#b"),
		TEXT("100%"),
		TEXT("%3F"),
		TEXT("?#%"),
	};

	for (const TCHAR* Password : Passwords)
	{
		// Built the way the joining client builds it.
		const FString TravelURL = FString::Printf(TEXT("127.0.0.1:7777?Pw=%s"),
			*EasySessionAddress::EncodeTravelOptionValue(Password));

		// Parsed the way the engine parses it before the host sees a request URL.
		const FURL URL(nullptr, *TravelURL, TRAVEL_Absolute);
		const FString Recovered = EasySessionAddress::DecodeTravelOptionValue(URL.GetOption(TEXT("Pw="), TEXT("")));

		TestEqual(FString::Printf(TEXT("'%s' survives FURL"), Password), Recovered, FString(Password));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
