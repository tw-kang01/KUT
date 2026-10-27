#pragma once
#include "CoreMinimal.h"

/** User-scoped Windows DPAPI. No plaintext credential is written or logged. */
class KHUTUTOR_API FKHUSecretStore
{
public:
    explicit FKHUSecretStore(FString InPath) : Path(MoveTemp(InPath)) {}
    bool Save(const FString& Key, FString& Error) const;
    bool Load(FString& Key, FString& Error, bool bAllowEnvironment = true) const;
    bool Delete(FString& Error) const;
    static bool IsValidKey(const FString& Key);
    static FString Mask(const FString& Key);
    static FString Redact(FString Text, const FString& Key);
private:
    FString Path;
};
