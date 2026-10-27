#pragma once
#include "GameFramework/Actor.h"
#include "UObject/Interface.h"
#include "KHUInspectionTestActor.generated.h"

/** Reflection fixture only; never spawned into user content. */
UINTERFACE()
class UKHUInspectionTestInterface : public UInterface { GENERATED_BODY() };
class IKHUInspectionTestInterface { GENERATED_BODY() };

/** Transient fixture for actor/reference/default snapshots. */
UCLASS(Transient, NotPlaceable)
class AKHUInspectionTestActor : public AActor, public IKHUInspectionTestInterface
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Test") int32 DoorCode = 7;
    UPROPERTY(EditAnywhere, Category="Test") TObjectPtr<AActor> DoorTarget;
    UPROPERTY(EditAnywhere, Category="Test") TArray<TObjectPtr<AActor>> Doors;
    UPROPERTY(EditAnywhere, Category="Test") TScriptInterface<IKHUInspectionTestInterface> DoorInterface;
    UPROPERTY(EditAnywhere, Category="Test") FString ApiKey = TEXT("fixture-private-value");
    UPROPERTY(EditAnywhere, Category="Test") FString Note;
};
