#pragma once
#include "CoreMinimal.h"

/** Fail-closed, per-operation modal; an Act tool must call this itself so no external caller can bypass it. */
class KHUTUTOR_API FKHUApprovalGate
{
public:
    /** Show an explicit preview, defaulting to No; headless calls always deny. */
    static bool Confirm(const FText& Preview);
};
