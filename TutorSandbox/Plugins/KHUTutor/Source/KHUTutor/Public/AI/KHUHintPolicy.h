#pragma once
#include "CoreMinimal.h"

/** Teaching rules shared by the request builder and tool authorization. */
struct KHUTUTOR_API FKHUHintPolicy
{
    /** Return the level cap, with the documented reasoning-model minimum. */
    static int32 OutputTokens(int32 Level, bool bReasoning);
    /** Build the hint note for the system block. It changes only with the level or tier, so the cached prefix survives between turns. */
    static FString SystemNote(int32 Level, const FString& Tier);
    /** Show is unavailable at L0; Act tools are not implemented. */
    static bool AllowShow(int32 Level) { return Level >= 1 && Level <= 4; }
};

/** Tracks consecutive "I don't know / too hard" messages so the panel can offer (never apply) a higher hint level at L0. */
struct KHUTUTOR_API FKHUStuckStreak
{
    /** Conservative Korean/English markers of not knowing or finding it too hard. */
    static bool IsStuck(const FString& Message);
    /** Record one student message; any message without a marker ends the streak. */
    void Note(const FString& Message);
    /** True once per streak: at L0, after two consecutive stuck messages. Marks the offer as made. */
    bool TakeOffer(int32 Hint);
    void Reset() { Count = 0; bOffered = false; }
    int32 Count = 0;
    bool bOffered = false;
};
