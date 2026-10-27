#include "UI/KHUTutorStyle.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateNoResource.h"

// Palette follows the editor FStyleColors dark defaults (SlateCore StyleColors.cpp) plus one tutor accent.
// Hex values are sRGB; FLinearColor(FColor) converts them for Slate.
FLinearColor KHUTutorPalette::Hex(const TCHAR* Value) { return FLinearColor(FColor::FromHex(Value)); }
FLinearColor KHUTutorPalette::Accent() { return Hex(TEXT("3CC7A3")); }
FLinearColor KHUTutorPalette::Icon() { return Hex(TEXT("C0C0C0")); }
FLinearColor KHUTutorPalette::Faint() { return Hex(TEXT("B8B8B8")); }
FLinearColor KHUTutorPalette::Secondary() { return Hex(TEXT("C4C4C4")); }
FLinearColor KHUTutorPalette::Link() { return Hex(TEXT("5DB8FF")); }
FLinearColor KHUTutorPalette::Warning() { return Hex(TEXT("FFB800")); }
FLinearColor KHUTutorPalette::Error() { return Hex(TEXT("F25A5A")); }
FLinearColor KHUTutorPalette::Success() { return Hex(TEXT("1FE44B")); }
TSharedRef<FSlateStyleSet> FKHUTutorStyle::Get()
{
    using KHUTutorPalette::Hex;
    static TWeakPtr<FSlateStyleSet> Weak;
    if (auto Existing = Weak.Pin()) { return Existing.ToSharedRef(); }
    TSharedPtr<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(TEXT("KHUTutorStyle")), [](FSlateStyleSet* S) { FSlateStyleRegistry::UnRegisterSlateStyle(*S); delete S; });
    const FString Resources = IPluginManager::Get().FindPlugin(TEXT("KHUTutor"))->GetBaseDir() / TEXT("Resources");
    const auto Normal = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("NormalText"));

    // Surfaces
    const FLinearColor Recessed = Hex(TEXT("1A1A1A")), Input = Hex(TEXT("1E1E1E")), Panel = Hex(TEXT("242424")), Raised = Hex(TEXT("2F2F2F"));
    const FLinearColor Outline = Hex(TEXT("383838")), OutlineStrong = Hex(TEXT("4C4C4C")), Divider = Hex(TEXT("0F0F0F"));
    // Text (each at least 4.5:1 on #242424)
    const FLinearColor Strong = Hex(TEXT("F5F5F5")), Body = Hex(TEXT("E8E8E8")), Secondary = KHUTutorPalette::Secondary(), Faint = KHUTutorPalette::Faint();
    // Tutor accent and semantic colors
    const FLinearColor Accent = KHUTutorPalette::Accent(), AccentFill = Hex(TEXT("117A5F")), AccentHover = Hex(TEXT("137D61")), AccentPress = Hex(TEXT("0D6049"));
    const FLinearColor AccentDisabled = Hex(TEXT("2A3C36")), AccentTint = Hex(TEXT("1C2624")), AccentLine = Hex(TEXT("2C4A42")), AccentBadge = Hex(TEXT("123F34"));
    const FLinearColor Link = KHUTutorPalette::Link(), Warning = KHUTutorPalette::Warning(), WarningTint = Hex(TEXT("2A2416")), WarningLine = Hex(TEXT("5C4A12"));
    const FLinearColor ErrorText = KHUTutorPalette::Error(), ErrorTint = Hex(TEXT("2A1C1C")), ErrorLine = Hex(TEXT("5A2A2A"));

    // Slate font sizes are points at 96 DPI (1pt = 1.33px). Body 11pt matches the user-confirmed settings page.
    const auto Text = [&](const TCHAR* Name, const TCHAR* Weight, int32 Size, const FLinearColor& Color)
    { Style->Set(Name, FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle(Weight, Size)).SetColorAndOpacity(Color)); };
    Text(TEXT("Normal"), TEXT("Regular"), 11, Body);
    Text(TEXT("b"), TEXT("Bold"), 11, Strong);
    Text(TEXT("Muted"), TEXT("Regular"), 10, Secondary);
    Style->Set(TEXT("code"), FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 11)).SetColorAndOpacity(Strong));
    Text(TEXT("Tutor.AppTitle"), TEXT("Bold"), 11, Strong);
    Text(TEXT("Tutor.Subtitle"), TEXT("Regular"), 10, Secondary);
    Text(TEXT("Tutor.Hero"), TEXT("Bold"), 15, Strong);
    Text(TEXT("Tutor.Body"), TEXT("Regular"), 11, Body);
    Text(TEXT("Tutor.Strong"), TEXT("Bold"), 11, Strong);
    Text(TEXT("Tutor.Label"), TEXT("Bold"), 10, Strong);
    Text(TEXT("Tutor.Caption"), TEXT("Regular"), 10, Secondary);
    Text(TEXT("Tutor.Micro"), TEXT("Regular"), 10, Faint);
    Text(TEXT("Tutor.Badge"), TEXT("Bold"), 9, Accent);
    Text(TEXT("Tutor.AccentLabel"), TEXT("Bold"), 10, Accent);
    Text(TEXT("Tutor.WarningLabel"), TEXT("Bold"), 10, Warning);
    Text(TEXT("Tutor.ErrorText"), TEXT("Regular"), 10, ErrorText);
    Text(TEXT("Tutor.ButtonText"), TEXT("Regular"), 10, Strong);
    Text(TEXT("Tutor.PrimaryText"), TEXT("Bold"), 10, FLinearColor::White);
    Text(TEXT("Tutor.GhostText"), TEXT("Regular"), 10, Body);
    Style->Set(TEXT("Hyperlink"), FHyperlinkStyle(FAppStyle::Get().GetWidgetStyle<FHyperlinkStyle>(TEXT("Hyperlink")))
        .SetTextStyle(FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11)).SetColorAndOpacity(Link)));

    // Surfaces and cards
    Style->Set(TEXT("Panel"), new FSlateRoundedBoxBrush(Recessed, 0.0f));
    Style->Set(TEXT("Tutor.Header"), new FSlateRoundedBoxBrush(Panel, 0.0f));
    Style->Set(TEXT("Tutor.ContextBar"), new FSlateRoundedBoxBrush(Input, 0.0f));
    Style->Set(TEXT("Tutor.Divider"), new FSlateRoundedBoxBrush(Divider, 0.0f));
    Style->Set(TEXT("Tutor.Rule"), new FSlateRoundedBoxBrush(Outline, 0.0f));
    Style->Set(TEXT("Card"), new FSlateRoundedBoxBrush(Raised, 10.0f));
    Style->Set(TEXT("TutorCard"), new FSlateRoundedBoxBrush(Recessed, 8.0f));
    Style->Set(TEXT("Avatar"), new FSlateRoundedBoxBrush(AccentBadge, 11.0f));
    Style->Set(TEXT("Tutor.StudentAvatar"), new FSlateRoundedBoxBrush(Hex(TEXT("3A4A5A")), 11.0f));
    Style->Set(TEXT("Tutor.HeroAvatar"), new FSlateRoundedBoxBrush(AccentBadge, 18.0f));
    Style->Set(TEXT("Tutor.Badge"), new FSlateRoundedBoxBrush(AccentBadge, 9.0f));
    Style->Set(TEXT("Tutor.Dot"), new FSlateRoundedBoxBrush(FLinearColor::White, 3.5f, FVector2f(7.0f, 7.0f)));
    Style->Set(TEXT("Tutor.Chip"), new FSlateRoundedBoxBrush(Panel, 13.0f, Outline, 1.0f));
    Style->Set(TEXT("Tutor.ChipWarning"), new FSlateRoundedBoxBrush(WarningTint, 13.0f, WarningLine, 1.0f));
    Style->Set(TEXT("Tutor.Pin"), new FSlateRoundedBoxBrush(Panel, 11.0f, Outline, 1.0f));
    Style->Set(TEXT("Tutor.Evidence"), new FSlateRoundedBoxBrush(Input, 8.0f, Outline, 1.0f));
    Style->Set(TEXT("Tutor.Inset"), new FSlateRoundedBoxBrush(Panel, 8.0f));
    Style->Set(TEXT("Tutor.InputBox"), new FSlateRoundedBoxBrush(Input, 10.0f, Outline, 1.0f));
    Style->Set(TEXT("Tutor.Coach"), new FSlateRoundedBoxBrush(AccentTint, 10.0f, AccentLine, 1.0f));
    Style->Set(TEXT("Tutor.CoachBody"), new FSlateNoResource());
    Style->Set(TEXT("Tutor.Example"), new FSlateRoundedBoxBrush(Input, 6.0f, OutlineStrong, 1.0f));
    Style->Set(TEXT("Tutor.Notice.Info"), new FSlateRoundedBoxBrush(Panel, 10.0f, Outline, 1.0f));
    Style->Set(TEXT("Tutor.Notice.Success"), new FSlateRoundedBoxBrush(AccentTint, 10.0f, AccentLine, 1.0f));
    Style->Set(TEXT("Tutor.Notice.Warning"), new FSlateRoundedBoxBrush(WarningTint, 10.0f, WarningLine, 1.0f));
    Style->Set(TEXT("Tutor.Notice.Error"), new FSlateRoundedBoxBrush(ErrorTint, 10.0f, ErrorLine, 1.0f));

    // Buttons
    const FButtonStyle BaseButton = FButtonStyle(FAppStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button")))
        .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0))
        .SetNormalForeground(Strong).SetHoveredForeground(Strong).SetPressedForeground(Strong).SetDisabledForeground(Faint);
    const auto Buttons = [&](const TCHAR* Name, const FSlateBrush& N, const FSlateBrush& H, const FSlateBrush& P, const FSlateBrush& D)
    { Style->Set(Name, FButtonStyle(BaseButton).SetNormal(N).SetHovered(H).SetPressed(P).SetDisabled(D)); };
    Buttons(TEXT("Tutor.PrimaryButton"), FSlateRoundedBoxBrush(AccentFill, 7.0f), FSlateRoundedBoxBrush(AccentHover, 7.0f), FSlateRoundedBoxBrush(AccentPress, 7.0f), FSlateRoundedBoxBrush(AccentDisabled, 7.0f));
    Buttons(TEXT("Tutor.SecondaryButton"), FSlateRoundedBoxBrush(Raised, 7.0f, OutlineStrong, 1.0f), FSlateRoundedBoxBrush(Outline, 7.0f, OutlineStrong, 1.0f),
        FSlateRoundedBoxBrush(Panel, 7.0f, OutlineStrong, 1.0f), FSlateRoundedBoxBrush(Panel, 7.0f, Outline, 1.0f));
    Buttons(TEXT("Tutor.GhostButton"), FSlateNoResource(), FSlateRoundedBoxBrush(Raised, 6.0f), FSlateRoundedBoxBrush(Outline, 6.0f), FSlateNoResource());
    Buttons(TEXT("Tutor.ChipButton"), FSlateRoundedBoxBrush(Panel, 14.0f, Outline, 1.0f), FSlateRoundedBoxBrush(Raised, 14.0f, OutlineStrong, 1.0f),
        FSlateRoundedBoxBrush(AccentTint, 14.0f, Accent, 1.0f), FSlateRoundedBoxBrush(Panel, 14.0f, Outline, 1.0f));
    Buttons(TEXT("Tutor.HintButton"), FSlateRoundedBoxBrush(AccentTint, 6.0f, AccentLine, 1.0f), FSlateRoundedBoxBrush(AccentBadge, 6.0f, AccentLine, 1.0f),
        FSlateRoundedBoxBrush(AccentBadge, 6.0f, Accent, 1.0f), FSlateRoundedBoxBrush(AccentTint, 6.0f, AccentLine, 1.0f));
    Buttons(TEXT("Tutor.StarterButton"), FSlateRoundedBoxBrush(Panel, 10.0f, Outline, 1.0f), FSlateRoundedBoxBrush(Raised, 10.0f, OutlineStrong, 1.0f),
        FSlateRoundedBoxBrush(AccentTint, 10.0f, Accent, 1.0f), FSlateRoundedBoxBrush(Panel, 10.0f, Outline, 1.0f));
    Buttons(TEXT("Tutor.ListButton"), FSlateRoundedBoxBrush(Panel, 8.0f), FSlateRoundedBoxBrush(Raised, 8.0f), FSlateRoundedBoxBrush(Outline, 8.0f), FSlateRoundedBoxBrush(Panel, 8.0f));
    Buttons(TEXT("Tutor.ListButtonActive"), FSlateRoundedBoxBrush(AccentTint, 8.0f, Accent, 1.0f), FSlateRoundedBoxBrush(AccentTint, 8.0f, Accent, 1.0f),
        FSlateRoundedBoxBrush(AccentBadge, 8.0f, Accent, 1.0f), FSlateRoundedBoxBrush(AccentTint, 8.0f, AccentLine, 1.0f));
    Style->Set(TEXT("Tutor.InputText"), FEditableTextBoxStyle(FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox")))
        .SetBackgroundImageNormal(FSlateNoResource()).SetBackgroundImageHovered(FSlateNoResource()).SetBackgroundImageFocused(FSlateNoResource())
        .SetBackgroundImageReadOnly(FSlateNoResource()).SetPadding(FMargin(10, 8, 10, 4)).SetForegroundColor(Strong).SetFocusedForegroundColor(Strong));

    // Settings and records pages keep their user-confirmed structure; only the palette changes.
    Style->Set(TEXT("Settings.Title"), FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 18)).SetColorAndOpacity(Strong));
    Style->Set(TEXT("Settings.Section"), FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13)).SetColorAndOpacity(Strong));
    Style->Set(TEXT("Settings.Label"), FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11)).SetColorAndOpacity(Strong));
    Style->Set(TEXT("Settings.Body"), FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11)).SetColorAndOpacity(Body));
    Style->Set(TEXT("Settings.Caption"), FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10)).SetColorAndOpacity(Secondary));
    Style->Set(TEXT("Settings.DangerText"), FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11)).SetColorAndOpacity(ErrorText));
    Style->Set(TEXT("Settings.Card"), new FSlateRoundedBoxBrush(Panel, 10.0f, Outline, 1.0f));
    Style->Set(TEXT("Settings.Inset"), new FSlateRoundedBoxBrush(Input, 8.0f));
    Style->Set(TEXT("Settings.Step"), new FSlateRoundedBoxBrush(AccentBadge, 14.0f));
    const FSlateRoundedBoxBrush Choice(Input, 8.0f, Outline, 1.0f);
    const FSlateRoundedBoxBrush Hover(Raised, 8.0f, OutlineStrong, 1.0f);
    const FSlateRoundedBoxBrush Checked(AccentTint, 8.0f, Accent, 1.0f);
    Style->Set(TEXT("Settings.Choice"), FCheckBoxStyle(FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("ToggleButtonCheckbox")))
        .SetCheckBoxType(ESlateCheckBoxType::ToggleButton).SetPadding(FMargin(12, 10))
        .SetUncheckedImage(Choice).SetUncheckedHoveredImage(Hover).SetUncheckedPressedImage(Hover)
        .SetCheckedImage(Checked).SetCheckedHoveredImage(Checked).SetCheckedPressedImage(Checked));
    const FButtonStyle SecondaryButton = FButtonStyle(BaseButton).SetNormal(FSlateRoundedBoxBrush(Raised, 7.0f, OutlineStrong, 1.0f))
        .SetHovered(FSlateRoundedBoxBrush(Outline, 7.0f, OutlineStrong, 1.0f)).SetPressed(Checked).SetDisabled(FSlateRoundedBoxBrush(Panel, 7.0f, Outline, 1.0f));
    Style->Set(TEXT("Settings.SecondaryButton"), SecondaryButton);
    Style->Set(TEXT("Settings.PrimaryButton"), FButtonStyle(SecondaryButton).SetNormal(FSlateRoundedBoxBrush(AccentFill, 7.0f))
        .SetHovered(FSlateRoundedBoxBrush(AccentHover, 7.0f)).SetPressed(FSlateRoundedBoxBrush(AccentPress, 7.0f)).SetDisabled(FSlateRoundedBoxBrush(AccentDisabled, 7.0f)));
    Style->Set(TEXT("Settings.DangerButton"), FButtonStyle(BaseButton).SetNormal(FSlateNoResource()).SetHovered(FSlateRoundedBoxBrush(ErrorTint, 7.0f))
        .SetPressed(FSlateRoundedBoxBrush(ErrorTint, 7.0f, ErrorLine, 1.0f)).SetDisabled(FSlateNoResource()));
    Style->Set(TEXT("Settings.KeyInput"), FEditableTextBoxStyle(FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox")))
        .SetBackgroundImageNormal(Choice).SetBackgroundImageHovered(Hover).SetBackgroundImageFocused(Checked)
        .SetPadding(FMargin(12, 10)).SetForegroundColor(Strong));

    // Icons: white SVG tinted by the widget. The KHU mark is the reversed (white) form for the dark editor.
    const auto Icon = [&](const TCHAR* Name, const TCHAR* File, float W, float H)
    { Style->Set(Name, new FSlateVectorImageBrush(Resources / File, FVector2D(W, H))); };
    Icon(TEXT("Tutor"), TEXT("Tutor.svg"), 20, 20);
    Icon(TEXT("KHUMark"), TEXT("KHUMark.svg"), 27, 16);
    Icon(TEXT("Icon.Spark"), TEXT("Spark.svg"), 12, 12);
    Icon(TEXT("Icon.SparkLarge"), TEXT("Spark.svg"), 20, 20);
    Icon(TEXT("Icon.History"), TEXT("History.svg"), 16, 16);
    Icon(TEXT("Icon.Copy"), TEXT("Copy.svg"), 13, 13);
    Icon(TEXT("Icon.ThumbUp"), TEXT("ThumbUp.svg"), 13, 13);
    Icon(TEXT("Icon.ThumbDown"), TEXT("ThumbDown.svg"), 13, 13);
    Icon(TEXT("Icon.Send"), TEXT("Send.svg"), 13, 13);
    Icon(TEXT("Icon.Stop"), TEXT("Stop.svg"), 11, 11);
    Icon(TEXT("Icon.Eye"), TEXT("Eye.svg"), 13, 13);
    Icon(TEXT("Icon.Steps"), TEXT("Steps.svg"), 16, 16);
    Icon(TEXT("Icon.Nodes"), TEXT("Nodes.svg"), 16, 16);
    FSlateStyleRegistry::RegisterSlateStyle(*Style); Weak = Style; return Style.ToSharedRef();
}
