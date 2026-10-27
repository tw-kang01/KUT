#include "UI/KHUPanelMock.h"
#include "Async/Async.h"
#include "Mcp/KHUMcpProtocol.h"

void FKHUPanelMock::Send(FKHUChatRequest Request, FCallback Callback)
{
    FString LastRole, LastContent, Note;
    if (!Request.Messages.IsEmpty())
    {
        const auto Last = Request.Messages.Last()->AsObject();
        Last->TryGetStringField(TEXT("role"), LastRole); Last->TryGetStringField(TEXT("content"), LastContent);
    }
    for (const auto& V : Request.Messages)
    { FString Role, Text; V->AsObject()->TryGetStringField(TEXT("role"), Role); V->AsObject()->TryGetStringField(TEXT("content"), Text); if (Role == TEXT("system") && Text.Contains(TEXT("HINT LEVEL:"))) { Note = Text; } }
    FKHUChatResponse R; R.bSuccess = R.bMock = true; R.Assistant = MakeShared<FJsonObject>(); R.Assistant->SetStringField(TEXT("role"), TEXT("assistant"));
    if (LastRole != TEXT("tool"))
    {
        FKHUToolCall Call; Call.Id = TEXT("panel_mock_read_") + FString::FromInt(Request.Messages.Num());
        Call.Name = TEXT("KHUTutorTools_KHUTutorReadToolset_GetBlueprintGraphDigest");
        Call.Arguments = TEXT("{\"assetPath\":\"/Game/KHUTest/BP_Door_Broken\",\"graphName\":\"EventGraph\",\"focus\":\"errors\",\"maxNodes\":20,\"hops\":2}");
        R.Calls.Add(Call); auto C = MakeShared<FJsonObject>(); auto F = MakeShared<FJsonObject>();
        F->SetStringField(TEXT("name"), Call.Name); F->SetStringField(TEXT("arguments"), Call.Arguments);
        C->SetStringField(TEXT("id"), Call.Id); C->SetStringField(TEXT("type"), TEXT("function")); C->SetObjectField(TEXT("function"), F);
        R.Assistant->SetArrayField(TEXT("tool_calls"), {MakeShared<FJsonValueObject>(C)}); R.Assistant->SetField(TEXT("content"), MakeShared<FJsonValueNull>());
    }
    else
    {
        if (!LastContent.StartsWith(TEXT("GRAPH ")) || !LastContent.Contains(TEXT("Target=!UNCONNECTED")))
        { R.Content = TEXT("테스트용 응답입니다. BP_Door_Broken의 Target 미연결 오류를 확인하지 못했습니다. Blueprint를 열고 Compile한 뒤 새 작업으로 다시 확인해 주세요."); }
        else
        {
            FString Ref; TArray<FString> Lines; LastContent.ParseIntoArrayLines(Lines);
            for (const auto& Line : Lines)
            { if (Line.StartsWith(TEXT("#")) && Line.Contains(TEXT("Target=!UNCONNECTED"))) { int32 Space; if (Line.FindChar(' ', Space)) { Ref = Line.Left(Space); break; } } }
            if (Note.Contains(TEXT("L0."))) { R.Content = TEXT("회전시킬 대상은 어떤 컴포넌트인가요? Target 핀에는 무엇이 연결되어야 할까요?"); }
            else if (Note.Contains(TEXT("L2."))) { R.Content = Ref + TEXT(" **Set Relative Rotation**의 `Target`을 확인하세요.\n1. 회전시킬 Scene Component 참조를 찾으세요.\n2. 그 참조를 Target에 연결하세요.\nCompile 결과가 달라졌나요?"); }
            else if (Note.Contains(TEXT("L3."))) { R.Content = Ref + TEXT(" Target이 비어 있습니다.\n1. 회전시킬 Scene Component를 확인합니다.\n2. 해당 참조를 Graph로 가져옵니다.\n3. Set Relative Rotation의 Target에 연결합니다.\n4. Compile 후 오류가 사라지는지 확인합니다.\n이 함수는 회전할 컴포넌트 참조가 필요합니다."); }
            else { R.Content = Ref + TEXT(" **Set Relative Rotation**의 `Target` 핀을 살펴보세요.\n회전시킬 Scene Component가 연결되어 있나요?"); }
        }
        R.Assistant->SetStringField(TEXT("content"), R.Content);
    }
    const uint64 G = Generation; const TWeakPtr<FKHUPanelMock> Weak = AsShared();
    AsyncTask(ENamedThreads::GameThread, [Weak, G, R = MoveTemp(R), Callback = MoveTemp(Callback)]() mutable
    { if (auto S = Weak.Pin(); S && S->Generation == G) { Callback(MoveTemp(R)); } });
}
