#include "Sensors/KHUObjectInspector.h"
#include "Tests/KHUInspectionTestActor.h"
#include "Toolsets/KHUTutorReadToolset.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FWorldFixture
    {
        UWorld* World;
        FWorldFixture()
        {
            const auto IV = UWorld::InitializationValues().CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Editor, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &IV);
        }
        ~FWorldFixture() { World->DestroyWorld(false); }
        AKHUInspectionTestActor* Actor()
        {
            FActorSpawnParameters P; P.ObjectFlags = RF_Transient; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            return World->SpawnActor<AKHUInspectionTestActor>(P);
        }
    };
    TSharedPtr<FJsonObject> Parse(const FString& Text)
    { TSharedPtr<FJsonObject> J; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J); return J; }
    TSharedPtr<FJsonObject> Property(const TSharedPtr<FJsonObject>& J, const FString& Name)
    {
        if (!J) { return {}; }
        for (const auto& V : J->GetArrayField(TEXT("properties"))) { if (V->AsObject()->GetStringField(TEXT("property")) == Name) { return V->AsObject(); } }
        return {};
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FX1WorldIdentity, "KHUTutor.X1.WorldAndExactIdentity", Flags)
bool FX1WorldIdentity::RunTest(const FString&)
{
    FWorldFixture F, Other; auto* A = F.Actor(); auto* B = F.Actor(); A->SetActorLabel(TEXT("SameDoor")); B->SetActorLabel(TEXT("SameDoor"));
    FString Issue;
    TestTrue(TEXT("Same-label actor A uses exact identity"), FKHUObjectInspector::Find(F.World, A->GetPathName(), Issue) == A);
    TestTrue(TEXT("Same-label actor B uses exact identity"), FKHUObjectInspector::Find(F.World, B->GetPathName(), Issue) == B);
    TestNull(TEXT("Label is not an identity"), FKHUObjectInspector::Find(F.World, TEXT("SameDoor"), Issue));
    TestNull(TEXT("No cross-world fallback"), FKHUObjectInspector::Find(Other.World, A->GetPathName(), Issue));
    TestTrue(TEXT("Mixed roots rejected"), FKHUObjectInspector::Relations(Other.World, {A}, 1, 4).StartsWith(TEXT("ERROR:")));
    TestTrue(TEXT("Unknown scope rejected"), UKHUTutorReadToolset::GetActorRelations(TEXT(""), TEXT("Auto"), 1, 4).StartsWith(TEXT("ERROR:")));
    TestTrue(TEXT("World-scoped details reject class defaults"), FKHUObjectInspector::Details(A->GetClass()->GetDefaultObject(false), F.World, TEXT("DoorCode"), 1).StartsWith(TEXT("ERROR:")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FX1Relations, "KHUTutor.X1.ReferenceChainCycleAndNone", Flags)
bool FX1Relations::RunTest(const FString&)
{
    FWorldFixture F; auto* Switch = F.Actor(); auto* Door = F.Actor();
    Switch->DoorTarget = Door; Door->DoorTarget = Switch; Switch->Doors = {Door, nullptr};
    Switch->DoorInterface.SetObject(Door); Switch->DoorInterface.SetInterface(Cast<IKHUInspectionTestInterface>(Door));
    const FString Text = FKHUObjectInspector::Relations(F.World, {Switch}, 2, 8); auto J = Parse(Text);
    TestTrue(TEXT("Bounded JSON relation snapshot"), J.IsValid() && Text.Len() <= 6000);
    if (J)
    {
        TestEqual(TEXT("Cycle visits each object once"), J->GetArrayField(TEXT("objects")).Num(), 2);
        TestTrue(TEXT("Exact target retained"), Text.Contains(Door->GetPathName()));
        TestTrue(TEXT("None is explicit"), Text.Contains(TEXT("\"state\":\"None\"")));
        TestTrue(TEXT("Array reference retained"), Text.Contains(TEXT("Doors[0]")));
        TestTrue(TEXT("Interface observation retained"), Text.Contains(TEXT("\"implements_interface\":true")));
    }
    auto DepthZero = Parse(FKHUObjectInspector::Relations(F.World, {Switch}, 0, 8));
    TestEqual(TEXT("Depth zero does not follow references"), DepthZero->GetArrayField(TEXT("objects")).Num(), 1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FX1Defaults, "KHUTutor.X1.DefaultsComponentsAndUnsupported", Flags)
bool FX1Defaults::RunTest(const FString&)
{
    FWorldFixture F; auto* A = F.Actor(); A->DoorCode = 42;
    auto* C = NewObject<USceneComponent>(A, TEXT("FixtureComponent"), RF_Transient); A->AddInstanceComponent(C);
    auto J = Parse(FKHUObjectInspector::Details(A, F.World, TEXT("DoorCode,Doors,ApiKey"), 12));
    auto Code = Property(J, TEXT("DoorCode")); auto Array = Property(J, TEXT("Doors")); auto Key = Property(J, TEXT("ApiKey"));
    TestTrue(TEXT("Concrete instance and default values"), Code && Code->GetStringField(TEXT("value")) == TEXT("42") && Code->GetStringField(TEXT("class_default")) == TEXT("7"));
    TestTrue(TEXT("Difference recorded without claiming a saved override"), Code && Code->GetBoolField(TEXT("differs_from_class_default")));
    TestTrue(TEXT("Container not fabricated as None"), Array && Array->GetStringField(TEXT("status")) == TEXT("not_read"));
    TestTrue(TEXT("Sensitive property not read"), Key && !Key->GetStringField(TEXT("value")).Contains(TEXT("fixture-private-value")));
    auto CD = Parse(FKHUObjectInspector::Details(A->GetClass()->GetDefaultObject(false), nullptr, TEXT("DoorCode"), 1));
    TestEqual(TEXT("Explicit class default scope"), CD->GetStringField(TEXT("scope")), FString(TEXT("ClassDefault")));
    auto Component = Parse(FKHUObjectInspector::Details(C, F.World, TEXT("RelativeLocation"), 1));
    TestTrue(TEXT("Component owner provenance"), Component && Component->GetObjectField(TEXT("object"))->GetStringField(TEXT("owner")) == A->GetPathName());
    TestEqual(TEXT("Read leaves value untouched"), A->DoorCode, 42);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FX1Bounds, "KHUTutor.X1.BoundsAndFreshObservation", Flags)
bool FX1Bounds::RunTest(const FString&)
{
    FWorldFixture F; auto* A = F.Actor();
    for (int32 I = 0; I < 12; ++I) { A->Doors.Add(F.Actor()); }
    FString Before = FKHUObjectInspector::Relations(F.World, {A}, 99, 99); auto J = Parse(Before);
    TestTrue(TEXT("Oversized request remains bounded valid JSON"), J && Before.Len() <= 6000);
    TestTrue(TEXT("Truncation is explicit"), J && J->GetBoolField(TEXT("truncated")));
    if (J) { TestTrue(TEXT("Object cap enforced"), J->GetArrayField(TEXT("objects")).Num() <= 8); }
    A->Note = FString::ChrN(10000, TCHAR('x'));
    const FString Details = FKHUObjectInspector::Details(A, F.World, TEXT("Note"), 99);
    TestTrue(TEXT("Long scalar is bounded JSON"), Parse(Details).IsValid() && Details.Len() <= 6000);
    A->DoorCode = 99;
    auto Fresh = Property(Parse(FKHUObjectInspector::Details(A, F.World, TEXT("DoorCode"), 1)), TEXT("DoorCode"));
    TestTrue(TEXT("Reread observes current value"), Fresh && Fresh->GetStringField(TEXT("value")) == TEXT("99"));
    TestTrue(TEXT("Missing property is an error"), FKHUObjectInspector::Details(A, F.World, TEXT("DoesNotExist"), 1).StartsWith(TEXT("ERROR:")));
    return true;
}
#endif
