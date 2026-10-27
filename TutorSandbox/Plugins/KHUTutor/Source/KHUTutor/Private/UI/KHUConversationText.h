#pragma once
#include "CoreMinimal.h"

namespace KHUConversationText
{
    /** Escape untrusted model text before allowing a small markup subset. */
    FString Escape(FString Text);
    /** Render bold/code/list text; links are accepted only from an evidence map. */
    FString Render(const FString& Text, const TMap<FString, FString>& LinkIds);
}
