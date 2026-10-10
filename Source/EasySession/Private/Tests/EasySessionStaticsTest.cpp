// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionStatics.h"
#include "EasySessionTypes.h"

/**
 * Make Easy Session Host Params From Settings copies every settings field and adds only the map and the LAN choice.
 * A field it drops would reset silently on the created session, so each one is compared.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionHostParamsFromSettingsTest, "EasySession.Statics.HostParamsFromSettingsKeepEveryField", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionHostParamsFromSettingsTest::RunTest(const FString& Parameters)
{
	FEasySessionSettings Settings;
	Settings.SessionDisplayName = TEXT("Night Match");
	Settings.MaxPlayers = 6;
	Settings.bShouldAdvertise = false;
	Settings.bHidden = true;
	Settings.Password = TEXT("open sesame");
	Settings.bFriendsBypassPassword = true;
	Settings.bAllowJoinInProgress = false;
	Settings.bAllowInvites = false;
	Settings.Region = EEasySessionRegion::EastAsia;
	Settings.bUseJoinCode = true;
	Settings.CustomSettings.Add(TEXT("Mode"), TEXT("Deathmatch"));

	const FEasySessionHostParams HostParams = UEasySessionStatics::MakeEasySessionHostParamsFromSettings(Settings, TEXT("/Game/Maps/Lobby"), true);

	TestEqual(TEXT("Session display name"), HostParams.SessionDisplayName, Settings.SessionDisplayName);
	TestEqual(TEXT("Max players"), HostParams.MaxPlayers, Settings.MaxPlayers);
	TestEqual(TEXT("Should advertise"), HostParams.bShouldAdvertise, Settings.bShouldAdvertise);
	TestEqual(TEXT("Hidden"), HostParams.bHidden, Settings.bHidden);
	TestEqual(TEXT("Password"), HostParams.Password, Settings.Password);
	TestEqual(TEXT("Friends bypass password"), HostParams.bFriendsBypassPassword, Settings.bFriendsBypassPassword);
	TestEqual(TEXT("Allow join in progress"), HostParams.bAllowJoinInProgress, Settings.bAllowJoinInProgress);
	TestEqual(TEXT("Allow invites"), HostParams.bAllowInvites, Settings.bAllowInvites);
	TestEqual(TEXT("Region"), HostParams.Region, Settings.Region);
	TestEqual(TEXT("Use join code"), HostParams.bUseJoinCode, Settings.bUseJoinCode);
	TestEqual(TEXT("Custom setting"), HostParams.CustomSettings.FindRef(TEXT("Mode")), FString(TEXT("Deathmatch")));
	TestEqual(TEXT("Initial map"), HostParams.InitialMapName, FString(TEXT("/Game/Maps/Lobby")));
	TestTrue(TEXT("LAN match"), HostParams.bIsLANMatch);
	TestTrue(TEXT("Presence keeps its default"), HostParams.bUsePresence);
	return true;
}

#endif
