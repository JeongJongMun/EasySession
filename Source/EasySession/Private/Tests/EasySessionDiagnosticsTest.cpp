// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionDiagnostics.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "Misc/ConfigCacheIni.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionDiagnosticsTest
{
	/** The configured-versus-active finding, or nullptr when the run did not report one. */
	const EasySessionDiagnostics::FFinding* FindServiceMismatch(const EasySessionDiagnostics::FReport& Report)
	{
		return Report.Findings.FindByPredicate([](const EasySessionDiagnostics::FFinding& Finding)
		{
			return Finding.Message.Contains(TEXT("DefaultPlatformService"));
		});
	}
}

/**
 * Diagnostics smoke test: the checks must run to completion on any subsystem
 * (NULL or Steam) without crashing, including a null world, and so must logging
 * whatever they returned.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionDiagnosticsSmokeTest, "EasySession.Diagnostics.RunsToCompletion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionDiagnosticsSmokeTest::RunTest(const FString& Parameters)
{
	// Null world: must not crash, and must still name the active service.
	const EasySessionDiagnostics::FReport NullWorldReport = EasySessionDiagnostics::RunDiagnostics(nullptr);
	TestTrue(TEXT("The summary names the services even without a world"), NullWorldReport.Summary.Contains(TEXT("active:")));

	// Real (standalone) world: must run all checks to completion.
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GEngine));
	GameInstance->InitializeStandalone();
	const EasySessionDiagnostics::FReport Report = EasySessionDiagnostics::RunDiagnostics(GameInstance->GetWorld());
	TestTrue(TEXT("The summary names the services"), Report.Summary.Contains(TEXT("configured:")));
	EasySessionDiagnostics::LogReport(Report);
	EasySessionTest::DestroyGameInstance(GameInstance.Get());

	return true;
}

/**
 * The findings follow the config. The service mismatch appears exactly when the
 * configured service is not the one that loaded, and its causes name bEnabled=false -
 * the place that knowledge moved to when the always-false bEnabled check in
 * DiagnoseSteam was removed. The checks inside DiagnoseSteam itself stay untested
 * here: they only run when Steam is the active subsystem.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionDiagnosticsFindingsTest, "EasySession.Diagnostics.FindingsFollowTheConfig", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionDiagnosticsFindingsTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionDiagnosticsTest;

	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GEngine));
	GameInstance->InitializeStandalone();
	UWorld* World = GameInstance->GetWorld();

	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(World);
	if (!TestNotNull(TEXT("An online subsystem is active"), OnlineSub))
	{
		EasySessionTest::DestroyGameInstance(GameInstance.Get());
		return false;
	}

	// The project's own value comes back at the end, whatever the assertions did.
	FString SavedService;
	const bool bHadService = GConfig->GetString(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), SavedService, GEngineIni);

	// Ask for the service that actually loaded: nothing to mismatch.
	GConfig->SetString(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), *OnlineSub->GetSubsystemName().ToString(), GEngineIni);
	const EasySessionDiagnostics::FReport MatchedReport = EasySessionDiagnostics::RunDiagnostics(World);
	TestTrue(TEXT("No mismatch is reported when the configured service loaded"), FindServiceMismatch(MatchedReport) == nullptr);

	// Ask for Steam while another service loaded: the mismatch appears with its causes.
	GConfig->SetString(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), TEXT("Steam"), GEngineIni);
	const EasySessionDiagnostics::FReport MismatchReport = EasySessionDiagnostics::RunDiagnostics(World);
	const EasySessionDiagnostics::FFinding* Mismatch = FindServiceMismatch(MismatchReport);
	if (TestNotNull(TEXT("The mismatch is reported when the configured service did not load"), Mismatch))
	{
		TestTrue(TEXT("The mismatch is a fix, not a note"), Mismatch->Kind == EasySessionDiagnostics::EFindingKind::Fix);
		TestTrue(TEXT("It names the configured service"), Mismatch->Message.Contains(TEXT("Steam")));
		TestTrue(TEXT("Its causes name bEnabled=false"), Mismatch->Postscript.Contains(TEXT("bEnabled=false")));
	}
	TestTrue(TEXT("The summary names the configured service"), MismatchReport.Summary.Contains(TEXT("configured: Steam")));

	// The engine ships a BeaconNetDriver definition, so a project that kept it draws no finding.
	TestFalse(TEXT("No BeaconNetDriver finding on a stock driver list"), MismatchReport.Findings.ContainsByPredicate(
		[](const EasySessionDiagnostics::FFinding& Finding)
		{
			return Finding.Message.Contains(TEXT("BeaconNetDriver"));
		}));

	if (bHadService)
	{
		GConfig->SetString(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), *SavedService, GEngineIni);
	}
	else
	{
		GConfig->RemoveKey(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), GEngineIni);
	}

	EasySessionTest::DestroyGameInstance(GameInstance.Get());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
