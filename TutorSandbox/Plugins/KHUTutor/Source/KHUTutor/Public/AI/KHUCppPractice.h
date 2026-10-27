#pragma once
#include "CoreMinimal.h"

/** Student-facing assignments only. Solutions and test oracles never enter the prompt catalog. */
struct KHUTUTOR_API FKHUCppExercise
{
    FString Id, Title, Goal, Problem, StudentWork, Verification, Transfer;
};

/** Self-contained drafts travel through the existing conversation, hint and memory boundaries. */
class KHUTUTOR_API FKHUCppPractice
{
public:
    /** Stable ids allow a saved assignment to be identified without inferring completion. */
    static const TArray<FKHUCppExercise>& All();
    /** Return an editable assignment, or empty for an unknown id. Never sends a request. */
    static FString Draft(const FString& Id, bool bTransfer = false);
};
