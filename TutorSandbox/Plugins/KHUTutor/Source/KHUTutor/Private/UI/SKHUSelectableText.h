#pragma once

#include "Widgets/Text/SMultiLineEditableText.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "Styling/SlateStyle.h"

/** Read-only chat text with native selection, keyboard/context copy and evidence links. */
class SKHUSelectableText : public SMultiLineEditableText
{
public:
    using FLinkMap = TMap<FString, FString>;
    SLATE_BEGIN_ARGS(SKHUSelectableText) {}
        SLATE_ARGUMENT(FString, Text)
        SLATE_ARGUMENT(FLinkMap, LinkIds)
        SLATE_ARGUMENT(TSharedPtr<FSlateStyleSet>, Style)
        SLATE_EVENT(FSlateHyperlinkRun::FOnClick, OnNavigate)
    SLATE_END_ARGS()

    /** Build a selectable display; text remains immutable through user input. */
    void Construct(const FArguments& Args);

private:
    // The marshaller stores a raw style pointer, so retain its owner for the widget lifetime.
    TSharedPtr<FSlateStyleSet> TextStyleOwner;
};
