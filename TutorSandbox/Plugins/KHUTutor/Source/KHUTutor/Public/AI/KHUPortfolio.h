#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FKHUSavedConversation;
/** Student-edited record; confirmation applies to this exact revision, not to factual correctness. */
struct KHUTUTOR_API FKHUPortfolio
{
    FString Body, Evidence, Revision;
    TArray<FString> Sources;
    int32 Hint = 1;
    bool bConfirmed = false;
    /** Validate persisted optional data without trusting model or file content. */
    static bool Valid(const TSharedPtr<FJsonObject>& Object);
    /** Read a validated record. */
    static FKHUPortfolio Read(const TSharedPtr<FJsonObject>& Object);
    /** Serialize a record, redacting known credentials and personal paths. */
    TSharedRef<FJsonObject> Json() const;
    /** Build an explicitly unverified scaffold from selected visible original turns. */
    static FKHUPortfolio Draft(const FKHUSavedConversation& Conversation, const TArray<FString>& Sources, int32 Hint);
    /** Return the concise student-facing request; source data and model instructions travel separately. */
    static FString Question();
    /** Display a known legacy generated request as a readable question without modifying stored text. */
    static FString DisplayQuestion(const FString& Text);
    /** Display a legacy auto-title only when its original generated request matches; preserve renamed titles. */
    static FString DisplayTitle(const FKHUSavedConversation& Conversation);
    /** Prepare internal retrospective context without untouched scaffolding; preserve student edits and sources. */
    FString Prompt(const FKHUSavedConversation& Conversation) const;
    /** Remove known secrets while retaining paragraphs; not a complete publication privacy review. */
    static FString Clean(const FString& Text);
};
