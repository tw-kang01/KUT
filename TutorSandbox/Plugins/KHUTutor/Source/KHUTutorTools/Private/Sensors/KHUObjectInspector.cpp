#include "Sensors/KHUObjectInspector.h"
#include "Digest/KHULogDigest.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "UObject/UnrealType.h"
#include "UObject/ScriptInterface.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    FString Json(const TSharedRef<FJsonObject>& O)
    { FString S; FJsonSerializer::Serialize(O, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S)); return S; }
    FString Safe(const FString& S) { return FKHULogDigest::Sanitize(S.Left(512)) + (S.Len() > 512 ? TEXT("...[value truncated]") : TEXT("")); }
    bool Sensitive(const FString& Name)
    {
        const FString N = Name.ToLower();
        for (const TCHAR* Word : {TEXT("apikey"), TEXT("api_key"), TEXT("password"), TEXT("secret"), TEXT("credential"), TEXT("token"), TEXT("authorization")})
        { if (N.Contains(Word)) { return true; } }
        return false;
    }
    bool InWorld(UObject* O, UWorld* W)
    { return IsValid(O) && W && !O->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && (O->IsA<AActor>() || O->IsA<UActorComponent>()) && O->GetWorld() == W; }
    TSharedRef<FJsonObject> Identity(UObject* O)
    {
        auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("path"), O->GetPathName());
        J->SetStringField(TEXT("name"), O->GetName()); J->SetStringField(TEXT("class"), O->GetClass()->GetPathName());
        if (auto* A = Cast<AActor>(O)) { J->SetStringField(TEXT("label"), Safe(A->GetActorLabel(false))); }
        if (auto* C = Cast<UActorComponent>(O)) { if (IsValid(C->GetOwner())) { J->SetStringField(TEXT("owner"), C->GetOwner()->GetPathName()); } }
        return J;
    }
    TSharedRef<FJsonObject> Header(UWorld* W)
    {
        auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("world"), W ? W->GetPathName() : TEXT("ClassDefault"));
        J->SetStringField(TEXT("scope"), !W ? TEXT("ClassDefault") : W->WorldType == EWorldType::PIE ? TEXT("PIE") : TEXT("Editor"));
        J->SetStringField(TEXT("observed_at_utc"), FDateTime::UtcNow().ToIso8601());
        J->SetBoolField(TEXT("truncated"), false); return J;
    }
    bool Append(const TSharedRef<FJsonObject>& Root, const TCHAR* Key, TArray<TSharedPtr<FJsonValue>>& Items, const TSharedRef<FJsonObject>& Item)
    {
        Items.Add(MakeShared<FJsonValueObject>(Item)); Root->SetArrayField(Key, Items);
        if (Json(Root).Len() <= 5750) { return true; }
        Items.Pop(); Root->SetArrayField(Key, Items); Root->SetBoolField(TEXT("truncated"), true); return false;
    }
    TArray<FProperty*> Properties(UObject* O, bool& Truncated)
    {
        TArray<FProperty*> Out;
        for (TFieldIterator<FProperty> It(O->GetClass()); It; ++It)
        { if (Out.Num() == 256) { Truncated = true; break; } Out.Add(*It); }
        Out.Sort([](const FProperty& A, const FProperty& B) { return A.GetName() < B.GetName(); }); return Out;
    }
    bool ReferenceType(FProperty* P)
    {
        if (auto* Ref = CastField<FObjectProperty>(P))
        { return Ref->PropertyClass->IsChildOf(AActor::StaticClass()) || Ref->PropertyClass->IsChildOf(UActorComponent::StaticClass()); }
        return CastField<FInterfaceProperty>(P) != nullptr;
    }
    UObject* Reference(FProperty* P, const void* Value)
    {
        if (auto* Ref = CastField<FObjectProperty>(P)) { return Ref->GetObjectPropertyValue(Value); }
        if (CastField<FInterfaceProperty>(P)) { return static_cast<const FScriptInterface*>(Value)->GetObject(); }
        return nullptr;
    }
    bool Value(FProperty* P, UObject* O, FString& Out)
    {
        if (!O || P->HasGetter() || P->ArrayDim != 1) { Out = TEXT("not_read: getter or fixed array"); return false; }
        const void* Address = P->ContainerPtrToValuePtr<void>(O);
        if (auto* S = CastField<FStrProperty>(P)) { Out = Safe(S->GetPropertyValue(Address)); return true; }
        if (auto* T = CastField<FTextProperty>(P)) { Out = Safe(T->GetPropertyValue(Address).ToString()); return true; }
        if (auto* Ref = CastField<FObjectProperty>(P))
        { UObject* Target = Ref->GetObjectPropertyValue(Address); Out = IsValid(Target) ? Target->GetPathName() : Target ? TEXT("invalid_object") : TEXT("None"); return true; }
        if (CastField<FInterfaceProperty>(P))
        { UObject* Target = Reference(P, Address); Out = IsValid(Target) ? Target->GetPathName() : Target ? TEXT("invalid_object") : TEXT("None"); return true; }
        bool Supported = CastField<FBoolProperty>(P) || CastField<FNumericProperty>(P) || CastField<FEnumProperty>(P) || CastField<FNameProperty>(P);
        if (auto* S = CastField<FStructProperty>(P))
        { const FString N = S->Struct->GetName(); Supported = N == TEXT("Vector") || N == TEXT("Vector2D") || N == TEXT("Rotator") || N == TEXT("Transform") || N == TEXT("LinearColor") || N == TEXT("Color"); }
        if (!Supported) { Out = TEXT("not_read: container, soft/weak reference, delegate or unsupported struct"); return false; }
        P->ExportTextItem_InContainer(Out, O, nullptr, O, 0); Out = Safe(Out); return true;
    }
}
UWorld* FKHUObjectInspector::World(const FString& Scope, FString& Issue)
{
    if (Scope != TEXT("Editor") && Scope != TEXT("PIE")) { Issue = TEXT("worldScope must be Editor or PIE"); return nullptr; }
    UWorld* W = GEditor ? (Scope == TEXT("PIE") ? GEditor->PlayWorld.Get() : GEditor->GetEditorWorldContext().World()) : nullptr;
    if (!W || (Scope == TEXT("PIE") ? W->WorldType != EWorldType::PIE : W->WorldType != EWorldType::Editor))
    { Issue = TEXT("requested world is unavailable; no fallback to a different world"); return nullptr; }
    return W;
}
UObject* FKHUObjectInspector::Find(UWorld* W, const FString& Path, FString& Issue)
{
    if (!W || Path.IsEmpty() || Path.Len() > 1024 || !Path.StartsWith(TEXT("/")))
    { Issue = TEXT("use an exact loaded actor/component object path from the requested world, not a label"); return nullptr; }
    UObject* O = FindObject<UObject>(nullptr, *Path);
    if (!InWorld(O, W) || O->GetPathName() != Path)
    { Issue = TEXT("object unavailable, not actor/component, or belongs to a different world"); return nullptr; }
    return O;
}
FString FKHUObjectInspector::Relations(UWorld* W, const TArray<UObject*>& Roots, int32 Depth, int32 MaxObjects)
{
    check(IsInGameThread());
    if (!W || (W->WorldType != EWorldType::Editor && W->WorldType != EWorldType::PIE) || Roots.IsEmpty()) { return TEXT("ERROR: no roots in requested Editor/PIE world"); }
    for (UObject* O : Roots) { if (!InWorld(O, W)) { return TEXT("ERROR: root belongs to a different world or is not actor/component"); } }
    Depth = FMath::Clamp(Depth, 0, 2); MaxObjects = FMath::Clamp(MaxObjects, 1, 8);
    auto Result = Header(W); Result->SetNumberField(TEXT("max_depth"), Depth); Result->SetNumberField(TEXT("max_objects"), MaxObjects);
    Result->SetStringField(TEXT("limits"), TEXT("Only loaded hard actor/component and interface references. None is an observed value, not proof of a bug. Calls, dispatcher bindings, soft/weak refs, maps/sets and execution history are not observed."));
    struct FPending { UObject* Object; int32 Level; };
    TArray<FPending> Queue; TSet<UObject*> Seen;
    for (UObject* O : Roots) { if (!Seen.Contains(O)) { if (Queue.Num() >= MaxObjects) { Result->SetBoolField(TEXT("truncated"), true); break; } Queue.Add({O, 0}); Seen.Add(O); } }
    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (int32 Index = 0; Index < Queue.Num(); ++Index)
    {
        const FPending Current = Queue[Index]; auto Node = Identity(Current.Object); Node->SetNumberField(TEXT("depth"), Current.Level);
        TArray<TSharedPtr<FJsonValue>> Edges; bool Cut = false;
        auto AddEdge = [&](const FString& Via, UObject* Target, FProperty* Property = nullptr)
        {
            if (Edges.Num() >= 12) { Cut = true; return; }
            auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("via"), Safe(Via));
            const bool Valid = IsValid(Target), SameWorld = Valid && InWorld(Target, W);
            E->SetStringField(TEXT("state"), !Target ? TEXT("None") : !Valid ? TEXT("invalid") : SameWorld ? TEXT("observed") : TEXT("outside_requested_world_or_type"));
            if (Valid) { E->SetStringField(TEXT("target_path"), Target->GetPathName()); }
            if (auto* Interface = CastField<FInterfaceProperty>(Property))
            { E->SetStringField(TEXT("interface"), Interface->InterfaceClass->GetPathName()); if (Valid) { E->SetBoolField(TEXT("implements_interface"), Target->GetClass()->ImplementsInterface(Interface->InterfaceClass)); } }
            bool Followed = false;
            if (SameWorld && Current.Level < Depth)
            {
                if (Seen.Contains(Target)) { Followed = true; }
                else if (Queue.Num() < MaxObjects) { Seen.Add(Target); Queue.Add({Target, Current.Level + 1}); Followed = true; }
                else { Cut = true; }
            }
            E->SetBoolField(TEXT("scheduled_or_seen"), Followed); Edges.Add(MakeShared<FJsonValueObject>(E));
        };
        for (FProperty* P : Properties(Current.Object, Cut))
        {
            if (P->HasGetter() || Sensitive(P->GetName())) { continue; }
            if (ReferenceType(P))
            {
                for (int32 I = 0; I < FMath::Min(P->ArrayDim, 4); ++I)
                { AddEdge(P->GetName() + (P->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), I) : FString()), Reference(P, P->ContainerPtrToValuePtr<void>(Current.Object, I)), P); }
                Cut |= P->ArrayDim > 4;
            }
            else if (auto* A = CastField<FArrayProperty>(P); A && ReferenceType(A->Inner))
            {
                FScriptArrayHelper Array(A, A->ContainerPtrToValuePtr<void>(Current.Object));
                for (int32 I = 0; I < FMath::Min(Array.Num(), 8); ++I) { AddEdge(FString::Printf(TEXT("%s[%d]"), *P->GetName(), I), Reference(A->Inner, Array.GetRawPtr(I)), A->Inner); }
                Cut |= Array.Num() > 8;
            }
            if (Edges.Num() >= 12) { Cut = true; break; }
        }
        if (auto* A = Cast<AActor>(Current.Object))
        {
            TArray<UActorComponent*> Components; A->GetComponents(Components);
            Components.Sort([](const UActorComponent& L, const UActorComponent& R) { return L.GetPathName() < R.GetPathName(); });
            for (int32 I = 0; I < FMath::Min(Components.Num(), 8); ++I) { AddEdge(TEXT("component:") + Components[I]->GetName(), Components[I]); }
            Cut |= Components.Num() > 8;
        }
        Node->SetArrayField(TEXT("references"), Edges); Node->SetBoolField(TEXT("references_truncated"), Cut);
        if (!Append(Result, TEXT("objects"), Nodes, Node)) { break; }
        if (Cut) { Result->SetBoolField(TEXT("truncated"), true); }
    }
    const FString Output = Json(Result); return Output.Len() <= 6000 ? Output : TEXT("ERROR: object metadata exceeds the output limit; narrow the target");
}
FString FKHUObjectInspector::Details(UObject* O, UWorld* W, const FString& Names, int32 MaxProperties)
{
    check(IsInGameThread());
    if (!IsValid(O) || (W && W->WorldType != EWorldType::Editor && W->WorldType != EWorldType::PIE) || (W ? !InWorld(O, W) : !O->HasAnyFlags(RF_ClassDefaultObject))) { return TEXT("ERROR: object does not match requested world/default scope"); }
    if (Names.Len() > 1024) { return TEXT("ERROR: propertyNames is too long"); }
    MaxProperties = FMath::Clamp(MaxProperties, 1, 16);
    TArray<FString> Requested; Names.ParseIntoArray(Requested, TEXT(","), true);
    if (Requested.Num() > 16) { return TEXT("ERROR: request at most 16 exact property names"); }
    for (FString& Name : Requested) { Name.TrimStartAndEndInline(); }
    auto Result = Header(W); Result->SetObjectField(TEXT("object"), Identity(O));
    UObject* CDO = O->GetClass()->GetDefaultObject(false); UObject* Archetype = O->GetArchetype();
    Result->SetStringField(TEXT("class_default_path"), CDO ? CDO->GetPathName() : TEXT("unavailable"));
    Result->SetStringField(TEXT("archetype_path"), Archetype ? Archetype->GetPathName() : TEXT("unavailable"));
    Result->SetStringField(TEXT("default_origin"), O->GetClass()->ClassGeneratedBy ? TEXT("Blueprint generated class default") : TEXT("native class default"));
    Result->SetStringField(TEXT("interpretation"), TEXT("Value differences do not prove a saved override or a bug. PIE values are current observations, not execution history. Class default is not a placed instance."));
    TArray<FProperty*> Candidates; bool Cut = false;
    if (Requested.IsEmpty()) { Candidates = Properties(O, Cut); }
    else
    {
        for (const FString& Name : Requested)
        {
            FProperty* P = O->GetClass()->FindPropertyByName(FName(*Name));
            if (!P || P->GetName() != Name) { return TEXT("ERROR: property not found; use exact reflected property name: ") + Safe(Name); }
            Candidates.AddUnique(P);
        }
    }
    TArray<TSharedPtr<FJsonValue>> Values;
    for (FProperty* P : Candidates)
    {
        const bool Visible = P->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
        if (Requested.IsEmpty() && (!Visible || Sensitive(P->GetName()))) { continue; }
        if (Values.Num() >= MaxProperties) { Cut = true; break; }
        auto V = MakeShared<FJsonObject>(); V->SetStringField(TEXT("property"), P->GetName());
        V->SetStringField(TEXT("type"), P->GetCPPType());
        FString Current, Default, Template;
        const bool CanRead = Visible && !Sensitive(P->GetName());
        const bool Read = CanRead && Value(P, O, Current);
        V->SetStringField(TEXT("status"), Read ? TEXT("observed") : TEXT("not_read"));
        V->SetStringField(TEXT("value"), CanRead ? Current : TEXT("restricted or not visible/editable"));
        if (Read)
        {
            if (CDO && Value(P, CDO, Default))
            { V->SetStringField(TEXT("class_default"), Default); V->SetBoolField(TEXT("differs_from_class_default"), !P->Identical_InContainer(O, CDO)); }
            if (Archetype && Archetype->GetClass()->FindPropertyByName(P->GetFName()) == P && Value(P, Archetype, Template))
            { V->SetStringField(TEXT("archetype_value"), Template); V->SetBoolField(TEXT("differs_from_archetype"), !P->Identical_InContainer(O, Archetype)); }
        }
        if (!Append(Result, TEXT("properties"), Values, V)) { Cut = true; break; }
    }
    Result->SetArrayField(TEXT("properties"), Values); if (Cut) { Result->SetBoolField(TEXT("truncated"), true); }
    const FString Output = Json(Result); return Output.Len() <= 6000 ? Output : TEXT("ERROR: object metadata exceeds the output limit; narrow the target");
}
