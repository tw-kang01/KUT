#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** A source is supplied by the application, never created by model metadata. */
struct KHUTUTOR_API FKHUCoachSource
{
    FString Id, Text, Kind;
    bool bCurrent = false;
};

/** Bounded optional review. No field represents mastery or completed learning. */
class KHUTUTOR_API FKHULearningCoach
{
public:
    TSharedPtr<FJsonObject> Review;
    TArray<TSharedPtr<FJsonValue>> Events;
    bool bPaused = false;
    /** Strip the reserved trailer, validate references, then consider one suggestion. */
    FString Consume(const FString& Raw, int32 Hint, const TArray<FKHUCoachSource>& Sources, const FString& PriorAI);
    /** Explicit UI choice only; does not report that a student performed the action. */
    bool Choose(const FString& Choice);
    /** Serialize application-owned state into the existing conversation archive. */
    TSharedRef<FJsonObject> Save() const;
    /** Load optional state; invalid state fails closed, preserving the original archive. */
    bool Restore(const TSharedPtr<FJsonObject>& Data);
    /** Validate untrusted saved state without side effects. */
    static bool Valid(const TSharedPtr<FJsonObject>& Data);
    /** Safe context contains statuses only at a lower hint, never old solution text. */
    FString Context(int32 Hint) const;
    /** Latest explicit student control; a newer proposal or actual report supersedes an older choice. */
    FString Control() const;
    /** Application-owned system note for the current control; empty when no control applies. */
    FString ControlNote() const;
    /** Korean review: the student's own words at any level, tool quotes only at L3, closed guidance text. */
    FString Describe(int32 Hint) const;
    /** Pause and hidden-by-hint status shown beside the review; never review content. */
    FString Notice(int32 Hint) const;
    /** One action and its reason, suitable for a small card in the reply flow. */
    FString Action(int32 Hint) const;
    /** Identity protects old buttons from modifying newer proposals. */
    FString Id() const;
    bool Visible(int32 Hint) const;
private:
    // Delivery status records protocol availability only, never a diagnosis or a student action.
    FString Delivery, DeliveryId;
    int32 DeliveryHint = 1;
};
