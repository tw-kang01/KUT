#pragma once
#include "CoreMinimal.h"

/**
 * Local-only answer feedback (helpful / not helpful). Never sent to a model, never scored, never blocks input.
 * A damaged file is left untouched and further writes are refused, like the project memory store.
 */
class FKHUFeedbackStore
{
public:
    static constexpr int32 MaxEntries = 2000;
    explicit FKHUFeedbackStore(FString InPath);
    /** Stable id for one displayed answer: conversation plus a CRC of the shown text. */
    static FString AnswerId(const FString& Conversation, const FString& Text);
    /** "up", "down" or empty. */
    FString Get(const FString& Id) const;
    /** Choosing the current rating again clears it. On save failure nothing changes and false is returned. */
    bool Toggle(const FString& Id, const FString& Rating, const FString& Conversation, int32 Hint, FString& OutRating);
    const FString& Error() const { return LastError; }
    int32 Num() const { return Entries.Num(); }
private:
    struct FEntry { FString Id, Rating, Conversation, UpdatedUtc; int32 Hint = 0; };
    bool Save(const TArray<FEntry>& Next);
    FString Path, LastError;
    TArray<FEntry> Entries;
    bool bDamaged = false;
};
