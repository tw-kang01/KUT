#pragma once
#include "AI/KHUConfig.h"
#include "AI/KHUPortfolio.h"

struct KHUTUTOR_API FKHULearningFolder
{
    FString Id, Title;
    bool bArchived = false;
};
struct KHUTUTOR_API FKHUSavedConversation
{
    FString Id, Title, Folder, Scope, Goal, Updated, LegacySummary;
    int32 Hint = 1, Session = 1, NextTurn = 1, HiddenReplies = 0;
    bool bArchived = false, bOwnArchived = false, bFolderArchived = false, bLegacy = false;
    bool bHasBudget = false, bCostKnown = true;
    double Spent = 0, Allowance = 0;
    TArray<TSharedPtr<FJsonValue>> Turns;
    TSharedPtr<FJsonObject> Coach;
    TSharedPtr<FJsonObject> Portfolio;
    bool bHasPortfolio = false, bPortfolioHidden = false;
};

/** Durable, redacted conversation archive with bounded retrieval. History is never current editor evidence. */
class KHUTUTOR_API FKHUProjectMemory
{
public:
    FKHUProjectMemory(FString FilePath, FString ProjectIdentity, FKHUContextPolicy Policy);
    /** Merge one logical task, retaining other panels' entries. Returns false on persistence failure. */
    bool SaveTask(const FString& Task, const FString& Scope, const FString& Goal, const FString& Student,
        const FString& Suggestion, int32 Hint, int32 Session, bool bSolved = false);
    /** Persist an accepted student turn or final visible AI reply without truncating its text. Idempotent by turn id. */
    bool RecordTurn(const FString& Task, const FString& Scope, const FString& Goal, const FString& Role,
        const FString& Text, const FString& TurnId, int32 Hint, int32 Session);
    /** Save a student-pinned goal/decision/completed/attempt/open/next note. Replacing a kind keeps its old archive entry. */
    bool Remember(const FString& Scope, const FString& Kind, const FString& Text, int32 Hint);
    /** Query matching asset and project-wide notes; transmit a bounded selection, never the entire archive. */
    FString Recall(const FString& Scope, int32 Hint, const FString& Query = TEXT(""), const FString& CurrentTask = TEXT("")) const;
    /** Render a cached, plain Korean view without loading/parsing the full JSON on every Slate frame. */
    FString Describe(const FString& Scope, int32 Hint) const;
    /** List folders and conversation metadata in this Unreal project without sending data to a model. */
    bool ListConversations(TArray<FKHULearningFolder>& Folders, TArray<FKHUSavedConversation>& Conversations) const;
    /** Read original stored messages for display; higher-hint assistant replies remain hidden. */
    bool ReadConversation(const FString& Id, int32 Hint, FKHUSavedConversation& Conversation) const;
    /** Create a user-named learning folder; no physical project or asset folders are changed. */
    bool CreateFolder(const FString& Title, FString& Id);
    /** Rename or reversibly archive a learning folder. */
    bool EditFolder(const FString& Id, const FString& Title, bool bArchived);
    /** Create an empty persistent conversation in the current editor asset scope. */
    bool CreateConversation(const FString& Scope, const FString& Folder, int32 Hint, FString& Id);
    /** Rename, move or reversibly archive a conversation without altering its messages or asset scope. */
    bool EditConversation(const FString& Id, const FString& Title, const FString& Folder, bool bArchived);
    /** Retain the task's existing credit limit across closing and reopening its conversation. */
    bool SaveConversationBudget(const FString& Id, double Spent, double Allowance, bool bCostKnown);
    /** Persist validated coaching state within this exact conversation and asset. */
    bool SaveCoach(const FString& Id, const FString& Scope, const TSharedRef<FJsonObject>& State);
    /** Save an exact-scope record with optimistic revision checking; never overwrite hidden or stale drafts. */
    bool SavePortfolio(const FString& Id, const FString& Scope, int32 Hint, FKHUPortfolio& Record);
    /** Sanitized storage/retrieval failure for UI; missing history must not masquerade as successful memory. */
    FString Error() const { return LastError; }
    /** Forget this project's local progress; never touches credentials or assets. */
    bool Forget();
    /** Make short, redacted text without attached graph observations. */
    static FString Excerpt(const FString& Text, int32 MaxChars);
private:
    bool Read(TArray<TSharedPtr<FJsonValue>>& Entries, TArray<TSharedPtr<FJsonValue>>& Notes) const;
    bool Write(const TArray<TSharedPtr<FJsonValue>>& Entries, const TArray<TSharedPtr<FJsonValue>>& Notes, TSharedPtr<FJsonObject> Library = nullptr);
    bool ValidLibrary(const TSharedRef<FJsonObject>& Library, const TSet<FString>& Tasks) const;
    TSharedRef<FJsonObject> LibraryCopy() const;
    TSharedRef<FJsonObject> Entry(const FString& Task, const FString& Scope, const FString& Goal,
        int32 Hint, int32 Session, TArray<TSharedPtr<FJsonValue>>& Entries) const;
    FString Path, Project;
    FKHUContextPolicy Policy;
    mutable FString LastError;
    mutable TSharedPtr<FJsonObject> Cache;
    mutable FDateTime CacheStamp;
    mutable int64 CacheBytes = -1;
};
