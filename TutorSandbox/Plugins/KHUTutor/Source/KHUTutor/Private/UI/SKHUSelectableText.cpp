#include "UI/SKHUSelectableText.h"
#include "UI/KHUConversationText.h"
#include "UI/KHUTutorStyle.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/TextDecorators.h"

namespace
{
    FString RenderSelectable(const FString& Source, const TMap<FString, FString>& LinkIds)
    {
        // Fence markers are presentation syntax, not code. Escape each code line separately:
        // Slate markup runs cannot span line breaks, and C++ operators must stay literal.
        TArray<FString> Lines;
        Source.Replace(TEXT("\r\n"), TEXT("\n")).Replace(TEXT("\r"), TEXT("\n")).ParseIntoArray(Lines, TEXT("\n"), false);
        TArray<FString> Rendered;
        bool InCode = false;
        for (const FString& Line : Lines)
        {
            const FString Trimmed = Line.TrimStartAndEnd();
            if ((!InCode && Trimmed.StartsWith(TEXT("```"))) || (InCode && Trimmed == TEXT("```")))
            {
                InCode = !InCode;
                continue;
            }
            Rendered.Add(InCode && !Line.IsEmpty()
                ? TEXT("<code>") + KHUConversationText::Escape(Line) + TEXT("</>")
                : KHUConversationText::Render(Line, InCode ? TMap<FString, FString>() : LinkIds));
        }
        return FString::Join(Rendered, TEXT("\n"));
    }
}

void SKHUSelectableText::Construct(const FArguments& Args)
{
    TextStyleOwner = Args._Style.IsValid() ? Args._Style : FKHUTutorStyle::Get();
    TArray<TSharedRef<ITextDecorator>> Decorators;
    if (Args._OnNavigate.IsBound())
    {
        Decorators.Add(FHyperlinkDecorator::Create(TEXT("khu"), Args._OnNavigate));
    }
    const auto Marshaller = FRichTextLayoutMarshaller::Create(MoveTemp(Decorators), TextStyleOwner.Get());
    SMultiLineEditableText::Construct(SMultiLineEditableText::FArguments()
        .Text(FText::FromString(RenderSelectable(Args._Text, Args._LinkIds)))
        .Marshaller(Marshaller)
        .TextStyle(&TextStyleOwner->GetWidgetStyle<FTextBlockStyle>(TEXT("Normal")))
        .AutoWrapText(true)
        .WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
        .LineHeightPercentage(1.4f)
        .IsReadOnly(true)
        .SelectAllTextWhenFocused(false)
        .SelectWordOnMouseDoubleClick(true)
        .ClearTextSelectionOnFocusLoss(false)
        .AllowContextMenu(true)
        .Margin(FMargin(0)));
}
