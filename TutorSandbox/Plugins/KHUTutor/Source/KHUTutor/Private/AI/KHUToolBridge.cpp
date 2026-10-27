#include "AI/KHUToolBridge.h"
#include "AI/KHUHintPolicy.h"
#include "Mcp/KHUMcpProtocol.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace
{
    TSharedPtr<FJsonObject> Clean(const TSharedPtr<FJsonObject>& Source, int32 Depth = 0)
    {
        if (!Source.IsValid() || Depth > 16 || Source->HasField(TEXT("$ref"))) { return nullptr; }
        auto Out = MakeShared<FJsonObject>();
        for (const TCHAR* Key : {TEXT("type"), TEXT("required"), TEXT("enum"), TEXT("minimum"), TEXT("maximum")})
        { if (const auto* V = Source->Values.Find(Key)) { Out->SetField(Key, *V); } }
        FString Description;
        if (Source->TryGetStringField(TEXT("description"), Description)) { Out->SetStringField(TEXT("description"), Description.Left(300)); }
        const TSharedPtr<FJsonObject>* Properties = nullptr;
        if (Source->TryGetObjectField(TEXT("properties"), Properties))
        {
            auto P = MakeShared<FJsonObject>();
            for (const auto& Pair : (*Properties)->Values)
            {
                if (Pair.Value->Type != EJson::Object) { return nullptr; }
                auto Child = Clean(Pair.Value->AsObject(), Depth + 1); if (!Child) { return nullptr; }
                P->SetObjectField(Pair.Key, Child);
            }
            Out->SetObjectField(TEXT("properties"), P);
        }
        if (Source->HasField(TEXT("items")))
        {
            const TSharedPtr<FJsonObject>* Item = nullptr;
            if (!Source->TryGetObjectField(TEXT("items"), Item)) { return nullptr; }
            auto Child = Clean(*Item, Depth + 1); if (!Child) { return nullptr; } Out->SetObjectField(TEXT("items"), Child);
        }
        FString Type; if (!Out->TryGetStringField(TEXT("type"), Type)) { return nullptr; }
        return Out;
    }
    const TSharedPtr<FJsonValue>* Exact(const TSharedPtr<FJsonObject>& Object, const FString& Key)
    { for (const auto& P : Object->Values) { if (FString(*P.Key).Equals(Key, ESearchCase::CaseSensitive)) { return &P.Value; } } return nullptr; }
}
FString FKHUToolBridge::FunctionName(const FString& Native)
{
    FString Name;
    for (TCHAR C : Native) { Name.AppendChar((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C == '-' ? C : '_'); }
    if (Name.IsEmpty()) { Name = TEXT("tool"); }
    if (Name.Len() > 64) { const FTCHARToUTF8 Utf8(*Native); Name = Name.Left(55) + TEXT("_") + FMD5::HashBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()).Left(8); }
    return Name;
}
FKHUToolBridge::FKHUToolBridge(const FKHUConfig& Config, const TArray<TSharedPtr<FJsonValue>>& NativeTools) : Never(Config.Never)
{
    TArray<TSharedPtr<FJsonObject>> Sorted;
    for (const auto& V : NativeTools) { if (V.IsValid() && V->Type == EJson::Object) { FString N; if (V->AsObject()->TryGetStringField(TEXT("name"), N)) { Sorted.Add(V->AsObject()); } } }
    Sorted.Sort([](const auto& A, const auto& B) { return A->GetStringField(TEXT("name")).Compare(B->GetStringField(TEXT("name")), ESearchCase::CaseSensitive) < 0; });
    TSet<FString> UsedNative, UsedFunctions;
    for (const auto& O : Sorted)
    {
        FTool T; const TSharedPtr<FJsonObject>* S = nullptr;
        if (!O->TryGetStringField(TEXT("name"), T.Native) || UsedNative.Contains(T.Native.ToLower()) || !O->TryGetObjectField(TEXT("inputSchema"), S)) { continue; }
        bool Forbidden = false; for (const auto& P : Never) { Forbidden |= T.Native.ToLower().MatchesWildcard(P.ToLower()); }
        if (Forbidden) { continue; }
        for (const TCHAR* Group : {TEXT("read"), TEXT("show"), TEXT("act"), TEXT("epic_t2_extra")})
        { if (const auto* Names = Config.ToolGroups.Find(Group)) { for (const auto& N : *Names) { if (N.Equals(T.Native, ESearchCase::CaseSensitive)) { T.Group = Group; } } } }
        if (T.Group.IsEmpty() || T.Group == TEXT("act")) { continue; } // Act tools stay excluded until each can confirm its own change in-tool (FKHUApprovalGate).
        T.Schema = Clean(*S); FString Type;
        if (!T.Schema || !T.Schema->TryGetStringField(TEXT("type"), Type) || Type != TEXT("object")) { continue; }
        if (!T.Schema->HasField(TEXT("properties"))) { T.Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>()); }
        const FString Base = FunctionName(T.Native); T.Function = Base; int32 Suffix = 1;
        while (UsedFunctions.Contains(T.Function.ToLower())) { const FString Tail = FString::Printf(TEXT("_%d"), ++Suffix); T.Function = Base.Left(64 - Tail.Len()) + Tail; }
        UsedNative.Add(T.Native.ToLower()); UsedFunctions.Add(T.Function.ToLower());
        auto Function = MakeShared<FJsonObject>(); Function->SetStringField(TEXT("name"), T.Function);
        FString Description; O->TryGetStringField(TEXT("description"), Description); Function->SetStringField(TEXT("description"), Description.Left(300));
        Function->SetObjectField(TEXT("parameters"), T.Schema);
        T.Definition = MakeShared<FJsonObject>(); T.Definition->SetStringField(TEXT("type"), TEXT("function")); T.Definition->SetObjectField(TEXT("function"), Function);
        Tools.Add(MoveTemp(T));
    }
}
bool FKHUToolBridge::Allowed(const FTool& T, const FString& Tier, int32 Hint, bool bShow) const
{ return Hint >= 0 && Hint <= 4 && (T.Group == TEXT("read") || (T.Group == TEXT("show") && bShow && FKHUHintPolicy::AllowShow(Hint)) || (T.Group == TEXT("epic_t2_extra") && Tier == TEXT("T2"))); }
const TArray<TSharedPtr<FJsonValue>>& FKHUToolBridge::Definitions(const FString& Tier, int32 Hint, bool bShow)
{
    const FString Key = FString::Printf(TEXT("%s/%d/%d"), *Tier, Hint, bShow);
    if (!Cache.Contains(Key)) { auto& List = Cache.Add(Key); for (const auto& T : Tools) { if (Allowed(T, Tier, Hint, bShow)) { List.Add(MakeShared<FJsonValueObject>(T.Definition)); } } }
    return Cache[Key];
}
bool FKHUToolBridge::Resolve(const FString& Function, const FString& Json, const FString& Tier, int32 Hint, bool bShow,
    FString& Native, TSharedPtr<FJsonObject>& Arguments, FString& Error) const
{
    for (const auto& T : Tools)
    {
        if (T.Function.Equals(Function, ESearchCase::CaseSensitive) && Allowed(T, Tier, Hint, bShow))
        {
            if (Json.Len() <= 65536 && KHUMcpProtocol::ReadObject(Json, Arguments) && Validate(T.Schema, MakeShared<FJsonValueObject>(Arguments))) { Native = T.Native; return true; }
            Error = TEXT("ERROR: tool arguments do not match the schema"); return false;
        }
    }
    Error = TEXT("ERROR: tool not allowed"); return false;
}
const FString& FKHUToolBridge::SerializedDefinitions(const FString& Tier, int32 Hint, bool bShow)
{
    const FString Key = FString::Printf(TEXT("%s/%d/%d"), *Tier, Hint, bShow);
    if (!SerializedCache.Contains(Key))
    { FString Text; FJsonSerializer::Serialize(Definitions(Tier, Hint, bShow), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)); SerializedCache.Add(Key, MoveTemp(Text)); }
    return SerializedCache[Key];
}
bool FKHUToolBridge::Validate(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonValue>& V, int32 Depth)
{
    if (!Schema || !V || Depth > 16) { return false; }
    FString Type; if (!Schema->TryGetStringField(TEXT("type"), Type)) { return false; }
    const TArray<TSharedPtr<FJsonValue>>* Enum = nullptr;
    if (Schema->TryGetArrayField(TEXT("enum"), Enum))
    {
        bool Found = false;
        for (const auto& E : *Enum)
        {
            if (!E || E->Type != V->Type) { continue; }
            Found |= (V->Type == EJson::String && E->AsString() == V->AsString())
                || (V->Type == EJson::Number && E->AsNumber() == V->AsNumber()) || (V->Type == EJson::Boolean && E->AsBool() == V->AsBool());
        }
        if (!Found) { return false; }
    }
    if (Type == TEXT("object"))
    {
        if (V->Type != EJson::Object) { return false; } const auto O = V->AsObject(); const TSharedPtr<FJsonObject>* P = nullptr;
        if (!Schema->TryGetObjectField(TEXT("properties"), P)) { return O->Values.IsEmpty(); }
        const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
        if (Schema->TryGetArrayField(TEXT("required"), Required)) { for (const auto& R : *Required) { FString N; if (!R->TryGetString(N) || !Exact(O, N)) { return false; } } }
        for (const auto& Pair : O->Values) { const auto* Child = Exact(*P, FString(*Pair.Key)); if (!Child || (*Child)->Type != EJson::Object || !Validate((*Child)->AsObject(), Pair.Value, Depth + 1)) { return false; } }
        return true;
    }
    if (Type == TEXT("array"))
    {
        if (V->Type != EJson::Array || V->AsArray().Num() > 1024) { return false; } const TSharedPtr<FJsonObject>* Item = nullptr;
        if (!Schema->TryGetObjectField(TEXT("items"), Item)) { return false; } for (const auto& Child : V->AsArray()) { if (!Validate(*Item, Child, Depth + 1)) { return false; } } return true;
    }
    if (Type == TEXT("string")) { return V->Type == EJson::String; }
    if (Type == TEXT("boolean")) { return V->Type == EJson::Boolean; }
    if (Type == TEXT("number") || Type == TEXT("integer"))
    {
        if (V->Type != EJson::Number) { return false; } const double N = V->AsNumber(); double Bound = 0;
        return FMath::IsFinite(N) && (Type != TEXT("integer") || FMath::FloorToDouble(N) == N)
            && (!Schema->TryGetNumberField(TEXT("minimum"), Bound) || N >= Bound) && (!Schema->TryGetNumberField(TEXT("maximum"), Bound) || N <= Bound);
    }
    return false;
}
FString FKHUToolBridge::Compress(const FString& Text, int32 MaxTokens)
{
    const int32 Limit = FMath::Max(0, MaxTokens); double Tokens = 0;
    for (TCHAR C : Text) { Tokens += C < 128 ? 0.25 : 1.0; }
    if (Tokens <= Limit) { return Text; }
    const FString Marker = TEXT("\n[... tool result truncated ...]\n");
    if (Limit <= Marker.Len()) { return FString(TEXT("[truncated]")).Left(Limit); }
    // Each retained UTF-16 unit is charged one token: conservative for Korean and surrogate pairs.
    const int32 Available = Limit - Marker.Len();
    const int32 HeadLimit = FMath::Min(Limit * 7 / 10, Available * 7 / 9), TailLimit = FMath::Min(Limit * 2 / 10, Available * 2 / 9);
    FString Head = Text.Left(HeadLimit), Tail = Text.Right(TailLimit);
    if (!Head.IsEmpty() && Head[Head.Len() - 1] >= 0xD800 && Head[Head.Len() - 1] <= 0xDBFF) { Head.LeftChopInline(1); }
    if (!Tail.IsEmpty() && Tail[0] >= 0xDC00 && Tail[0] <= 0xDFFF) { Tail.RightChopInline(1); }
    return Head + Marker + Tail;
}
