#include "AI/KHUToolExecutor.h"
#include "AI/KHUToolBridge.h"
#include "Toolsets/KHUTutorReadToolset.h"
#include "Toolsets/KHUTutorShowToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "Async/Async.h"

TArray<TSharedPtr<FJsonValue>> FKHUToolExecutorDirect::Catalog()
{
    TArray<TSharedPtr<FJsonValue>> Out;
    for (const auto Class : {UKHUTutorReadToolset::StaticClass(), UKHUTutorShowToolset::StaticClass()})
    {
        TSharedPtr<FJsonObject> O; const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
        if (KHUMcpProtocol::ReadObject(UToolsetRegistry::GetToolsetJsonSchema(Class), O) && O->TryGetArrayField(TEXT("tools"), List)) { Out.Append(*List); }
    }
    return Out;
}
void FKHUToolExecutorDirect::Execute(const FString& Native, const TSharedRef<FJsonObject>& Args, FCallback Callback)
{
    const uint64 G = Generation; const TWeakPtr<FKHUToolExecutorDirect> Weak = AsShared();
    AsyncTask(ENamedThreads::GameThread, [Weak, G, Native, Args, Callback = MoveTemp(Callback)]() mutable
    {
        const auto Self = Weak.Pin(); if (!Self || Self->Generation != G) { return; }
        FKHUMcpResult R; bool Valid = false;
        for (const auto& V : Catalog())
        {
            const auto O = V->AsObject(); const TSharedPtr<FJsonObject>* S = nullptr; FString Name;
            if (O->TryGetStringField(TEXT("name"), Name) && Name.Equals(Native, ESearchCase::CaseSensitive) && O->TryGetObjectField(TEXT("inputSchema"), S))
            { Valid = FKHUToolBridge::Validate(*S, MakeShared<FJsonValueObject>(Args)); break; }
        }
        if (!Valid) { R.Error = TEXT("ERROR: invalid owned tool or arguments"); Callback(MoveTemp(R)); return; }
        auto S = [&Args](const TCHAR* K, const TCHAR* D = TEXT("")) { FString V; return Args->TryGetStringField(K, V) ? V : FString(D); };
        auto N = [&Args](const TCHAR* K, int32 D) { double V = 0; return Args->TryGetNumberField(K, V) ? static_cast<int32>(FMath::Clamp(V, -100000.0, 100000.0)) : D; };
        const FString Read = TEXT("KHUTutorTools.KHUTutorReadToolset."), Show = TEXT("KHUTutorTools.KHUTutorShowToolset.");
        if (Native == Read + TEXT("Ping")) { R.Text = UKHUTutorReadToolset::Ping(S(TEXT("message"))); }
        else if (Native == Read + TEXT("GetTutorContext")) { R.Text = UKHUTutorReadToolset::GetTutorContext(); }
        else if (Native == Read + TEXT("GetLogDigest")) { R.Text = UKHUTutorReadToolset::GetLogDigest(S(TEXT("minSeverity"), TEXT("Warning")), N(TEXT("sinceSeconds"), 600), N(TEXT("maxLines"), 40)); }
        else if (Native == Read + TEXT("GetBlueprintCompileErrors")) { R.Text = UKHUTutorReadToolset::GetBlueprintCompileErrors(S(TEXT("assetPath"))); }
        else if (Native == Read + TEXT("GetCppBuildErrors")) { R.Text = UKHUTutorReadToolset::GetCppBuildErrors(N(TEXT("maxErrors"), 10)); }
        else if (Native == Read + TEXT("ReadProjectSource")) { R.Text = UKHUTutorReadToolset::ReadProjectSource(S(TEXT("relativePath")), N(TEXT("line"), 1), N(TEXT("radius"), 20)); }
        else if (Native == Read + TEXT("GetBlueprintGraphDigest")) { R.Text = UKHUTutorReadToolset::GetBlueprintGraphDigest(S(TEXT("assetPath")), S(TEXT("graphName")), S(TEXT("focus"), TEXT("errors")), N(TEXT("maxNodes"), 20), N(TEXT("hops"), 2)); }
        else if (Native == Read + TEXT("GetActorRelations")) { R.Text = UKHUTutorReadToolset::GetActorRelations(S(TEXT("actorPath")), S(TEXT("worldScope"), TEXT("Editor")), N(TEXT("depth"), 1), N(TEXT("maxObjects"), 4)); }
        else if (Native == Read + TEXT("GetObjectDetails")) { R.Text = UKHUTutorReadToolset::GetObjectDetails(S(TEXT("objectPath")), S(TEXT("worldScope"), TEXT("Editor")), S(TEXT("propertyNames")), N(TEXT("maxProperties"), 12)); }
        else if (Native == Show + TEXT("FocusActor")) { R.Text = UKHUTutorShowToolset::FocusActor(S(TEXT("actorLabel"))); }
        else if (Native == Show + TEXT("OpenAssetEditor")) { R.Text = UKHUTutorShowToolset::OpenAssetEditor(S(TEXT("assetPath"))); }
        else if (Native == Show + TEXT("FocusGraphNode")) { R.Text = UKHUTutorShowToolset::FocusGraphNode(S(TEXT("assetPath")), S(TEXT("nodeRef"))); }
        else { R.Text = TEXT("ERROR: tool not allowed by Direct executor"); }
        R.bSuccess = !R.Text.StartsWith(TEXT("ERROR")); if (!R.bSuccess) { R.Error = R.Text; } Callback(MoveTemp(R));
    });
}
void FKHUToolExecutorMcp::Execute(const FString& Native, const TSharedRef<FJsonObject>& Args, FCallback Callback)
{
    if (!Client) { Client = MakeShared<FKHUMcpClient>(Queue, Options); }
    Client->CallTool(Native, KHUMcpProtocol::WriteObject(Args), MoveTemp(Callback));
}
void FKHUToolExecutorMcp::Cancel() { if (Client) { Client->Shutdown(); Client.Reset(); } }
