#pragma once
#include "AI/KHUChatClient.h"

/** Bounded per-model EMA. Persistence contains no text, keys, paths or account data. */
class KHUTUTOR_API FKHUTokenEstimator
{
public:
    explicit FKHUTokenEstimator(FKHUContextPolicy InPolicy, FString InPath = TEXT(""));
    /** Conservative estimate of the serialized input, not an exact tokenizer result. */
    int32 Estimate(const FString& Model, int32 Utf8Bytes, bool bOverhead = true) const;
    /** Learn only from a real, known usage sample for the actual final request body. */
    bool Observe(const FString& Model, int32 Utf8Bytes, int64 InputTokens);
    double Ratio(const FString& Model) const;
    int32 Samples(const FString& Model) const;
private:
    void Load();
    FKHUContextPolicy Policy;
    FString Path;
    TMap<FString, TPair<double, int32>> Calibration;
};

struct KHUTUTOR_API FKHUContextResult
{
    bool bSuccess = false;
    FString Error;
    int32 InputBytes = 0, EstimatedTokens = 0, OmittedTurns = 0;
    TSharedPtr<FJsonObject> Segments;
};

/** Removes only complete older turns; current evidence and Responses output stay intact. */
class KHUTUTOR_API FKHUContextBudgeter
{
public:
    /** On failure, Request is unchanged. Original/current question and latest tool turn are pinned. */
    static FKHUContextResult Build(FKHUChatRequest& Request, const FKHUContextPolicy& Policy,
        const FKHUTokenEstimator& Estimator, int32 TokenLimit);
    /** Validate call/result pairing before sending either supported API representation. */
    static bool Validate(const TArray<TSharedPtr<FJsonValue>>& Messages);
    static int32 Utf8Bytes(const FString& Text);
};
