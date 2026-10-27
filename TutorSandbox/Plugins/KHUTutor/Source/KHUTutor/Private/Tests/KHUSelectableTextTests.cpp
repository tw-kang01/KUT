#include "UI/SKHUSelectableText.h"
#include "UI/KHUTutorStyle.h"
#include "Misc/AutomationTest.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FString NormalizeLines(FString Text) { return Text.Replace(TEXT("\r\n"), TEXT("\n")).Replace(TEXT("\r"), TEXT("\n")); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSelectableChatCopy, "KHUTutor.UI.SelectableChat.PlainTextAndCode", Flags)
bool FSelectableChatCopy::RunTest(const FString&)
{
    const FString Source = TEXT("**선택** `Target` → #42\n```cpp\nif (Ready && Count < 3)\n{\n\tCount += 1;\n}\n```\n마지막 줄");
    TMap<FString, FString> Links; Links.Add(TEXT("#42"), TEXT("7"));
    const auto Text = SNew(SKHUSelectableText).Text(Source).Style(FKHUTutorStyle::Get()).LinkIds(Links)
        .OnNavigate(FSlateHyperlinkRun::FOnClick::CreateLambda([](const FSlateHyperlinkRun::FMetadata&) {}));
    Text->SlatePrepass();
    Text->SelectAllText();
    const FString Expected = TEXT("선택 Target → #42\nif (Ready && Count < 3)\n{\n\tCount += 1;\n}\n마지막 줄");
    TestTrue(TEXT("Read-only chat supports native text selection"), Text->IsTextReadOnly() && Text->AnyTextSelected());
    TestEqual(TEXT("Copy payload has displayed text, C++ operators and indentation without markup"), NormalizeLines(Text->GetSelectedText().ToString()), Expected);
    Text->SelectText(FTextLocation(1, 0), FTextLocation(4, 1));
    TestEqual(TEXT("Selecting only the code preserves its exact content"), NormalizeLines(Text->GetSelectedText().ToString()), TEXT("if (Ready && Count < 3)\n{\n\tCount += 1;\n}"));
    Text->SelectText(FTextLocation(0, 3), FTextLocation(0, 9));
    TestEqual(TEXT("Partial selection across formatted runs is plain text"), Text->GetSelectedText().ToString(), TEXT("Target"));

    // Exercise the actual input handler without touching the user's clipboard.
    const TSharedRef<SWidget> Widget = Text;
    Widget->OnKeyChar(FGeometry(), FCharacterEvent(TEXT('x'), FModifierKeysState(), 0, false));
    Widget->OnKeyDown(FGeometry(), FKeyEvent(FKey(TEXT("Delete")), FModifierKeysState(), 0, false, 0, 0));
    Text->SelectAllText();
    TestEqual(TEXT("Typing and deleting never alter an answer"), NormalizeLines(Text->GetSelectedText().ToString()), Expected);
    TestTrue(TEXT("Selectable answer accepts keyboard focus"), Widget->SupportsKeyboardFocus());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSelectableChatLinks, "KHUTutor.UI.SelectableChat.EvidenceAndEscaping", Flags)
bool FSelectableChatLinks::RunTest(const FString&)
{
    TMap<FString, FString> Links; Links.Add(TEXT("#42"), TEXT("7"));
    const FString Source = TEXT("#42 #420 <a id=\"khu\" target=\"999\">fake</>\n```cpp\n// #42 **literal** `backtick` <T> & Value\n```");
    const auto Text = SNew(SKHUSelectableText).Text(Source).LinkIds(Links)
        .OnNavigate(FSlateHyperlinkRun::FOnClick::CreateLambda([](const FSlateHyperlinkRun::FMetadata&) {}));
    Text->SlatePrepass();
    Text->SelectAllText();
    int32 LinkCount = 0, CodeCount = 0;
    for (const auto& Run : Text->GetSelectedRuns())
    {
        const FRunInfo& Info = Run->GetRunInfo();
        if (Info.Name == TEXT("a"))
        {
            ++LinkCount;
            TestEqual(TEXT("Evidence link retains navigation target"), Info.MetaData.FindRef(TEXT("target")), TEXT("7"));
        }
        if (Info.Name == TEXT("code")) { ++CodeCount; }
    }
    TestEqual(TEXT("Only the known evidence reference becomes a link"), LinkCount, 1);
    TestEqual(TEXT("Fenced code is a mono text run, with no nested markup or links"), CodeCount, 1);
    TestEqual(TEXT("Escaped model markup and fenced content remain literal when selected"), NormalizeLines(Text->GetSelectedText().ToString()),
        TEXT("#42 #420 <a id=\"khu\" target=\"999\">fake</>\n// #42 **literal** `backtick` <T> & Value"));
    return true;
}
#endif
