#pragma once
#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "HAL/CriticalSection.h"

/** One captured line with monotonic time, without console color/control flags. */
struct KHUTUTORTOOLS_API FKHULogEntry
{
    double Time = 0;
    ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
    FName Category;
    FString Text;
};

/** Thread-safe bounded in-memory capture. Never writes another log from Serialize. */
class KHUTUTORTOOLS_API FKHULogCapture final : public FOutputDevice
{
public:
    /** Access the tools module's shared sensor. */
    static FKHULogCapture& Get();
    /** Allocate the ring before registering with GLog. */
    explicit FKHULogCapture(int32 InCapacity = 5000);
    virtual ~FKHULogCapture() override;
    /** Register once; the redirector may replay its existing backlog. */
    void Start();
    /** Unregister before module unload. */
    void Stop();
    /** Capture bounded lines from any logging thread. */
    virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override;
    virtual bool CanBeUsedOnAnyThread() const override { return true; }
    /** Copy a chronological snapshot while holding a short lock. */
    TArray<FKHULogEntry> Snapshot() const;
private:
    mutable FCriticalSection Mutex;
    TArray<FKHULogEntry> Ring;
    int32 Head = 0;
    int32 Count = 0;
    bool bRegistered = false;
};
