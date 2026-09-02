// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionTypes.h"

namespace EasySessionFriendOrderTest
{
	static FEasySessionFriend MakeFriend(const TCHAR* Name, bool bOnline, bool bPlaying)
	{
		FEasySessionFriend Friend;
		Friend.DisplayName = Name;
		Friend.bIsOnline = bOnline;
		Friend.bIsPlayingThisGame = bPlaying;
		return Friend;
	}

	static FEasyFriendSession MakeEntry(const TCHAR* Name, bool bOnline, bool bPlaying, bool bHasSession)
	{
		FEasyFriendSession Entry;
		Entry.Friend = MakeFriend(Name, bOnline, bPlaying);
		Entry.bHasSession = bHasSession;
		return Entry;
	}

	static FString Names(const TArray<FEasyFriendSession>& Entries)
	{
		TArray<FString> Out;
		for (const FEasyFriendSession& Entry : Entries)
		{
			Out.Add(Entry.Friend.DisplayName);
		}
		return FString::Join(Out, TEXT(","));
	}
}

/**
 * The friend list a UI shows should need no sorting of its own: friends in a joinable session first,
 * then by presence, then by name. The order is a pure function of the entries, so it is checked without an online service.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionFriendOrderTest, "EasySession.Friends.ListOrdersSessionsThenPresence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionFriendOrderTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionFriendOrderTest;

	TArray<FEasyFriendSession> Entries;
	Entries.Add(MakeEntry(TEXT("zed"), false, false, false));       // offline
	Entries.Add(MakeEntry(TEXT("Mia"), true, false, false));        // online, other game
	Entries.Add(MakeEntry(TEXT("bob"), true, true, true));          // in a session
	Entries.Add(MakeEntry(TEXT("Amy"), true, true, false));         // playing, no findable session
	Entries.Add(MakeEntry(TEXT("Kai"), false, false, false));       // offline
	Entries.Add(MakeEntry(TEXT("Ada"), true, true, true));          // in a session

	EasySession::SortFriendSessions(Entries);
	TestEqual(TEXT("Sessions first, then playing, online, offline; names sorted case-insensitively inside each group"),
		Names(Entries), FString(TEXT("Ada,bob,Amy,Mia,Kai,zed")));

	// Sorting again changes nothing - a refresh must not shuffle rows the player is looking at.
	const FString Before = Names(Entries);
	EasySession::SortFriendSessions(Entries);
	TestEqual(TEXT("The order is stable under a second sort"), Names(Entries), Before);

	TArray<FEasySessionFriend> Friends;
	Friends.Add(MakeFriend(TEXT("Cy"), false, false));
	Friends.Add(MakeFriend(TEXT("Bo"), true, false));
	Friends.Add(MakeFriend(TEXT("Al"), true, true));
	EasySession::SortFriends(Friends);
	TestEqual(TEXT("Read Easy Friends orders playing, online, offline"),
		Friends[0].DisplayName + Friends[1].DisplayName + Friends[2].DisplayName, FString(TEXT("AlBoCy")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
