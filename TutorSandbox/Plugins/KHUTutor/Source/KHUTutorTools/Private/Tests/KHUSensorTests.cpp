#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sensors/KHULogCapture.h"
#include "Sensors/KHUCompileWatcher.h"
#include "Digest/KHULogDigest.h"
#include "Toolsets/KHUTutorReadToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/UObjectGlobals.h"
#include "Async/Async.h"
#include "HAL/PlatformTime.h"
#include <atomic>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FKHULogEntry Entry(const FString& Text, ELogVerbosity::Type Severity = ELogVerbosity::Error, double Time = 1000)
    { FKHULogEntry E; E.Text = Text; E.Verbosity = Severity; E.Time = Time; E.Category = TEXT("LogTemp"); return E; }
    FKHULogDigestOptions Options()
    {
        FKHULogDigestOptions O; O.NoiseCategories = {FName(TEXT("LogSlate")), FName(TEXT("LogRenderer"))}; return O;
    }
    struct FConcurrentState
    {
        TSharedRef<FKHULogCapture, ESPMode::ThreadSafe> Sensor = MakeShared<FKHULogCapture, ESPMode::ThreadSafe>();
        std::atomic<int32> Remaining{4};
        double Deadline = FPlatformTime::Seconds() + 10;
    };
    class FConcurrentCheck final : public IAutomationLatentCommand
    {
    public:
        FConcurrentCheck(FAutomationTestBase* InTest, TSharedRef<FConcurrentState, ESPMode::ThreadSafe> InState) : Test(InTest), State(InState) {}
        virtual bool Update() override
        {
            if (State->Remaining.load() != 0 && FPlatformTime::Seconds() < State->Deadline) { return false; }
            if (State->Remaining.load() != 0) { Test->AddError(TEXT("Concurrent log writers did not finish")); return true; }
            const auto Lines = State->Sensor->Snapshot(); Test->TestEqual(TEXT("All concurrent writes captured"), Lines.Num(), 128);
            TSet<FString> Texts; double Previous = 0;
            for (const auto& Line : Lines) { Texts.Add(Line.Text); Test->TestTrue(TEXT("Snapshot is chronological"), Line.Time >= Previous); Previous = Line.Time; }
            Test->TestEqual(TEXT("No overwritten/duplicated writes"), Texts.Num(), 128); return true;
        }
    private:
        FAutomationTestBase* Test;
        TSharedRef<FConcurrentState, ESPMode::ThreadSafe> State;
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHULogFixturesTest, "KHUTutor.Logs.Fixtures", Flags)
bool FKHULogFixturesTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../fixtures/logs"));
    for (const FString Name : {FString(TEXT("bp_door")), FString(TEXT("accessed_none")), FString(TEXT("mixed_noise"))})
    {
        FString Input, Expected;
        if (!FFileHelper::LoadFileToString(Input, *(Directory / (Name + TEXT(".txt"))))
            || !FFileHelper::LoadFileToString(Expected, *(Directory / (Name + TEXT(".expected.txt")))))
        { AddError(TEXT("Missing D3 fixture: ") + Name); continue; }
        TArray<FString> Rows; Input.Replace(TEXT("\r\n"), TEXT("\n")).ParseIntoArray(Rows, TEXT("\n"), true);
        TArray<FKHULogEntry> Entries;
        for (const FString& Row : Rows)
        {
            TArray<FString> Fields; Row.ParseIntoArray(Fields, TEXT("|"), false);
            if (Fields.Num() != 4) { AddError(TEXT("Invalid fixture row")); continue; }
            auto E = Entry(Fields[3], ELogVerbosity::Log, FCString::Atod(*Fields[0])); E.Category = FName(*Fields[2]);
            if (Fields[1] == TEXT("Error")) { E.Verbosity = ELogVerbosity::Error; }
            else if (Fields[1] == TEXT("Warning")) { E.Verbosity = ELogVerbosity::Warning; }
            else if (Fields[1] == TEXT("Display")) { E.Verbosity = ELogVerbosity::Display; }
            Entries.Add(MoveTemp(E));
        }
        TestEqual(*Name, FKHULogDigest::Build(Entries, 1000, Options()), Expected.Replace(TEXT("\r\n"), TEXT("\n")).TrimEnd());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHULogPrivacyTest, "KHUTutor.Logs.PrivacyAndBounds", Flags)
bool FKHULogPrivacyTest::RunTest(const FString& Parameters)
{
    const FString Safe = FKHULogDigest::Sanitize(TEXT("C:\\Users\\Student\\file C:/Users/Kim/code baze_FAKE_ONLY sk-FAKE_ONLY Authorization: Bearer FAKE_JWT x-api-key=FAKE_HEADER"));
    for (const FString Secret : {FString(TEXT("Student")), FString(TEXT("Kim")), FString(TEXT("FAKE_ONLY")), FString(TEXT("FAKE_JWT")), FString(TEXT("FAKE_HEADER"))})
    { TestFalse(*Secret, Safe.Contains(Secret)); }
    TestTrue(TEXT("Profile masked"), Safe.Contains(TEXT("Users\\***")) && Safe.Contains(TEXT("Users/***")));
    const FString Other = FKHULogDigest::Sanitize(TEXT("D:/Users/John Smith/file {\"x-api-key\":\"FAKE_JSON_ONLY\"}"));
    TestFalse(TEXT("Profile names containing spaces masked"), Other.Contains(TEXT("Smith")));
    TestFalse(TEXT("JSON-quoted authentication masked"), Other.Contains(TEXT("FAKE_JSON_ONLY")));
    const FString Bounded = FKHULogDigest::Bound(FString::ChrN(9000, TEXT('x')));
    TestTrue(TEXT("6000-character bound with visible marker"), Bounded.Len() <= 6000 && Bounded.Contains(TEXT("...[truncated ")));
    TestEqual(TEXT("Short text unchanged"), FKHULogDigest::Bound(TEXT("short")), FString(TEXT("short"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHULogLimitTest, "KHUTutor.Logs.FirstErrorAndLimits", Flags)
bool FKHULogLimitTest::RunTest(const FString& Parameters)
{
    TArray<FKHULogEntry> Entries;
    for (int32 Index = 0; Index < 8; ++Index) { Entries.Add(Entry(FString::Printf(TEXT("Before_%c "), TEXT('A') + Index) + FString::ChrN(2048, TEXT('x')), ELogVerbosity::Display)); }
    Entries.Add(Entry(TEXT("FIRST_ERROR_MARKER")));
    for (int32 Index = 0; Index < 80; ++Index) { Entries.Add(Entry(FString::Printf(TEXT("After_%c "), TEXT('A') + Index) + FString::ChrN(2048, TEXT('y')), ELogVerbosity::Warning)); }
    auto O = Options(); O.MaxLines = 1;
    FString Text = FKHULogDigest::Build(Entries, 1000, O);
    TestTrue(TEXT("First error survives MaxLines one"), Text.Contains(TEXT("FIRST_ERROR_MARKER")) && Text.Contains(TEXT("shown=1")));
    O.MaxLines = 80; Text = FKHULogDigest::Build(Entries, 1000, O);
    TestTrue(TEXT("First error survives huge context and character limit"), Text.Len() <= 6000 && Text.Contains(TEXT("FIRST_ERROR_MARKER")));
    O.MinSeverity = TEXT("invalid"); TestTrue(TEXT("Invalid severity explicit"), FKHULogDigest::Build(Entries, 1000, O).StartsWith(TEXT("ERROR:")));
    O = Options(); O.SinceSeconds = std::numeric_limits<double>::quiet_NaN();
    TestTrue(TEXT("Invalid window explicit"), FKHULogDigest::Build(Entries, 1000, O).StartsWith(TEXT("ERROR:")));
    TestTrue(TEXT("Empty log explicit"), FKHULogDigest::Build({}, 1000, Options()).Contains(TEXT("NO_MATCHING_LOGS"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHURingTest, "KHUTutor.Sensors.RingBuffer", Flags)
bool FKHURingTest::RunTest(const FString& Parameters)
{
    FKHULogCapture Sensor;
    for (int32 Index = 0; Index < 5100; ++Index) { Sensor.Serialize(*FString::Printf(TEXT("Line %d"), Index), ELogVerbosity::Log, TEXT("LogTest")); }
    auto Lines = Sensor.Snapshot(); TestEqual(TEXT("Bounded to 5000 lines"), Lines.Num(), 5000);
    TestEqual(TEXT("Oldest overwritten"), Lines[0].Text, FString(TEXT("Line 100")));
    TestEqual(TEXT("Newest preserved"), Lines.Last().Text, FString(TEXT("Line 5099")));
    FKHULogCapture Small(3); Small.Serialize(TEXT("a\r\nb\nc\rd"), ELogVerbosity::Warning, TEXT("LogTest"));
    Lines = Small.Snapshot(); TestEqual(TEXT("Multiline split and wrap"), Lines.Num(), 3);
    TestEqual(TEXT("First retained line"), Lines[0].Text, FString(TEXT("b")));
    Small.Serialize(TEXT("color"), ELogVerbosity::SetColor, TEXT("LogTest"));
    TestEqual(TEXT("Color command ignored"), Small.Snapshot().Last().Text, FString(TEXT("d")));
    Small.Serialize(*FString::ChrN(9000, TEXT('x')), ELogVerbosity::Log, TEXT("LogTest"));
    TestEqual(TEXT("Individual input line bounded"), Small.Snapshot().Last().Text.Len(), 2048); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUConcurrentCaptureTest, "KHUTutor.Sensors.ConcurrentCapture", Flags)
bool FKHUConcurrentCaptureTest::RunTest(const FString& Parameters)
{
    auto State = MakeShared<FConcurrentState, ESPMode::ThreadSafe>();
    for (int32 Writer = 0; Writer < 4; ++Writer)
    {
        Async(EAsyncExecution::ThreadPool, [State, Writer]()
        {
            for (int32 Index = 0; Index < 32; ++Index) { State->Sensor->Serialize(*FString::Printf(TEXT("Writer_%d_%d"), Writer, Index), ELogVerbosity::Log, TEXT("LogTest")); }
            State->Remaining.fetch_sub(1);
        });
    }
    AddCommand(new FConcurrentCheck(this, State)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUBpEventTest, "KHUTutor.Compile.BlueprintEvents", Flags)
bool FKHUBpEventTest::RunTest(const FString& Parameters)
{
    if (!GEditor) { AddError(TEXT("Editor required")); return false; }
    const auto Previous = FKHUBuildState::Get();
    auto& Watcher = FKHUCompileWatcher::Get(); Watcher.Stop(); Watcher.Start();
    auto* Blueprint = NewObject<UBlueprint>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("KHUTestTransientBP")));
    auto* Graph = NewObject<UEdGraph>(Blueprint, TEXT("EventGraph")); Blueprint->UbergraphPages.Add(Graph);
    auto* Error = NewObject<UEdGraphNode>(Graph); Error->NodeGuid = FGuid(0xAABBCCDD, 1, 2, 3); Error->bHasCompilerMessage = true;
    Error->ErrorType = EMessageSeverity::Error; Error->ErrorMsg = TEXT("Target must have a connection C:/Users/Student/File");
    auto* Warning = NewObject<UEdGraphNode>(Graph); Warning->NodeGuid = FGuid(0x11223344, 1, 2, 3); Warning->bHasCompilerMessage = true;
    Warning->ErrorType = EMessageSeverity::Warning; Warning->ErrorMsg = TEXT("Unused node");
    Graph->Nodes.Add(Error); Graph->Nodes.Add(Warning); Blueprint->Status = BS_Error;
    GEditor->BroadcastBlueprintPreCompile(Blueprint); Blueprint->BroadcastCompiled();
    const auto& State = FKHUBuildState::Get().LastBpCompile.GetValue();
    TestEqual(TEXT("Exact Blueprint observed"), State.AssetPath, Blueprint->GetPathName());
    TestEqual(TEXT("One error"), State.Errors, 1); TestEqual(TEXT("One warning"), State.Warnings, 1);
    const FString Text = FKHUCompileWatcher::DescribeBlueprint(Blueprint);
    TestTrue(TEXT("Status and stable D5 numbers"), Text.Contains(TEXT("ERROR 1, WARN 1")) && Text.Contains(TEXT("E #2")) && Text.Contains(TEXT("W #1")));
    TestFalse(TEXT("Diagnostic user path masked"), Text.Contains(TEXT("Student")));
    TestTrue(TEXT("No GUID in model diagnostics"), !Text.Contains(TEXT("AABBCCDD")) && Text.Contains(TEXT("ref=EventGraph:#2")));
    Watcher.Stop(); Blueprint->Status = BS_UpToDate; Blueprint->BroadcastCompiled();
    TestEqual(TEXT("Delegate removed on Stop"), FKHUBuildState::Get().LastBpCompile.GetValue().Status, FString(TEXT("ERROR")));
    Watcher.Start(); FKHUBuildState::Get() = Previous; return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUD3SchemaTest, "KHUTutor.Tools.D3ReadSchema", Flags)
bool FKHUD3SchemaTest::RunTest(const FString& Parameters)
{
    const FString Schema = UToolsetRegistry::GetToolsetJsonSchema(UKHUTutorReadToolset::StaticClass());
    for (const FString Name : {FString(TEXT("GetTutorContext")), FString(TEXT("GetLogDigest")), FString(TEXT("GetBlueprintCompileErrors"))})
    { TestTrue(*Name, Schema.Contains(TEXT("KHUTutorTools.KHUTutorReadToolset.") + Name)); }
    for (const FString Field : {FString(TEXT("\"minSeverity\"")), FString(TEXT("\"sinceSeconds\"")), FString(TEXT("\"maxLines\"")), FString(TEXT("\"assetPath\""))})
    { TestTrue(*Field, Schema.Contains(Field)); }
    TestTrue(TEXT("Invalid asset rejected"), UKHUTutorReadToolset::GetBlueprintCompileErrors(TEXT("../Other")).StartsWith(TEXT("ERROR:")));
    TestTrue(TEXT("Unloaded asset explicit"), UKHUTutorReadToolset::GetBlueprintCompileErrors(TEXT("/Game/NoSuchBlueprint")).StartsWith(TEXT("ERROR:")));
    const FString Context = UKHUTutorReadToolset::GetTutorContext();
    TestTrue(TEXT("Current project and observed C++ status"), Context.Contains(TEXT("PROJECT TutorSandbox")) && Context.Contains(TEXT("LAST_CPP_BUILD ")));
    const int32 RealtimeAt = Context.Find(TEXT("| VIEWPORT_REALTIME "));
    TestTrue(TEXT("Viewport realtime state before the selection marker"), RealtimeAt != INDEX_NONE && RealtimeAt < Context.Find(TEXT("\nSELECTED_ACTORS ")));
    bool bKnownRealtime = false;
    for (const TCHAR* Value : {TEXT("| VIEWPORT_REALTIME on |"), TEXT("| VIEWPORT_REALTIME off |"), TEXT("| VIEWPORT_REALTIME none |"), TEXT("| VIEWPORT_REALTIME on(override) |"), TEXT("| VIEWPORT_REALTIME off(override) |")})
    { bKnownRealtime |= Context.Contains(Value); }
    TestTrue(TEXT("Viewport realtime value is one of the documented states"), bKnownRealtime);
    TestTrue(TEXT("Context bounded"), Context.Len() <= 6000); return true;
}
#endif
