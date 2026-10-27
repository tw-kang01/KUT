#include "UI/KHUConversationText.h"

FString KHUConversationText::Escape(FString Text)
{
    return Text.Replace(TEXT("&"), TEXT("&amp;")).Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;")).Replace(TEXT("\""), TEXT("&quot;"));
}
FString KHUConversationText::Render(const FString& Text, const TMap<FString, FString>& LinkIds)
{
    FString Out;
    for (int32 I = 0; I < Text.Len();)
    {
        if (Text.Mid(I, 2) == TEXT("**") || Text[I] == '`')
        {
            const bool Bold = Text[I] == '*'; const FString Marker = Bold ? TEXT("**") : TEXT("`");
            const int32 End = Text.Find(Marker, ESearchCase::CaseSensitive, ESearchDir::FromStart, I + Marker.Len());
            if (End > I + Marker.Len())
            {
                const FString Inner = Text.Mid(I + Marker.Len(), End - I - Marker.Len()); bool ContainsLink = false;
                for (const auto& Pair : LinkIds) { ContainsLink |= Inner.Contains(Pair.Key, ESearchCase::CaseSensitive); }
                // Slate rich text uses flat runs. Preserve navigation instead of nesting styles.
                if (ContainsLink) { Out += Render(Inner, LinkIds); }
                else { Out += Bold ? TEXT("<b>") : TEXT("<code>"); Out += Escape(Inner); Out += TEXT("</>"); }
                I = End + Marker.Len(); continue;
            }
        }
        bool Linked = false;
        if (Text[I] == '#' || Text[I] == '@')
        {
            // Longest match prevents #1 stealing #10, and actor prefixes stealing longer labels.
            FString Best, Id;
            for (const auto& Pair : LinkIds)
            {
                if (Pair.Key.Len() <= Best.Len() || Text.Mid(I, Pair.Key.Len()) != Pair.Key) { continue; }
                const int32 Next = I + Pair.Key.Len();
                if (Next < Text.Len() && (FChar::IsAlnum(Text[Next]) || Text[Next] == '_' || (Pair.Key.StartsWith(TEXT("@/")) && (Text[Next] == '.' || Text[Next] == '/' || Text[Next] == ':')))) { continue; }
                Best = Pair.Key; Id = Pair.Value;
            }
            if (!Best.IsEmpty()) { Out += TEXT("<a id=\"khu\" target=\"") + Escape(Id) + TEXT("\">") + Escape(Best) + TEXT("</>"); I += Best.Len(); Linked = true; }
        }
        if (!Linked) { Out += Escape(Text.Mid(I++, 1)); }
    }
    return Out;
}
