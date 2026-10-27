#include "Sensors/KHUProjectSource.h"
#include "Digest/KHULogDigest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Windows/WindowsHWrapper.h"

namespace
{
    struct FReadHandle
    {
        HANDLE Handle = INVALID_HANDLE_VALUE;
        explicit FReadHandle(const FString& Path, bool bDirectory = false)
        { Handle = ::CreateFileW(*Path, bDirectory ? FILE_READ_ATTRIBUTES : GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            bDirectory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL, nullptr); }
        ~FReadHandle() { if (Handle != INVALID_HANDLE_VALUE) { ::CloseHandle(Handle); } }
        FReadHandle(const FReadHandle&) = delete;
        FReadHandle& operator=(const FReadHandle&) = delete;
    };
    FString FinalPath(HANDLE Handle)
    {
        TArray<WCHAR> Buffer; Buffer.SetNumUninitialized(32768);
        const DWORD Length = ::GetFinalPathNameByHandleW(Handle, Buffer.GetData(), Buffer.Num(), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (Length == 0 || Length >= static_cast<DWORD>(Buffer.Num())) { return {}; }
        FString Path(Buffer.GetData());
        if (Path.StartsWith(TEXT("\\\\?\\UNC\\"))) { Path = TEXT("\\\\") + Path.Mid(8); }
        else { Path.RemoveFromStart(TEXT("\\\\?\\")); }
        FPaths::NormalizeFilename(Path); return Path;
    }
    bool ReadHandle(HANDLE Handle, int32 MaxBytes, FString& Text, FString& Issue)
    {
        LARGE_INTEGER Size;
        if (Handle == INVALID_HANDLE_VALUE || !::GetFileSizeEx(Handle, &Size)) { Issue = TEXT("file unavailable"); return false; }
        if (Size.QuadPart < 0 || Size.QuadPart > MaxBytes) { Issue = TEXT("file exceeds read limit"); return false; }
        TArray<uint8> Bytes; Bytes.SetNumUninitialized(static_cast<int32>(Size.QuadPart)); DWORD Read = 0;
        if (!Bytes.IsEmpty() && (!::ReadFile(Handle, Bytes.GetData(), Bytes.Num(), &Read, nullptr) || Read != static_cast<DWORD>(Bytes.Num())))
        { Issue = TEXT("file changed or read failed"); return false; }
        if (Bytes.IsEmpty()) { Text.Reset(); }
        else { FFileHelper::BufferToString(Text, Bytes.GetData(), Bytes.Num()); }
        return true;
    }
}
bool FKHUProjectSource::IsUnderRoot(const FString& Root, const FString& File)
{
    FString R = Root, F = File; FPaths::NormalizeFilename(R); FPaths::NormalizeFilename(F); R.RemoveFromEnd(TEXT("/"));
    return !R.IsEmpty() && F.StartsWith(R + TEXT("/"), ESearchCase::IgnoreCase);
}
bool FKHUProjectSource::Resolve(const FString& ProjectDir, const FString& RelativePath, FString& Absolute)
{
    FString Relative = RelativePath; FPaths::NormalizeFilename(Relative);
    if (Relative.IsEmpty() || Relative.Len() > 512 || !FPaths::IsRelative(Relative) || Relative.Contains(TEXT(":"))
        || Relative.Contains(TEXT("..")) || Relative.Contains(TEXT("\r")) || Relative.Contains(TEXT("\n"))
        || !Relative.StartsWith(TEXT("Source/"), ESearchCase::IgnoreCase)) { return false; }
    const FString Extension = FPaths::GetExtension(Relative).ToLower();
    if (Extension != TEXT("cpp") && Extension != TEXT("h") && Extension != TEXT("hpp") && Extension != TEXT("inl") && Extension != TEXT("c") && Extension != TEXT("cs")) { return false; }
    Absolute = FPaths::ConvertRelativePathToFull(ProjectDir / Relative); FPaths::NormalizeFilename(Absolute);
    return FPaths::CollapseRelativeDirectories(Absolute) && IsUnderRoot(FPaths::ConvertRelativePathToFull(ProjectDir / TEXT("Source")), Absolute);
}
bool FKHUProjectSource::ReadBoundedFile(const FString& Absolute, int32 MaxBytes, FString& Text, FString& Issue)
{
    FReadHandle File(Absolute); return ReadHandle(File.Handle, MaxBytes, Text, Issue);
}
FString FKHUProjectSource::Read(const FString& ProjectDir, const FString& RelativePath, int32 Line, int32 Radius)
{
    FString Absolute;
    if (!Resolve(ProjectDir, RelativePath, Absolute)) { return TEXT("ERROR: path not allowed; use a project-relative Source/ C++ or Build.cs path"); }
    if (Line < 1) { return TEXT("ERROR: Line must be >= 1"); }
    FReadHandle Project(FPaths::ConvertRelativePathToFull(ProjectDir), true), Root(FPaths::ConvertRelativePathToFull(ProjectDir / TEXT("Source")), true), File(Absolute);
    if (Project.Handle == INVALID_HANDLE_VALUE || Root.Handle == INVALID_HANDLE_VALUE || File.Handle == INVALID_HANDLE_VALUE) { return TEXT("ERROR: source file unavailable"); }
    const FString RootPath = FinalPath(Root.Handle), FilePath = FinalPath(File.Handle);
    if (!RootPath.Equals(FinalPath(Project.Handle) / TEXT("Source"), ESearchCase::IgnoreCase)
        || RootPath.IsEmpty() || FilePath.IsEmpty() || !IsUnderRoot(RootPath, FilePath)) { return TEXT("ERROR: path not allowed; resolved file escapes Source/"); }
    FString Text, Issue;
    if (!ReadHandle(File.Handle, 1024 * 1024, Text, Issue)) { return TEXT("ERROR: ") + Issue; }
    Text.ReplaceInline(TEXT("\r\n"), TEXT("\n")); Text.ReplaceInline(TEXT("\r"), TEXT("\n"));
    TArray<FString> Lines; Text.ParseIntoArray(Lines, TEXT("\n"), false);
    if (Lines.IsEmpty() || Line > Lines.Num()) { return TEXT("ERROR: Line is outside the source file"); }
    Radius = FMath::Clamp(Radius, 0, 40);
    const int32 Start = FMath::Max(1, Line - Radius), End = FMath::Min(Lines.Num(), Line + Radius);
    const int32 Budget = FMath::Clamp(5400 / (End - Start + 1) - 12, 32, 1024);
    FString Result = FString::Printf(TEXT("SOURCE %s lines=%d-%d focus=%d total=%d"), *FKHULogDigest::Sanitize(RelativePath), Start, End, Line, Lines.Num());
    for (int32 Index = Start; Index <= End; ++Index)
    {
        FString Content = FKHULogDigest::Sanitize(Lines[Index - 1]);
        if (Content.Len() > Budget) { Content = Content.Left(Budget - 20) + TEXT(" ...[line truncated]"); }
        Result += FString::Printf(TEXT("\n%d| %s"), Index, *Content);
    }
    return FKHULogDigest::Bound(Result);
}
