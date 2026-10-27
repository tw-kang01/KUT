#pragma once
#include "Styling/SlateStyle.h"

/** sRGB palette shared by the style set and the widgets that need a live tint: editor dark-theme values plus one tutor accent. */
namespace KHUTutorPalette
{
    FLinearColor Hex(const TCHAR* Value);
    FLinearColor Accent();
    FLinearColor Icon();
    FLinearColor Faint();
    FLinearColor Secondary();
    FLinearColor Link();
    FLinearColor Warning();
    FLinearColor Error();
    FLinearColor Success();
}

/** Shared lifetime exceeds every tutor widget and its rich-text runs. */
class FKHUTutorStyle
{
public:
    static TSharedRef<FSlateStyleSet> Get();
};
