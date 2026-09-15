#include "a2functional.h"
#include <cstddef>
#include <memory>
#include "WebSocketSessionMan.h"

#include "a2doctest.h"

#include "DownloadEngine.h"
#include "Option.h"
#include "RequestGroup.h"
#include "SelectEventPoll.h"
#include "SocketCore.h"
#include "WebSocketSession.h"
#include "prefs.h"

namespace aria2 {

namespace rpc {

class WebSocketSessionManTest {
protected:
  std::shared_ptr<Option> option_;
  std::shared_ptr<DownloadEngine> e_;

public:
  WebSocketSessionManTest()
  {
    option_ = std::make_shared<Option>();
    e_ = make_unique<DownloadEngine>(make_unique<SelectEventPoll>());
    e_->setOption(option_.get());
  }
};

TEST_CASE_FIXTURE(WebSocketSessionManTest,
                  "WebSocketSessionManTest."
                  "testSessionRequiresAuthorizationWhenRpcSecretIsSet")
{
  option_->put(PREF_RPC_SECRET, "secret");

  auto session = std::make_shared<WebSocketSession>(
      std::make_shared<SocketCore>(), e_.get());

  REQUIRE(!session->isAuthorized());
}

TEST_CASE_FIXTURE(WebSocketSessionManTest,
                  "WebSocketSessionManTest."
                  "testNotificationRecipientsExcludeUnauthorizedSessions")
{
  option_->put(PREF_RPC_SECRET, "secret");

  WebSocketSessionMan sessionMan;
  auto unauthorizedSession = std::make_shared<WebSocketSession>(
      std::make_shared<SocketCore>(), e_.get());
  auto authorizedSession = std::make_shared<WebSocketSession>(
      std::make_shared<SocketCore>(), e_.get());
  authorizedSession->markAuthorized();
  sessionMan.addSession(unauthorizedSession);
  sessionMan.addSession(authorizedSession);

  REQUIRE_EQ((size_t)1, sessionMan.countNotificationRecipients());
}

} // namespace rpc

} // namespace aria2
