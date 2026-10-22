// Copyright (c) 2026 Langerak.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSocial.h"
#include "EasySessionTypes.h"

namespace EasySessionFriendOrderTest
{
	static FEasySessionFriend MakeFriend(const TCHAR* Name, bool bOnline, bool bPlaying, bool bInLobby)
	{
		FEasySessionFriend Friend;
		Friend.DisplayName = Name;
		Friend.bIsOnline = bOnline;
		Friend.bIsPlayingThisGame = bPlaying;
		Friend.bIsInLobby = bInLobby;
		return Friend;
	}

	static FString Names(const TArray<FEasySessionFriend>& Friends)
	{
		TArray<FString> Out;
		for (const FEasySessionFriend& Friend : Friends)
		{
			Out.Add(Friend.DisplayName);
		}
		return FString::Join(Out, TEXT(","));
	}
}

/**
 * The friend list a UI shows should need no sorting of its own: friends in a lobby first, then by presence, then by name.
 * The order is a pure function of the entries, so it is checked without an online subsystem.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionFriendOrderTest, "EasySession.Friends.ListOrdersLobbiesThenPresence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionFriendOrderTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionFriendOrderTest;

	TArray<FEasySessionFriend> Friends;
	Friends.Add(MakeFriend(TEXT("zed"), false, false, false));      // offline
	Friends.Add(MakeFriend(TEXT("Mia"), true, false, false));       // online, other game
	Friends.Add(MakeFriend(TEXT("bob"), true, true, true));         // in a lobby
	Friends.Add(MakeFriend(TEXT("Amy"), true, true, false));        // playing, in no lobby
	Friends.Add(MakeFriend(TEXT("Kai"), false, false, false));      // offline
	Friends.Add(MakeFriend(TEXT("Ada"), true, true, true));         // in a lobby
	Friends.Add(MakeFriend(TEXT("abe"), true, true, true));         // in a lobby, before Ada only when case is ignored
	Friends.Add(MakeFriend(TEXT("sam"), false, false, false));      // offline, the same name as the next one when case is ignored
	Friends.Add(MakeFriend(TEXT("Sam"), false, false, false));      // offline

	FEasySessionSocial::SortFriends(Friends);
	TestEqual(TEXT("Lobbies first, then playing, online, offline; names sorted case-insensitively inside each rank, equal names in arrival order"),
		Names(Friends), FString(TEXT("abe,Ada,bob,Amy,Mia,Kai,sam,Sam,zed")));

	// Sorting again changes nothing, the two Sams included. A refresh must not shuffle rows the player is looking at.
	const FString Before = Names(Friends);
	FEasySessionSocial::SortFriends(Friends);
	TestEqual(TEXT("The order is stable under a second sort"), Names(Friends), Before);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
