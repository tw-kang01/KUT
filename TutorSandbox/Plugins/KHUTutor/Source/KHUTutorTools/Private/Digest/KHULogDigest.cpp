#include "Digest/KHULogDigest.h"
#include "Internationalization/Regex.h"

namespace
{
    FString ReplaceMatches(const FString& Text, const FString& Pattern, const FString& Replacement, bool bKeepPrefix = false)
    {
        FRegexMatcher Matcher(FRegexPattern(Pattern), Text);
        FString Result; int32 Position = 0;
        while (Matcher.FindNext())
        {
            Result += Text.Mid(Position, Matcher.GetMatchBeginning() - Position);
            if (bKeepPrefix) { Result += Matcher.GetCaptureGroup(1); }
            Result += Replacement; Position = Matcher.GetMatchEnding();
        }
        Result += Text.Mid(Position); return Result;
    }
    FString Normalize(const FString& Text)
    {
        FString Key = ReplaceMatches(Text, TEXT("(?i)0x[0-9a-f]+"), TEXT("<addr>"));
        Key = ReplaceMatches(Key, TEXT("(?i)[A-Z]:[\\\\/][^\\s\"']+"), TEXT("<path>"));
        return ReplaceMatches(Key, TEXT("\\b[0-9]+(?:\\.[0-9]+)?\\b"), TEXT("<n>"));
    }
    const TCHAR* Tag(ELogVerbosity::Type Severity)
    {
        if (Severity == ELogVerbosity::Fatal) { return TEXT("F"); }
        if (Severity == ELogVerbosity::Error) { return TEXT("E"); }
        if (Severity == ELogVerbosity::Warning) { return TEXT("W"); }
        if (Severity == ELogVerbosity::Display) { return TEXT("D"); }
        return TEXT("L");
    }
    struct FExactKeys : TDefaultMapKeyFuncs<FString, int32, false>
    { static bool Matches(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); } };
    struct FGroup { FString Text; int32 Count = 1; bool bContextOnly = false; };
}
FString FKHULogDigest::Sanitize(const FString& Text)
{
    FString Result = ReplaceMatches(Text, TEXT("(?i)(?:baze_|sk-)[A-Za-z0-9_-]+"), TEXT("<key-redacted>"));
    Result = ReplaceMatches(Result, TEXT("(?i)(?:Authorization[\"']?\\s*[:=]\\s*[\"']?Bearer\\s+|Bearer\\s+|x-api-key[\"']?\\s*[:=]\\s*[\"']?)[A-Za-z0-9_.-]+"), TEXT("<auth-redacted>"));
    Result = ReplaceMatches(Result, TEXT("(?i)([A-Z]:[\\\\/]Users[\\\\/])[^\\\\/\"'\\r\\n]+"), TEXT("***"), true);
    Result.ReplaceInline(TEXT("\r"), TEXT(" ")); Result.ReplaceInline(TEXT("\n"), TEXT(" "));
    return Result;
}
FString FKHULogDigest::Bound(const FString& Text, int32 MaxChars)
{
    MaxChars = FMath::Clamp(MaxChars, 64, 6000);
    if (Text.Len() <= MaxChars) { return Text; }
    FString Suffix = FString::Printf(TEXT("\n...[truncated %d chars]"), Text.Len() - MaxChars);
    Suffix = FString::Printf(TEXT("\n...[truncated %d chars]"), Text.Len() - MaxChars + Suffix.Len());
    return Text.Left(MaxChars - Suffix.Len()) + Suffix;
}
FString FKHULogDigest::Build(const TArray<FKHULogEntry>& Entries, double Now, const FKHULogDigestOptions& Input)
{
    ELogVerbosity::Type Minimum = ELogVerbosity::Warning;
    if (Input.MinSeverity.Equals(TEXT("Error"), ESearchCase::IgnoreCase)) { Minimum = ELogVerbosity::Error; }
    else if (Input.MinSeverity.Equals(TEXT("Fatal"), ESearchCase::IgnoreCase)) { Minimum = ELogVerbosity::Fatal; }
    else if (Input.MinSeverity.Equals(TEXT("Display"), ESearchCase::IgnoreCase)) { Minimum = ELogVerbosity::Display; }
    else if (Input.MinSeverity.Equals(TEXT("Log"), ESearchCase::IgnoreCase)) { Minimum = ELogVerbosity::Log; }
    else if (!Input.MinSeverity.Equals(TEXT("Warning"), ESearchCase::IgnoreCase)) { return TEXT("ERROR: MinSeverity must be Fatal, Error, Warning, Display, or Log"); }
    if (!FMath::IsFinite(Now) || !FMath::IsFinite(Input.SinceSeconds)) { return TEXT("ERROR: invalid time window"); }
    const double Since = FMath::Clamp(Input.SinceSeconds, 0.0, 86400.0);
    const int32 MaxLines = FMath::Clamp(Input.MaxLines, 1, 80);
    TArray<int32> Window; int32 FirstError = INDEX_NONE;
    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
        const auto& Entry = Entries[Index];
        if (Entry.Time < Now - Since || Entry.Time > Now || Entry.Verbosity <= ELogVerbosity::NoLogging) { continue; }
        if (FirstError == INDEX_NONE && Entry.Verbosity <= ELogVerbosity::Error) { FirstError = Index; }
        Window.Add(Index);
    }
    TArray<FGroup> Groups;
    TMap<FString, int32, FDefaultSetAllocator, FExactKeys> Lookup;
    int32 ErrorGroup = INDEX_NONE, Matched = 0, Noise = 0;
    for (int32 Index : Window)
    {
        const auto& Entry = Entries[Index];
        if (Input.NoiseCategories.Contains(Entry.Category) && Entry.Verbosity > ELogVerbosity::Error) { ++Noise; continue; }
        const bool bSeverity = Entry.Verbosity <= Minimum;
        const bool bContext = FirstError != INDEX_NONE && FMath::Abs(Index - FirstError) <= 3;
        if (!bSeverity && !bContext) { continue; }
        ++Matched;
        const FString Safe = Sanitize(Entry.Text);
        const FString Category = Sanitize(Entry.Category.ToString());
        const FString Key = FString::Printf(TEXT("%d|%s|%s"), Entry.Verbosity, *Category, *Normalize(Safe));
        if (int32* Existing = Lookup.Find(Key))
        {
            ++Groups[*Existing].Count;
            Groups[*Existing].bContextOnly &= !bSeverity;
            if (Index == FirstError) { ErrorGroup = *Existing; }
        }
        else
        {
            FGroup Group; Group.Text = FString::Printf(TEXT("%s %s: %s"), Tag(Entry.Verbosity), *Category, *Safe);
            Group.bContextOnly = !bSeverity;
            const int32 Added = Groups.Add(MoveTemp(Group)); Lookup.Add(Key, Added);
            if (Index == FirstError) { ErrorGroup = Added; }
        }
    }
    const int32 Start = Groups.Num() > MaxLines && ErrorGroup != INDEX_NONE ? FMath::Max(0, ErrorGroup - FMath::Min(3, MaxLines - 1)) : 0;
    const int32 End = FMath::Min(Start + MaxLines, Groups.Num());
    FString Result = FString::Printf(TEXT("LOG since=%.0fs min=%s matched=%d groups=%d shown=%d omitted=%d noise=%d"),
        Since, *Input.MinSeverity, Matched, Groups.Num(), End - Start, Groups.Num() - (End - Start), Noise);
    const int32 LineBudget = FMath::Clamp(5600 / FMath::Max(1, End - Start) - 16, 32, 1024);
    for (int32 Index = Start; Index < End; ++Index)
    {
        const auto& Group = Groups[Index];
        Result += TEXT("\n"); if (Group.bContextOnly) { Result += TEXT("CTX "); }
        const FString Suffix = TEXT(" ...[line truncated]");
        Result += Group.Text.Len() > LineBudget ? Group.Text.Left(LineBudget - Suffix.Len()) + Suffix : Group.Text;
        if (Group.Count > 1) { Result += FString::Printf(TEXT(" x%d"), Group.Count); }
    }
    if (Groups.IsEmpty()) { Result += TEXT("\nNO_MATCHING_LOGS"); }
    return Bound(Result);
}
