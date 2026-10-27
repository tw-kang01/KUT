#pragma once
#include "AI/KHUChatClient.h"

/** Mock model, real read-only evidence. Used only by the explicit test-mode switch. */
class FKHUPanelMock final : public IKHUChatClient, public TSharedFromThis<FKHUPanelMock>
{
public:
    virtual void Send(FKHUChatRequest Request, FCallback Callback) override;
    virtual void Cancel() override { ++Generation; }
    virtual bool IsMock() const override { return true; }
private:
    uint64 Generation = 0;
};
