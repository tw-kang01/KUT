#include "Digest/KHUUbtLogParser.h"
#include "Sensors/KHUCppBuildWatcher.h"
#include "Sensors/KHUProjectSource.h"
#include "Toolsets/KHUTutorReadToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const FString Project = TEXT("C:/KHU/TutorSandbox/TutorSandbox.uproject");
    FString Log(const FString& Body, const FString& File = Project)
    { return TEXT("Command line: UnrealBuildTool.dll TutorSandboxEditor Win64 Development -Project=\"") + File + TEXT("\"\n") + Body; }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUUbtFixturesTest, "KHUTutor.Cpp.Fixtures", Flags)
bool FKHUUbtFixturesTest::RunTest(const FString& Parameters)
{
    const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../fixtures/logs"));
    struct FCase { const TCHAR* Name; const TCHAR* Code; int32 Count; int32 Line; };
    for (const auto& Case : {FCase{TEXT("c2065"), TEXT("C2065"), 1, 42}, FCase{TEXT("lnk2019"), TEXT("LNK2019"), 2, 0},
        FCase{TEXT("c1083"), TEXT("C1083"), 1, 7}, FCase{TEXT("generated"), TEXT("UHT"), 2, 12}, FCase{TEXT("c4430"), TEXT("C4430"), 1, 15}})
    {
        FString Text;
        if (!TestTrue(TEXT("Fixture read"), FFileHelper::LoadFileToString(Text, *(Dir / (TEXT("ubt_") + FString(Case.Name) + TEXT(".txt")))))) { continue; }
        TestTrue(TEXT("Fixture project attribution"), FKHUUbtLogParser::IsProjectBuild(Text, Project));
        const auto Parsed = FKHUUbtLogParser::Parse(Text, TEXT("C:/KHU/TutorSandbox"));
        TestEqual(TEXT("Actual error count, duplicates and warnings excluded"), Parsed.Errors.Num(), Case.Count);
        TestEqual(TEXT("Failed outcome"), Parsed.Status, FString(TEXT("failed")));
        if (!Parsed.Errors.IsEmpty())
        {
            TestEqual(TEXT("First error code"), Parsed.Errors[0].Code, FString(Case.Code));
            TestEqual(TEXT("Line and optional column"), Parsed.Errors[0].Line, Case.Line);
            TestFalse(TEXT("File is not absolute"), Parsed.Errors[0].File.Contains(TEXT("C:")));
        }
        if (FString(Case.Code) == TEXT("LNK2019")) { TestTrue(TEXT("Linker hint"), FKHUUbtLogParser::Describe(Parsed, 10).Contains(TEXT("Build.cs"))); }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUUbtOutcomesTest, "KHUTutor.Cpp.OutcomesAndBounds", Flags)
bool FKHUUbtOutcomesTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Running is not success"), FKHUUbtLogParser::Parse(Log(TEXT("Building...")), TEXT("C:/KHU/TutorSandbox")).Status, FString(TEXT("running")));
    TestEqual(TEXT("Success explicit"), FKHUUbtLogParser::Parse(Log(TEXT("Result: Succeeded")), TEXT("C:/KHU/TutorSandbox")).Status, FString(TEXT("ok")));
    const auto Failed = FKHUUbtLogParser::Parse(Log(TEXT("Result: Failed (Unknown)")), TEXT("C:/KHU/TutorSandbox"));
    TestTrue(TEXT("Failure without diagnostics remains failure"), FKHUUbtLogParser::Describe(Failed, 10).Contains(TEXT("NO_DIAGNOSTICS")));
    const auto Reset = FKHUUbtLogParser::Parse(Log(TEXT("Error: old failure")) + TEXT("\n") + Log(TEXT("Building...")), TEXT("C:/KHU/TutorSandbox"));
    TestTrue(TEXT("New command resets old failure"), Reset.Status == TEXT("running") && Reset.Errors.IsEmpty());
    FString Many = Log(TEXT(""));
    for (int32 Index = 0; Index < 110; ++Index) { Many += FString::Printf(TEXT("Source/Test.cpp(%d): error C2065: item %d %s\n"), Index + 1, Index, *FString::ChrN(2500, TEXT('x'))); }
    auto Parsed = FKHUUbtLogParser::Parse(Many, TEXT("C:/KHU/TutorSandbox"));
    TestEqual(TEXT("Storage capped"), Parsed.Errors.Num(), 100);
    TestTrue(TEXT("Overflow visible, text bounded"), Parsed.bDiagnosticsTruncated && FKHUUbtLogParser::Describe(Parsed, 50).Len() <= 6000);
    TestTrue(TEXT("MaxErrors and omitted count"), FKHUUbtLogParser::Describe(Parsed, 1).Contains(TEXT("shown=1 omitted=99")));
    Parsed = FKHUUbtLogParser::Parse(Log(TEXT("C:/Other/Engine.h(1): error C2065: baze_FAKE_ONLY C:\\Users\\Student\\path\nResult: Succeeded")), TEXT("C:/KHU/TutorSandbox"));
    TestTrue(TEXT("No false success and external path bounded"), Parsed.Status == TEXT("failed") && Parsed.Errors[0].File == TEXT("<external>/Engine.h"));
    const FString Safe = FKHUUbtLogParser::Describe(Parsed, 10);
    TestFalse(TEXT("Key masked"), Safe.Contains(TEXT("baze_FAKE_ONLY"))); TestFalse(TEXT("User masked"), Safe.Contains(TEXT("Student")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUUbtProjectTest, "KHUTutor.Cpp.ProjectIsolation", Flags)
bool FKHUUbtProjectTest::RunTest(const FString& Parameters)
{
    const FDateTime Time(2026, 10, 2, 12, 0, 0); TOptional<FKHUCppBuildSnapshot> State;
    TestTrue(TEXT("Own build applied"), FKHUCppBuildWatcher::ApplyLog(State, Log(TEXT("Result: Succeeded")), Project, TEXT("own"), Time));
    TestFalse(TEXT("Other project ignored"), FKHUCppBuildWatcher::ApplyLog(State, Log(TEXT("Error: other"), TEXT("C:/Other/TutorSandbox.uproject")), Project, TEXT("shared"), Time));
    TestFalse(TEXT("SDK check is not a build"), FKHUUbtLogParser::IsProjectBuild(Log(TEXT("")) + TEXT("\nCommand line: UBT -Mode=ValidatePlatforms -Project=") + Project, Project));
    TestFalse(TEXT("No command attribution"), FKHUUbtLogParser::IsProjectBuild(TEXT("Source/Test.cpp(1): error C2065: no marker"), Project));
    TestFalse(TEXT("Project marker inside Log option does not count"), FKHUUbtLogParser::IsProjectBuild(TEXT("Command line: UBT -Log=") + Project, Project));
    TestFalse(TEXT("Conflicting project arguments rejected"), FKHUUbtLogParser::IsProjectBuild(TEXT("Command line: UBT -Project=") + Project + TEXT(" -Project=C:/Other/Test.uproject"), Project));
    TestTrue(TEXT("Unquoted spaces in project path supported"), FKHUUbtLogParser::IsProjectBuild(TEXT("Command line: UBT -Project=C:\\KHU Folder\\TutorSandbox.uproject -WaitMutex"), TEXT("C:/KHU Folder/TutorSandbox.uproject")));
    TestFalse(TEXT("Older result cannot overwrite"), FKHUCppBuildWatcher::ApplyLog(State, Log(TEXT("Error: old")), Project, TEXT("old"), Time - FTimespan::FromSeconds(10)));
    TestEqual(TEXT("State preserved"), State.GetValue().Status, FString(TEXT("ok"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHULiveOutcomeTest, "KHUTutor.Cpp.LiveCodingState", Flags)
bool FKHULiveOutcomeTest::RunTest(const FString& Parameters)
{
    const FDateTime Time(2026, 10, 2, 12, 0, 0); TOptional<FKHUCppBuildSnapshot> State;
    FKHUCppBuildWatcher::ApplyLog(State, Log(TEXT("Source/Test.cpp(1): error C2065: Foo\nResult: Failed (CompileError)")), Project, TEXT("own"), Time);
    TestTrue(TEXT("Live failure observed"), FKHUCppBuildWatcher::ApplyLiveMessage(State, TEXT("Live coding failed, please see Live console for more information"), Time + FTimespan::FromSeconds(1)));
    TestEqual(TEXT("Recent project diagnostics retained"), State.GetValue().Errors.Num(), 1);
    FKHUCppBuildWatcher::ApplyLiveMessage(State, TEXT("Starting Live Coding compile."), Time + FTimespan::FromSeconds(2));
    TestTrue(TEXT("New compile clears old diagnostics"), State.GetValue().Status == TEXT("running") && State.GetValue().Errors.IsEmpty());
    FKHUCppBuildWatcher::ApplyLiveMessage(State, TEXT("Live coding succeeded, no code changes detected"), Time + FTimespan::FromSeconds(3));
    TestTrue(TEXT("Success tagged as LiveCoding"), State.GetValue().Status == TEXT("ok") && State.GetValue().Kind == TEXT("LiveCoding"));
    TestFalse(TEXT("Unrelated log is not a completion"), FKHUCppBuildWatcher::ApplyLiveMessage(State, TEXT("Starting LiveCoding"), Time));
    TestFalse(TEXT("Older live result ignored"), FKHUCppBuildWatcher::ApplyLiveMessage(State, TEXT("Live coding failed"), Time)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUSourcePathTest, "KHUTutor.Source.PathBoundary", Flags)
bool FKHUSourcePathTest::RunTest(const FString& Parameters)
{
    FString Absolute;
    for (const FString Path : {FString(TEXT("../Source/Test.cpp")), FString(TEXT("Source/../Config/Test.cpp")), FString(TEXT("C:/KHU/Source/Test.cpp")),
        FString(TEXT("\\\\server\\Source\\Test.cpp")), FString(TEXT("Source/Test.cpp:stream")), FString(TEXT("Config/Test.cpp")),
        FString(TEXT("Plugins/X/Source/Test.cpp")), FString(TEXT("Source/secret.json")), FString(TEXT("Source/Test.cpp\n"))})
    { TestFalse(*Path, FKHUProjectSource::Resolve(TEXT("C:/KHU/TutorSandbox"), Path, Absolute)); }
    TestTrue(TEXT("Unicode/spaces accepted inside Source"), FKHUProjectSource::Resolve(TEXT("C:/KHU/TutorSandbox"), TEXT("Source/한글 폴더/Test.cpp"), Absolute));
    TestFalse(TEXT("Canonical sibling prefix denied"), FKHUProjectSource::IsUnderRoot(TEXT("C:/Root/Source"), TEXT("C:/Root/SourceBackup/Test.cpp")));
    TestFalse(TEXT("Canonical link outside denied"), FKHUProjectSource::IsUnderRoot(TEXT("C:/Root/Source"), TEXT("C:/Outside/Test.cpp")));
    const FString Schema = UToolsetRegistry::GetToolsetJsonSchema(UKHUTutorReadToolset::StaticClass());
    for (const FString Name : {FString(TEXT("GetCppBuildErrors")), FString(TEXT("ReadProjectSource")), FString(TEXT("\"maxErrors\"")),
        FString(TEXT("\"relativePath\"")), FString(TEXT("\"line\"")), FString(TEXT("\"radius\""))}) { TestTrue(*Name, Schema.Contains(Name)); }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUSourceExcerptTest, "KHUTutor.Source.ExcerptAndBounds", Flags)
bool FKHUSourceExcerptTest::RunTest(const FString& Parameters)
{
    const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("KHUTutor/D4_Source_Test"));
    IFileManager::Get().MakeDirectory(*(Root / TEXT("Source/한글 폴더")), true);
    TestTrue(TEXT("Real junction fixture exists (use scripts/test.ps1)"), IFileManager::Get().FileExists(*(Root / TEXT("Source/D4_Link/Secret.cpp"))));
    const FString Escape = FKHUProjectSource::Read(Root, TEXT("Source/D4_Link/Secret.cpp"), 1, 1);
    TestTrue(TEXT("Real junction outside Source rejected"), Escape.Contains(TEXT("resolved file escapes Source")));
    TestFalse(TEXT("Outside content never returned"), Escape.Contains(TEXT("KHU_D4_BOUNDARY_SENTINEL")));
    const FString Relative = TEXT("Source/한글 폴더/Test.cpp");
    TestTrue(TEXT("UTF-8 fixture written"), FFileHelper::SaveStringToFile(TEXT("// 한글\n\nbaze_FAKE_ONLY\nFOCUS\nlast\n"), *(Root / Relative), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    const FString Excerpt = FKHUProjectSource::Read(Root, Relative, 4, 2);
    TestTrue(TEXT("Focus, blank lines and numbered lines preserved"), Excerpt.Contains(TEXT("\n2| \n")) && Excerpt.Contains(TEXT("\n4| FOCUS")));
    TestFalse(TEXT("Source key masked"), Excerpt.Contains(TEXT("baze_FAKE_ONLY")));
    TestTrue(TEXT("Radius zero"), FKHUProjectSource::Read(Root, Relative, 1, 0).Contains(TEXT("1| // 한글")));
    TestTrue(TEXT("Missing/out-of-range explicit"), FKHUProjectSource::Read(Root, Relative, 100, 20).StartsWith(TEXT("ERROR:")));
    TestTrue(TEXT("Line zero explicit"), FKHUProjectSource::Read(Root, Relative, 0, 20).StartsWith(TEXT("ERROR:")));
    FString Large;
    for (int32 Index = 0; Index < 100; ++Index) { Large += FString::ChrN(2000, TEXT('x')) + TEXT("\n"); }
    FFileHelper::SaveStringToFile(Large, *(Root / Relative), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    const FString Bounded = FKHUProjectSource::Read(Root, Relative, 50, 40);
    TestTrue(TEXT("Focus survives bounded long context"), Bounded.Len() <= 6000 && Bounded.Contains(TEXT("\n50| ")) && Bounded.Contains(TEXT("line truncated")));
    FFileHelper::SaveStringToFile(FString::ChrN(1024 * 1024 + 1, TEXT('x')), *(Root / Relative), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TestTrue(TEXT("Oversized file refused"), FKHUProjectSource::Read(Root, Relative, 1, 20).Contains(TEXT("read limit")));
    IFileManager::Get().Delete(*(Root / Relative), false, true); return true;
}
#endif
