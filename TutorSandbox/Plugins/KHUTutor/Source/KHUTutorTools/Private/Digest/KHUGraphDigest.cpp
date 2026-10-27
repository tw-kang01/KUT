#include "Digest/KHUGraphDigest.h"
#include "Digest/KHUNodeRefCache.h"
#include "Digest/KHULogDigest.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Timeline.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditor.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/UnrealType.h"

namespace
{
    FString Safe(FString Text, int32 Limit = 240)
    {
        Text.ReplaceInline(TEXT("\r"), TEXT("")); Text.ReplaceInline(TEXT("\n"), TEXT("\\n")); Text = FKHULogDigest::Sanitize(Text);
        Text.ReplaceInline(TEXT("|"), TEXT("\\|")); Text.ReplaceInline(TEXT("\""), TEXT("'"));
        return Text.Len() <= Limit ? Text : Text.Left(FMath::Max(0, Limit - 3)) + TEXT("...");
    }
    FString Tag(UEdGraphNode* Node)
    {
        if (Node->IsA<UK2Node_CustomEvent>()) { return TEXT("Custom"); }
        if (Node->IsA<UK2Node_Event>()) { return TEXT("Event"); }
        if (const auto* Call = Cast<UK2Node_CallFunction>(Node)) { return Call->IsNodePure() ? TEXT("Pure") : TEXT("Call"); }
        if (Node->IsA<UK2Node_VariableSet>()) { return TEXT("Set"); }
        if (Node->IsA<UK2Node_VariableGet>()) { return TEXT("Var"); }
        if (Node->IsA<UK2Node_IfThenElse>()) { return TEXT("Branch"); }
        if (Node->IsA<UK2Node_DynamicCast>()) { return TEXT("Cast"); }
        if (Node->IsA<UK2Node_MacroInstance>()) { return TEXT("Macro"); }
        if (Node->IsA<UK2Node_Timeline>()) { return TEXT("Timeline"); }
        if (Node->IsA<UEdGraphNode_Comment>()) { return TEXT("Comment"); }
        return TEXT("Custom");
    }
    bool RequiredTarget(UBlueprint* Blueprint, UEdGraphNode* Node, UEdGraphPin* Pin)
    {
        if (Pin->Direction != EGPD_Input || !Pin->LinkedTo.IsEmpty() || Pin->DefaultObject || !Pin->GetDefaultAsString().IsEmpty()
            || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Object || Pin->bDefaultValueIsIgnored) { return false; }
        const bool bTarget = Pin->PinName == UEdGraphSchema_K2::PN_Self || Pin->PinName == TEXT("Target");
        const auto* Call = Cast<UK2Node_CallFunction>(Node);
        if (bTarget && Call)
        {
            const UFunction* Function = Call->GetTargetFunction();
            if (Function && Function->HasAnyFunctionFlags(FUNC_Static)) { return false; }
            const UClass* Expected = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
            const UClass* Self = Blueprint->GeneratedClass ? Blueprint->GeneratedClass.Get() : Blueprint->ParentClass.Get();
            return Expected && (!Self || !Self->IsChildOf(Expected));
        }
        return Node->bHasCompilerMessage && Node->ErrorMsg.Contains(TEXT("must have a connection"))
            && Node->ErrorMsg.Contains(Pin->PinName.ToString());
    }
    FString Row(UBlueprint* Blueprint, UEdGraphNode* Node, const TMap<UEdGraphNode*, int32>& Refs, int32 Budget)
    {
        FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
        if (const auto* Call = Cast<UK2Node_CallFunction>(Node)) { Title = Call->GetFunctionName().ToString(); }
        FString Head = FString::Printf(TEXT("#%d [%s] %s"), Refs[Node], *Tag(Node), *Safe(Title, FMath::Clamp(Budget / 3 - 18, 8, 60)));
        FString Diagnostic;
        if (FKHUGraphDigest::HasDiagnostic(Node))
        { Diagnostic = FString::Printf(TEXT(" | %s \"%s\""), Node->ErrorType == EMessageSeverity::Error ? TEXT("ERR") : TEXT("WARN"), *Safe(Node->ErrorMsg, FMath::Clamp(Budget / 2 - 12, 18, 320))); }
        TArray<FString> Parts;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin->bHidden || Pin->bAdvancedView || Pin->bOrphanedPin) { continue; }
            FString Name = Pin->PinName == UEdGraphSchema_K2::PN_Self ? FString(TEXT("Target")) : Safe(Pin->PinName.ToString(), 40);
            TArray<FString> Links;
            for (UEdGraphPin* Link : Pin->LinkedTo)
            {
                if (!Link || !Refs.Contains(Link->GetOwningNodeUnchecked())) { continue; }
                const int32 Ref = Refs[Link->GetOwningNodeUnchecked()];
                Links.AddUnique(Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
                    ? FString::Printf(TEXT("#%d.%s"), Ref, *Safe(Link->PinName.ToString(), 40)) : FString::Printf(TEXT("#%d"), Ref));
            }
            Links.Sort();
            if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                if (Pin->Direction == EGPD_Input) { continue; }
                const bool bDefault = Pin->PinName == UEdGraphSchema_K2::PN_Then;
                Parts.Add((bDefault ? FString(TEXT("exec")) : TEXT("exec(") + Name + TEXT(")")) + TEXT(">")
                    + (Links.IsEmpty() ? TEXT("none") : FString::Join(Links, TEXT(","))));
            }
            else if (Pin->Direction == EGPD_Output)
            { Parts.Add(Name + TEXT(">") + (Links.IsEmpty() ? TEXT("none") : FString::Join(Links, TEXT(",")))); }
            else if (!Links.IsEmpty()) { Parts.Add(Name + TEXT("<") + FString::Join(Links, TEXT(","))); }
            else if (RequiredTarget(Blueprint, Node, Pin)) { Parts.Add((Pin->PinName == UEdGraphSchema_K2::PN_Self ? FString(TEXT("Target")) : Name) + TEXT("=!UNCONNECTED")); }
            else if (!Pin->bDefaultValueIsIgnored && !Pin->DoesDefaultValueMatchAutogenerated())
            { Parts.Add(Name + TEXT("=") + Safe(Pin->GetDefaultAsString(), 160)); }
        }
        FString Pins = Parts.IsEmpty() ? FString() : TEXT(" | ") + FString::Join(Parts, TEXT(" | "));
        const int32 Remaining = FMath::Max(0, Budget - Head.Len() - Diagnostic.Len());
        if (Pins.Len() > Remaining) { Pins = Remaining >= 24 ? Pins.Left(Remaining - 20) + TEXT(" ...[pins truncated]") : (Remaining >= 4 ? TEXT(" ...") : TEXT("")); }
        return Head + Pins + Diagnostic;
    }
    FString Variables(UBlueprint* Blueprint, UEdGraph* Graph, const TArray<UEdGraphNode*>& Nodes)
    {
        TSet<FName> Used;
        TArray<FBPVariableDescription> Descriptions = Blueprint->NewVariables;
        Descriptions.Append(Blueprint->GeneratedVariables);
        for (UEdGraphNode* Node : Nodes)
        {
            if (const auto* Variable = Cast<UK2Node_Variable>(Node)) { const FName Name = Variable->VariableReference.GetMemberName(); if (!Name.IsNone()) { Used.Add(Name); } }
            if (const auto* Entry = Cast<UK2Node_FunctionEntry>(Node)) { Descriptions.Append(Entry->LocalVariables); }
        }
        TArray<FString> Names; for (FName Name : Used) { Names.Add(Name.ToString()); } Names.Sort();
        FString Result;
        for (const FString& Name : Names)
        {
            FString Type = TEXT("unknown"), Default = TEXT("unknown");
            const auto* Description = Descriptions.FindByPredicate([&](const FBPVariableDescription& V) { return V.VarName == FName(*Name); });
            if (Description)
            {
                Type = Description->VarType.PinSubCategoryObject.IsValid() ? Description->VarType.PinSubCategoryObject->GetName() : Description->VarType.PinCategory.ToString();
                Default = Description->DefaultValue.IsEmpty() ? TEXT("unknown") : Description->DefaultValue;
                if (Description->VarType.IsContainer()) { Default = TEXT("<container>"); }
            }
            UClass* Class = Blueprint->GeneratedClass ? Blueprint->GeneratedClass.Get() : Blueprint->ParentClass.Get();
            if (Class)
            {
                if (FProperty* Property = Class->FindPropertyByName(FName(*Name)))
                {
                    Type = Property->GetCPPType();
                    if (!CastField<FArrayProperty>(Property) && !CastField<FMapProperty>(Property) && !CastField<FSetProperty>(Property))
                    { if (UObject* Cdo = Class->GetDefaultObject(false)) { Default.Reset(); Property->ExportTextItem_InContainer(Default, Cdo, nullptr, Cdo, 0); } }
                }
            }
            const FString Part = Safe(Name, 48) + TEXT(":") + Safe(Type, 48) + TEXT("=") + Safe(Default, 80);
            if (Result.Len() + Part.Len() > 230) { Result += TEXT(" ...[vars truncated]"); break; }
            Result += (Result.IsEmpty() ? FString() : FString(TEXT("  "))) + Part;
        }
        return Result.IsEmpty() ? FString() : TEXT("\nVARS ") + Result;
    }
    FString Comments(const TArray<UEdGraphNode*>& Nodes, const TSet<UEdGraphNode*>& Shown, const TMap<UEdGraphNode*, int32>& Refs)
    {
        FString Result;
        for (UEdGraphNode* Node : Nodes)
        {
            if (Node->NodeComment.IsEmpty()) { continue; }
            TArray<FString> Members;
            if (const auto* Comment = Cast<UEdGraphNode_Comment>(Node))
            {
                for (UEdGraphNode* Other : Nodes)
                {
                    const bool bInside = Other->NodePosX >= Node->NodePosX && Other->NodePosY >= Node->NodePosY
                        && static_cast<int64>(Other->NodePosX) < static_cast<int64>(Node->NodePosX) + Node->NodeWidth
                        && static_cast<int64>(Other->NodePosY) < static_cast<int64>(Node->NodePosY) + Node->NodeHeight;
                    if (Other != Node && Shown.Contains(Other) && (Comment->GetNodesUnderComment().Contains(Other) || bInside))
                    { Members.Add(FString::Printf(TEXT("#%d"), Refs[Other])); }
                }
            }
            else if (Shown.Contains(Node)) { Members.Add(FString::Printf(TEXT("#%d"), Refs[Node])); }
            if (Members.IsEmpty()) { continue; }
            const FString Part = TEXT(" \"") + Safe(Node->NodeComment, 100) + TEXT("\"[") + Safe(FString::Join(Members, TEXT(",")), 80) + TEXT("]");
            if (Result.Len() + Part.Len() > 260) { Result += TEXT(" ...[comments truncated]"); break; }
            Result += Part;
        }
        return Result.IsEmpty() ? FString() : TEXT("\nCOMMENTS") + Result;
    }
}
bool FKHUGraphDigest::ObjectPath(const FString& AssetPath, FString& Path)
{
    if (!AssetPath.StartsWith(TEXT("/Game/")) || AssetPath.Len() > 512 || AssetPath.Contains(TEXT(".."))
        || AssetPath.Contains(TEXT(":")) || AssetPath.Contains(TEXT("\\")) || AssetPath.Contains(TEXT("\r")) || AssetPath.Contains(TEXT("\n"))) { return false; }
    Path = AssetPath;
    if (!Path.Contains(TEXT("."))) { int32 Slash = INDEX_NONE; Path.FindLastChar(TEXT('/'), Slash); if (Slash == Path.Len() - 1) { return false; } Path += TEXT(".") + Path.Mid(Slash + 1); }
    return true;
}
UBlueprint* FKHUGraphDigest::FindLoadedBlueprint(const FString& AssetPath, FString& Issue)
{
    FString Path;
    if (!ObjectPath(AssetPath, Path)) { Issue = TEXT("use a loaded /Game/ Blueprint path"); return nullptr; }
    auto* Blueprint = FindObject<UBlueprint>(nullptr, *Path);
    if (!Blueprint) { Issue = TEXT("Blueprint is not loaded; open it first"); }
    return Blueprint;
}
bool FKHUGraphDigest::HasDiagnostic(const UEdGraphNode* Node)
{
    return Node && Node->bHasCompilerMessage && !Node->ErrorMsg.IsEmpty() && (Node->ErrorType == EMessageSeverity::Error
        || Node->ErrorType == EMessageSeverity::Warning || Node->ErrorType == EMessageSeverity::PerformanceWarning);
}
UEdGraph* FKHUGraphDigest::ChooseGraph(UBlueprint* Blueprint, const FString& GraphName)
{
    if (!Blueprint) { return nullptr; }
    TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs); Graphs.RemoveAll([](UEdGraph* Graph) { return !Graph; });
    Graphs.Sort([](const UEdGraph& A, const UEdGraph& B) { const int32 Order = A.GetName().Compare(B.GetName(), ESearchCase::CaseSensitive); return Order ? Order < 0 : A.GetPathName() < B.GetPathName(); });
    if (!GraphName.IsEmpty())
    {
        UEdGraph* Found = nullptr;
        for (UEdGraph* Graph : Graphs) { if (Graph->GetName().Equals(GraphName, ESearchCase::CaseSensitive)) { if (Found && Found != Graph) { return nullptr; } Found = Graph; } }
        return Found;
    }
    for (UEdGraph* Graph : Graphs) { for (UEdGraphNode* Node : Graph->Nodes) { if (HasDiagnostic(Node)) { return Graph; } } }
    for (UEdGraph* Graph : Graphs) { if (Graph->GetName() == TEXT("EventGraph")) { return Graph; } }
    return Graphs.IsEmpty() ? nullptr : Graphs[0];
}
TArray<UEdGraphNode*> FKHUGraphDigest::SelectedNodes(UBlueprint* Blueprint, UEdGraph* Graph)
{
    TArray<UEdGraphNode*> Nodes;
    const auto Editor = FKismetEditorUtilities::GetIBlueprintEditorForObject(Blueprint, false);
    if (Editor.IsValid() && Editor->GetFocusedGraph() == Graph)
    {
        const FName Kind = Editor->GetToolkitFName();
        if (Kind != TEXT("BlueprintEditor") && Kind != TEXT("WidgetBlueprintEditor")) { return Nodes; }
        // Built-in Blueprint and Widget editors derive from FBlueprintEditor.
        const auto BlueprintEditor = StaticCastSharedPtr<FBlueprintEditor>(Editor);
        for (UObject* Object : BlueprintEditor->GetSelectedNodes()) { if (auto* Node = Cast<UEdGraphNode>(Object)) { if (Node->GetGraph() == Graph) { Nodes.Add(Node); } } }
    }
    return Nodes;
}
FString FKHUGraphDigest::Build(UBlueprint* Blueprint, UEdGraph* Graph, FKHUGraphDigestOptions Options)
{
    check(IsInGameThread());
    if (!Blueprint || !Graph) { return TEXT("ERROR: Blueprint or graph is unavailable"); }
    if (Options.Focus != TEXT("errors") && Options.Focus != TEXT("selection") && Options.Focus != TEXT("all")) { return TEXT("ERROR: Focus must be errors, selection, or all"); }
    TArray<UEdGraphNode*> Nodes; FString Issue;
    if (!FKHUNodeRefCache::SortedNodes(Graph, Nodes, Issue)) { return TEXT("ERROR: ") + Issue; }
    Options.MaxNodes = FMath::Clamp(Options.MaxNodes, 1, 60); Options.Hops = FMath::Clamp(Options.Hops, 0, 4);
    TMap<UEdGraphNode*, int32> Refs; for (int32 I = 0; I < Nodes.Num(); ++I) { Refs.Add(Nodes[I], I + 1); }
    TArray<UEdGraphNode*> Seeds;
    for (UEdGraphNode* Node : Nodes)
    {
        if (Options.Focus == TEXT("all") || (Options.Focus == TEXT("errors") && HasDiagnostic(Node))
            || (Options.Focus == TEXT("selection") && Options.Selection.Contains(Node))) { Seeds.Add(Node); }
    }
    TMap<UEdGraphNode*, int32> Distances; TArray<UEdGraphNode*> Queue;
    for (UEdGraphNode* Node : Seeds) { Distances.Add(Node, 0); Queue.Add(Node); }
    for (int32 I = 0; I < Queue.Num(); ++I)
    {
        UEdGraphNode* Node = Queue[I]; const int32 Distance = Distances[Node];
        if (Distance >= Options.Hops) { continue; }
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin) { continue; }
            for (UEdGraphPin* Link : Pin->LinkedTo)
            {
                UEdGraphNode* Other = Link ? Link->GetOwningNodeUnchecked() : nullptr;
                if (Other && Refs.Contains(Other) && !Distances.Contains(Other)) { Distances.Add(Other, Distance + 1); Queue.Add(Other); }
            }
        }
    }
    TArray<UEdGraphNode*> Candidates;
    for (UEdGraphNode* Node : Nodes) { if (Distances.Contains(Node)) { Candidates.Add(Node); } }
    Candidates.Sort([&](const UEdGraphNode& A, const UEdGraphNode& B)
    {
        const int32 DA = Distances[const_cast<UEdGraphNode*>(&A)], DB = Distances[const_cast<UEdGraphNode*>(&B)];
        if (DA != DB) { return DA < DB; }
        const bool EA = HasDiagnostic(&A) && A.ErrorType == EMessageSeverity::Error, EB = HasDiagnostic(&B) && B.ErrorType == EMessageSeverity::Error;
        return EA != EB ? EA : Refs[const_cast<UEdGraphNode*>(&A)] < Refs[const_cast<UEdGraphNode*>(&B)];
    });
    const int32 Eligible = Candidates.Num(); if (Candidates.Num() > Options.MaxNodes) { Candidates.SetNum(Options.MaxNodes); }
    Candidates.Sort([&](const UEdGraphNode& A, const UEdGraphNode& B) { return Refs[const_cast<UEdGraphNode*>(&A)] < Refs[const_cast<UEdGraphNode*>(&B)]; });
    FString Asset = Blueprint->GetPathName(), PackagePath;
    if (Asset.Split(TEXT("."), &PackagePath, nullptr)) { Asset = MoveTemp(PackagePath); }
    FString Result = FString::Printf(TEXT("GRAPH %s:%s nodes=%d shown=%d focus=%s hops=%d"), *Safe(Asset, 180), *Safe(Graph->GetName(), 80), Nodes.Num(), Candidates.Num(), *Options.Focus, Options.Hops);
    const int32 Budget = FMath::Clamp(4900 / FMath::Max(1, Candidates.Num()) - 1, 64, 1200); TSet<UEdGraphNode*> Shown;
    for (UEdGraphNode* Node : Candidates) { Shown.Add(Node); Result += TEXT("\n") + Row(Blueprint, Node, Refs, Budget); }
    if (Eligible > Candidates.Num()) { Result += FString::Printf(TEXT("\n...%d more nodes (ask with Focus=all, MaxNodes=60)"), Eligible - Candidates.Num()); }
    if (Candidates.IsEmpty()) { Result += Options.Focus == TEXT("errors") ? TEXT("\nNO_NODE_DIAGNOSTICS (use Focus=all)") : TEXT("\nNO_SELECTED_NODES"); }
    Result += Variables(Blueprint, Graph, Nodes) + Comments(Nodes, Shown, Refs);
    Result += TEXT("\nLEGEND !UNCONNECTED=required input not connected; none=output not used");
    FKHUNodeRefCache::Get().Publish(Blueprint, Graph, Nodes);
    return FKHULogDigest::Bound(Result);
}
