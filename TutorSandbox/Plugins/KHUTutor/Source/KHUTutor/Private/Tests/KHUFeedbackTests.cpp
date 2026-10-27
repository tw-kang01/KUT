#include "UI/KHUFeedbackStore.h"
#include "UI/SKHUTutorPanel.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Framework/Application/SlateApplication.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto FeedbackFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFeedbackStoreTest, "KHUTutor.UI.Feedback.LocalToggleRestartAndDamage", FeedbackFlags)
bool FFeedbackStoreTest::RunTest(const FString&)
{
    const FString Path = FPaths::ProjectSavedDir() / TEXT("KHUTutor/Tests") / (TEXT("Feedback_") + FGuid::NewGuid().ToString() + TEXT(".json"));
    const FString A = FKHUFeedbackStore::AnswerId(TEXT("c1"), TEXT("ANSWER_TEXT_SENTINEL"));
    TestNotEqual(TEXT("Answer ids differ by conversation"), A, FKHUFeedbackStore::AnswerId(TEXT("c2"), TEXT("ANSWER_TEXT_SENTINEL")));
    TestNotEqual(TEXT("Answer ids differ by shown text"), A, FKHUFeedbackStore::AnswerId(TEXT("c1"), TEXT("other answer")));
    FString Out;
    {
        FKHUFeedbackStore Store(Path);
        TestTrue(TEXT("Helpful saved"), Store.Toggle(A, TEXT("up"), TEXT("c1"), 2, Out)); TestEqual(TEXT("Helpful selected"), Out, FString(TEXT("up")));
        TestTrue(TEXT("Same choice clears"), Store.Toggle(A, TEXT("up"), TEXT("c1"), 2, Out)); TestTrue(TEXT("Cleared"), Out.IsEmpty());
        TestTrue(TEXT("Other choice saved"), Store.Toggle(A, TEXT("down"), TEXT("c1"), 2, Out)); TestEqual(TEXT("Not helpful selected"), Out, FString(TEXT("down")));
        TestFalse(TEXT("Unknown rating refused"), Store.Toggle(A, TEXT("score"), TEXT("c1"), 2, Out));
        TestEqual(TEXT("Refused rating keeps previous choice"), Store.Get(A), FString(TEXT("down")));
    }
    {
        FKHUFeedbackStore Restart(Path);
        TestEqual(TEXT("Choice survives restart"), Restart.Get(A), FString(TEXT("down")));
        TestEqual(TEXT("One entry per answer"), Restart.Num(), 1);
    }
    FString Disk; FFileHelper::LoadFileToString(Disk, *Path);
    TestFalse(TEXT("Answer text is not copied into the feedback file"), Disk.Contains(TEXT("ANSWER_TEXT_SENTINEL")));
    FFileHelper::SaveStringToFile(TEXT("broken-feedback"), *Path);
    {
        FKHUFeedbackStore Damaged(Path);
        TestFalse(TEXT("Damaged file refuses new writes"), Damaged.Toggle(A, TEXT("up"), TEXT("c1"), 1, Out));
        TestFalse(TEXT("Damage is explained"), Damaged.Error().IsEmpty());
        FString After; FFileHelper::LoadFileToString(After, *Path);
        TestEqual(TEXT("Damaged original is left untouched"), After, FString(TEXT("broken-feedback")));
    }
    IFileManager::Get().Delete(*Path, false, true, true);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFeedbackPanel, "KHUTutor.UI.Sidebar.EmptyStateActivityAndNotices", FeedbackFlags)
bool FFeedbackPanel::RunTest(const FString&)
{
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Slate required for panel test")); return false; }
    auto Panel = SNew(SKHUTutorPanel);
    TestEqual(TEXT("New panel starts on the empty state"), Panel->TurnCount, 0);
    const int32 Before = Panel->MessageCount;
    Panel->BeginActivity();
    TestTrue(TEXT("Turn card is live while working"), Panel->Activity.IsValid() && Panel->Activity->bLive);
    TestEqual(TEXT("Turn card is not counted as a message"), Panel->MessageCount, Before);
    Panel->OnEvent(EKHUAgentState::AskContinue, TEXT("PAUSE_TEXT"));
    TestEqual(TEXT("Pause becomes one notice"), Panel->MessageCount, Before + 1);
    TestEqual(TEXT("Pause is not shown as a tutor answer"), Panel->TurnCount, 0);
    TestFalse(TEXT("Turn card stops when paused"), Panel->Activity->bLive);
    Panel->OnEvent(EKHUAgentState::Idle, TEXT("ANSWER_TEXT"));
    TestEqual(TEXT("Answer appended once"), Panel->MessageCount, Before + 2);
    TestEqual(TEXT("Answer hides the empty state"), Panel->TurnCount, 1);
    TestFalse(TEXT("Answer is not repeated in status"), Panel->Status.Contains(TEXT("ANSWER_TEXT")));
    Panel->ResetTask(false);
    TestEqual(TEXT("New task returns to the empty state without a notice"), Panel->MessageCount, 0);
    TestEqual(TEXT("New task clears turns"), Panel->TurnCount, 0);
    Panel->ResetTask(true);
    TestEqual(TEXT("Solved task leaves one success notice"), Panel->MessageCount, 1);
    TestEqual(TEXT("Solved notice keeps the starters visible"), Panel->TurnCount, 0);
    Panel->Stop(); return true;
}
#endif
