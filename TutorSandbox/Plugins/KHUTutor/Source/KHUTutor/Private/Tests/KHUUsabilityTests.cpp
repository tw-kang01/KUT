#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/SKHUTutorPanel.h"
#include "UI/KHUTutorStyle.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
    constexpr auto UsabilityFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    double Luminance(const FLinearColor& C) { return 0.2126 * C.R + 0.7152 * C.G + 0.0722 * C.B; }
    double Contrast(const FSlateColor& A, const FSlateColor& B)
    {
        const double L1 = Luminance(A.GetSpecifiedColor()), L2 = Luminance(B.GetSpecifiedColor());
        return (FMath::Max(L1, L2) + 0.05) / (FMath::Min(L1, L2) + 0.05);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposerDraftNotice, "KHUTutor.UI.Usability.DraftNoticeAndHelperPreservation", UsabilityFlags)
bool FComposerDraftNotice::RunTest(const FString&)
{
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Slate is required")); return false; }
    auto Panel = SNew(SKHUTutorPanel);
    const FString Draft = TEXT("작성 중인 C++ 질문\nif (Count < 3) { ++Count; }");
    Panel->Input->SetText(FText::FromString(Draft));
    Panel->DraftPractice(TEXT("review"), false);
    TestEqual(TEXT("Practice choice preserves exact unsent code"), Panel->Input->GetText().ToString(), Draft);
    TestTrue(TEXT("Conflict uses prominent warning"), Panel->bDraftConflict);
    TestEqual(TEXT("Composer notice visible"), Panel->ComposerNotice->GetVisibility(), EVisibility::Visible);
    TestTrue(TEXT("Action explained"), Panel->Status.Contains(TEXT("먼저 보내거나")));
    Panel->RunQuick(SKHUTutorPanel::EQuick::ExplainBack);
    TestEqual(TEXT("Explain helper cannot replace existing draft"), Panel->Input->GetText().ToString(), Draft);
    Panel->RunQuick(SKHUTutorPanel::EQuick::Where);
    TestEqual(TEXT("Location helper cannot replace existing draft"), Panel->Input->GetText().ToString(), Draft);
    TestFalse(TEXT("Choosing templates never starts request"), Panel->Loop.IsValid());
    Panel->Input->SetText(FText::GetEmpty());
    TestFalse(TEXT("Editing resolves stale warning"), Panel->bDraftConflict);
    Panel->RunQuick(SKHUTutorPanel::EQuick::Where);
    TestTrue(TEXT("Empty composer accepts template"), Panel->Input->GetText().ToString().Contains(TEXT("찾고 싶은")));
    Panel->Stop(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReadableTutorPalette, "KHUTutor.UI.Usability.TextContrast", UsabilityFlags)
bool FReadableTutorPalette::RunTest(const FString&)
{
    const auto Style = FKHUTutorStyle::Get();
    for (const TCHAR* Surface : {TEXT("Panel"), TEXT("Tutor.Header"), TEXT("Card"), TEXT("Tutor.Notice.Info"), TEXT("Tutor.Notice.Warning")})
    {
        for (const TCHAR* Text : {TEXT("Normal"), TEXT("Tutor.Body"), TEXT("Tutor.Caption"), TEXT("Tutor.Micro"), TEXT("Tutor.GhostText")})
        {
            const double Ratio = Contrast(Style->GetWidgetStyle<FTextBlockStyle>(Text).ColorAndOpacity, Style->GetBrush(Surface)->TintColor);
            TestTrue(FString::Printf(TEXT("%s on %s contrast %.2f >= 4.5"), Text, Surface, Ratio), Ratio >= 4.5);
        }
    }
    const auto& Primary = Style->GetWidgetStyle<FButtonStyle>(TEXT("Tutor.PrimaryButton"));
    const auto& Label = Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.PrimaryText"));
    TestTrue(TEXT("Primary label normal contrast"), Contrast(Label.ColorAndOpacity, Primary.Normal.TintColor) >= 4.5);
    TestTrue(TEXT("Primary label hover contrast"), Contrast(Label.ColorAndOpacity, Primary.Hovered.TintColor) >= 4.5);
    TestTrue(TEXT("Primary label pressed contrast"), Contrast(Label.ColorAndOpacity, Primary.Pressed.TintColor) >= 4.5);
    return true;
}
#endif
