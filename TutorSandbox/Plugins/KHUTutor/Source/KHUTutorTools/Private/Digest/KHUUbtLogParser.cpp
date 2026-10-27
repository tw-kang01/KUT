#include "Digest/KHUUbtLogParser.h"
#include "Digest/KHULogDigest.h"
#include "Internationalization/Regex.h"
#include "Misc/Paths.h"

namespace
{
    FString NormalPath(FString Path)
    {
        Path.TrimStartAndEndInline(); Path.RemoveFromStart(TEXT("\"")); Path.RemoveFromEnd(TEXT("\""));
        FPaths::NormalizeFilename(Path); FPaths::CollapseRelativeDirectories(Path);
        return Path;
    }
    FString DiagnosticFile(const FString& File, const FString& ProjectDir)
    {
        FString Path = NormalPath(File), Root = NormalPath(FPaths::ConvertRelativePathToFull(ProjectDir));
        Root.RemoveFromEnd(TEXT("/"));
        if (Path.IsEmpty()) { return TEXT("<build>"); }
        if (Path.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase)) { return Path.Mid(Root.Len() + 1); }
        if (FPaths::IsRelative(Path) && !Path.Contains(TEXT("..")) && !Path.Contains(TEXT(":"))
            && (Path.StartsWith(TEXT("Source/")) || Path.StartsWith(TEXT("Plugins/")) || Path.StartsWith(TEXT("Intermediate/")))) { return Path; }
        return TEXT("<external>/") + FPaths::GetCleanFilename(Path);
    }
    bool Match(const FString& Line, const FRegexPattern& Pattern, TArray<FString>& Groups, int32 Count)
    {
        FRegexMatcher Matcher(Pattern, Line);
        if (!Matcher.FindNext()) { return false; }
        Groups.Reset(); for (int32 Index = 1; Index <= Count; ++Index) { Groups.Add(Matcher.GetCaptureGroup(Index)); }
        return true;
    }
}
bool FKHUUbtLogParser::IsProjectBuild(const FString& Text, const FString& ProjectFile)
{
    TArray<FString> Lines; Text.ParseIntoArrayLines(Lines, false);
    FString Command;
    for (const FString& Line : Lines) { if (Line.StartsWith(TEXT("Command line:"))) { Command = Line; } }
    if (Command.IsEmpty() || Command.Contains(TEXT("-Mode="), ESearchCase::IgnoreCase)) { return false; }
    const FRegexPattern Pattern(TEXT("(?i)-Project\\s*=\\s*(?:\"([^\"]+\\.uproject)\"|([^\\r\\n]*?\\.uproject)(?=\\s|$))"));
    FRegexMatcher Matcher(Pattern, Command);
    bool bFound = false;
    while (Matcher.FindNext())
    {
        FString Path = Matcher.GetCaptureGroup(1); if (Path.IsEmpty()) { Path = Matcher.GetCaptureGroup(2); }
        if (FPaths::IsRelative(Path) || !NormalPath(Path).Equals(NormalPath(ProjectFile), ESearchCase::IgnoreCase)) { return false; }
        bFound = true;
    }
    return bFound;
}
FKHUCppBuildSnapshot FKHUUbtLogParser::Parse(const FString& Text, const FString& ProjectDir)
{
    const FRegexPattern Msvc(TEXT("(?i)^\\s*(.*?)\\(([0-9]+)(?:,[0-9]+)?\\)\\s*:\\s*(?:fatal\\s+)?error\\s+(C[0-9]+|LNK[0-9]+)\\s*:\\s*(.*)$"));
    const FRegexPattern Link(TEXT("(?i)^\\s*(.*?)\\s*:\\s*(?:fatal\\s+)?error\\s+(LNK[0-9]+|C[0-9]+)\\s*:\\s*(.*)$"));
    const FRegexPattern Uht(TEXT("(?i)^\\s*(.*?)\\(([0-9]+)(?:,[0-9]+)?\\)\\s*:\\s*error\\s*:\\s*(.*)$"));
    const FRegexPattern Ubt(TEXT("(?i)^\\s*(.*?)\\s*:\\s*error\\s*:\\s*(.*)$"));
    const FRegexPattern Bare(TEXT("(?i)^\\s*error\\s*:\\s*(.*)$"));
    FKHUCppBuildSnapshot Result;
    TArray<FString> Lines; Text.ParseIntoArrayLines(Lines, false);
    for (FString Line : Lines)
    {
        Line.TrimStartAndEndInline();
        if (Line.StartsWith(TEXT("Command line:"))) { Result = {}; Result.Status = TEXT("running"); continue; }
        if (Line.StartsWith(TEXT("Result: Succeeded")) || Line.Equals(TEXT("Result: UpToDate"))) { Result.Status = TEXT("ok"); continue; }
        if (Line.StartsWith(TEXT("Result: Failed")) || Line.StartsWith(TEXT("Result: CompilationResult"))) { Result.Status = TEXT("failed"); continue; }
        if (Line.StartsWith(TEXT("Result: Cancel"))) { Result.Status = TEXT("cancelled"); continue; }
        // Bound regex work independently of the total file bound.
        if (Line.Len() > 16384) { Result.bDiagnosticsTruncated = true; continue; }
        FKHUCppDiagnostic Error; TArray<FString> Groups; FString File;
        if (Match(Line, Msvc, Groups, 4)) { File = Groups[0]; Error.Line = FCString::Atoi(*Groups[1]); Error.Code = Groups[2].ToUpper(); Error.Message = Groups[3]; }
        else if (Match(Line, Link, Groups, 3)) { File = Groups[0]; Error.Code = Groups[1].ToUpper(); Error.Message = Groups[2]; }
        else if (Match(Line, Uht, Groups, 3)) { File = Groups[0]; Error.Line = FCString::Atoi(*Groups[1]); Error.Code = TEXT("UHT"); Error.Message = Groups[2]; }
        else if (Match(Line, Bare, Groups, 1)) { Error.Code = TEXT("UBT"); Error.Message = Groups[0]; }
        else if (Match(Line, Ubt, Groups, 2)) { File = Groups[0]; Error.Code = TEXT("UBT"); Error.Message = Groups[1]; }
        else { continue; }
        Error.File = DiagnosticFile(File, ProjectDir);
        Error.Message = FKHULogDigest::Sanitize(Error.Message).Left(2048);
        Error.File = FKHULogDigest::Sanitize(Error.File);
        bool bDuplicate = false;
        for (const auto& Existing : Result.Errors)
        {
            if (Existing.Line == Error.Line && Existing.File.Equals(Error.File, ESearchCase::CaseSensitive)
                && Existing.Code == Error.Code && Existing.Message.Equals(Error.Message, ESearchCase::CaseSensitive)) { bDuplicate = true; break; }
        }
        if (!bDuplicate)
        {
            if (Result.Errors.Num() < 100) { Result.Errors.Add(MoveTemp(Error)); }
            else { Result.bDiagnosticsTruncated = true; }
        }
        Result.Status = TEXT("failed");
    }
    // Inconsistent success + errors must never become a successful build.
    if (!Result.Errors.IsEmpty() && Result.Status == TEXT("ok")) { Result.Status = TEXT("failed"); }
    return Result;
}
FString FKHUUbtLogParser::Describe(const FKHUCppBuildSnapshot& Snapshot, int32 MaxErrors)
{
    const int32 Count = FMath::Min(FMath::Clamp(MaxErrors, 1, 50), Snapshot.Errors.Num());
    FString Text = FString::Printf(TEXT("CPP %s kind=%s errors=%d shown=%d omitted=%d time=%s log=%s"),
        *Snapshot.Status, *Snapshot.Kind, Snapshot.Errors.Num(), Count, Snapshot.Errors.Num() - Count,
        Snapshot.Time == FDateTime() ? TEXT("unknown") : *Snapshot.Time.ToIso8601(), *FKHULogDigest::Sanitize(Snapshot.Log));
    const int32 Budget = FMath::Clamp(5400 / FMath::Max(Count, 1) - 20, 48, 1600);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const auto& Error = Snapshot.Errors[Index];
        FString Message = Error.Message;
        if (Error.Code == TEXT("LNK2019")) { Message += TEXT(" (hint: check module dependencies in Build.cs and matching function definitions)"); }
        FString Row = FString::Printf(TEXT("E%d %s:%d %s %s"), Index + 1, *Error.File, Error.Line, *Error.Code, *Message);
        if (Row.Len() > Budget) { Row = Row.Left(Budget - 20) + TEXT(" ...[line truncated]"); }
        Text += TEXT("\n") + Row;
    }
    if (Snapshot.bDiagnosticsTruncated) { Text += TEXT("\n...[diagnostics truncated]"); }
    if (Snapshot.Errors.IsEmpty()) { Text += Snapshot.Status == TEXT("failed") ? TEXT("\nNO_DIAGNOSTICS: build failed; inspect the build log") : TEXT("\nNO_COMPILER_ERRORS"); }
    return FKHULogDigest::Bound(Text);
}
