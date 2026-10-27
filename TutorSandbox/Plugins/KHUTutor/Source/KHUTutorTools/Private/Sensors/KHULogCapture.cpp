#include "Sensors/KHULogCapture.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"

FKHULogCapture& FKHULogCapture::Get() { static FKHULogCapture Sensor; return Sensor; }
FKHULogCapture::FKHULogCapture(int32 InCapacity) { Ring.SetNum(FMath::Clamp(InCapacity, 1, 5000)); }
FKHULogCapture::~FKHULogCapture() { Stop(); }
void FKHULogCapture::Start()
{
    check(IsInGameThread());
    if (!bRegistered && GLog) { bRegistered = true; GLog->AddOutputDevice(this); }
}
void FKHULogCapture::Stop()
{
    if (bRegistered && GLog) { GLog->RemoveOutputDevice(this); }
    bRegistered = false;
}
void FKHULogCapture::Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category)
{
    const auto Severity = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
    if (!Text || Severity == ELogVerbosity::NoLogging || (Verbosity & ELogVerbosity::SetColor)) { return; }
    TArray<FString> Lines;
    FString(Text).Left(32768).Replace(TEXT("\r\n"), TEXT("\n")).Replace(TEXT("\r"), TEXT("\n")).ParseIntoArray(Lines, TEXT("\n"), true);
    FScopeLock Lock(&Mutex);
    const double Now = FPlatformTime::Seconds();
    for (const FString& Line : Lines)
    {
        auto& Entry = Ring[Head];
        Entry.Time = Now; Entry.Verbosity = Severity; Entry.Category = Category; Entry.Text = Line.Left(2048);
        Head = (Head + 1) % Ring.Num(); Count = FMath::Min(Count + 1, Ring.Num());
    }
}
TArray<FKHULogEntry> FKHULogCapture::Snapshot() const
{
    FScopeLock Lock(&Mutex);
    TArray<FKHULogEntry> Result; Result.Reserve(Count);
    const int32 Start = (Head + Ring.Num() - Count) % Ring.Num();
    for (int32 Index = 0; Index < Count; ++Index) { Result.Add(Ring[(Start + Index) % Ring.Num()]); }
    return Result;
}
