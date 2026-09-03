// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionOperation.h"
#include "EasySessionRequestQueue.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionOperationTest
{
	/** An operation with no steps: it only reports what the queue asks and ends itself when canceled, as the real ones do. */
	class FFakeOperation final : public IEasySessionOperation
	{
	public:

		FFakeOperation(FEasySessionRequestQueue& InQueue, EEasySessionOperationType InType, bool bInCountsAsBusy)
			: Queue(InQueue)
			, Type(InType)
			, bCountsAsBusy(bInCountsAsBusy)
		{
		}

		EEasySessionOperationType GetType() const override { return Type; }
		bool CountsAsBusy() const override { return bCountsAsBusy; }

		void Cancel() override
		{
			++CancelCount;
			Queue.EndOperation(*this);
		}

		FString DescribeProgress() const override
		{
			return FString::Printf(TEXT("Fake %s"), EasySession::OperationTypeToString(Type));
		}

		int32 CancelCount = 0;

	private:

		FEasySessionRequestQueue& Queue;
		EEasySessionOperationType Type;
		bool bCountsAsBusy;
	};
}

/**
 * The queue owns multi-step operations: one of each type, busy only for the types that count, cancel in one call, and a status line that lists them.
 * The subsystem reads the same list for Is Busy and Get Activity.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionOperationTest, "EasySession.Queue.OperationsDriveBusyAndCancel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionOperationTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionOperationTest;

	FEasySessionRequestQueue Queue([]() {}, []() {});
	TestTrue(TEXT("An empty queue is idle"), Queue.IsIdle());
	TestFalse(TEXT("An empty queue is not busy"), Queue.IsBusy());
	TestEqual(TEXT("An empty queue says Idle"), Queue.DescribeStatus(false), FString(TEXT("Idle")));

	TSharedRef<FFakeOperation> Search = MakeShared<FFakeOperation>(Queue, EEasySessionOperationType::FriendSearch, false);
	TSharedRef<FFakeOperation> SecondSearch = MakeShared<FFakeOperation>(Queue, EEasySessionOperationType::FriendSearch, false);
	TSharedRef<FFakeOperation> Matchmaking = MakeShared<FFakeOperation>(Queue, EEasySessionOperationType::Matchmaking, true);

	TestTrue(TEXT("The first operation of a type registers"), Queue.BeginOperation(Search));
	TestFalse(TEXT("A second operation of the same type is refused"), Queue.BeginOperation(SecondSearch));
	TestFalse(TEXT("A read-only operation does not make the queue busy"), Queue.IsBusy());

	TestTrue(TEXT("A different type registers alongside"), Queue.BeginOperation(Matchmaking));
	TestTrue(TEXT("An operation that counts as busy makes the queue busy"), Queue.IsBusy());
	TestTrue(TEXT("Operations do not occupy the request slot"), Queue.IsIdle());
	TestTrue(TEXT("Find returns the registered operation"), Queue.FindOperation(EEasySessionOperationType::Matchmaking) == Matchmaking);
	TestTrue(TEXT("The busy operation is the matchmaking one"), Queue.FindBusyOperation() == Matchmaking);
	TestEqual(TEXT("The status line lists operations in registration order"), Queue.DescribeStatus(false), FString(TEXT("Idle; Fake FriendSearch; Fake Matchmaking")));

	Queue.EndOperation(*Search);
	TestFalse(TEXT("An ended operation is gone"), Queue.FindOperation(EEasySessionOperationType::FriendSearch).IsValid());
	TestTrue(TEXT("The type is free again after the end"), Queue.BeginOperation(SecondSearch));

	// Every cancel ends its operation from inside the walk - the queue must survive its own list changing under it.
	Queue.CancelOperations();
	TestEqual(TEXT("Cancel reached the matchmaking operation"), Matchmaking->CancelCount, 1);
	TestEqual(TEXT("Cancel reached the second search"), SecondSearch->CancelCount, 1);
	TestEqual(TEXT("The ended search was not canceled"), Search->CancelCount, 0);
	TestFalse(TEXT("Nothing is busy after the cancel"), Queue.IsBusy());
	TestEqual(TEXT("The status line is plain again"), Queue.DescribeStatus(false), FString(TEXT("Idle")));

	// The subsystem answers Is Busy and Get Activity from the same list.
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GEngine));
	GameInstance->InitializeStandalone();
	UEasySessionSubsystem* Subsystem = GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(GameInstance.Get());
		return false;
	}

	FEasySessionRequestQueue& SubsystemQueue = FEasySessionTestAccess::GetRequestQueue(*Subsystem);
	TSharedRef<FFakeOperation> Run = MakeShared<FFakeOperation>(SubsystemQueue, EEasySessionOperationType::Matchmaking, true);
	TestFalse(TEXT("The subsystem starts idle"), Subsystem->IsBusy());
	TestTrue(TEXT("The subsystem's queue registers the operation"), SubsystemQueue.BeginOperation(Run));
	TestTrue(TEXT("Is Busy sees the busy operation"), Subsystem->IsBusy());
	TestEqual(TEXT("Get Activity names the busy operation"), Subsystem->GetActivity(), EEasySessionActivity::Matchmaking);
	TestTrue(TEXT("Get Queue Status lists the operation"), Subsystem->GetQueueStatus().Contains(TEXT("Fake Matchmaking")));

	SubsystemQueue.EndOperation(*Run);
	TestFalse(TEXT("Is Busy clears when the operation ends"), Subsystem->IsBusy());
	TestEqual(TEXT("Get Activity is None again"), Subsystem->GetActivity(), EEasySessionActivity::None);

	EasySessionTest::DestroyGameInstance(GameInstance.Get());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
