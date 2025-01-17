/*
 * Copyright 2018-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <thrift/lib/cpp2/async/PooledRequestChannel.h>

#include <thrift/lib/cpp2/async/FutureRequest.h>

#include <folly/futures/Future.h>

namespace apache {
namespace thrift {

uint16_t PooledRequestChannel::getProtocolId() {
  auto executor = executor_.lock();
  if (!executor) {
    throw std::logic_error("IO executor already destroyed.");
  }
  folly::call_once(protocolIdInitFlag_, [&] {
    auto evb = executor->getEventBase();
    evb->runImmediatelyOrRunInEventBaseThreadAndWait(
        [&] { protocolId_ = impl(*evb).getProtocolId(); });
  });

  return protocolId_;
}

uint32_t PooledRequestChannel::sendRequestImpl(
    RpcKind rpcKind,
    RpcOptions& options,
    std::unique_ptr<RequestCallback> cob,
    std::unique_ptr<ContextStack> ctx,
    std::unique_ptr<folly::IOBuf> buf,
    std::shared_ptr<transport::THeader> header) {
  auto executor = executor_.lock();
  if (!executor) {
    throw std::logic_error("IO executor already destroyed.");
  }
  auto evb = executor->getEventBase();

  evb->runInEventBaseThread([this,
                             keepAlive = getKeepAliveToken(evb),
                             options = std::move(options),
                             rpcKind,
                             cob = std::move(cob),
                             ctx = std::move(ctx),
                             buf = std::move(buf),
                             header = std::move(header)]() mutable {
    switch (rpcKind) {
      case RpcKind::SINGLE_REQUEST_NO_RESPONSE:
        impl(*keepAlive)
            .sendOnewayRequest(
                options,
                std::move(cob),
                std::move(ctx),
                std::move(buf),
                std::move(header));
        break;
      case RpcKind::SINGLE_REQUEST_SINGLE_RESPONSE:
        impl(*keepAlive)
            .sendRequest(
                options,
                std::move(cob),
                std::move(ctx),
                std::move(buf),
                std::move(header));
        break;
      case RpcKind::SINGLE_REQUEST_STREAMING_RESPONSE:
        impl(*keepAlive)
            .sendStreamRequest(
                options,
                std::move(cob),
                std::move(ctx),
                std::move(buf),
                std::move(header));
        break;
      default:
        folly::assume_unreachable();
        break;
    };
  });
  return 0;
}

namespace {
class ExecutorRequestCallback final : public RequestCallback {
 public:
  ExecutorRequestCallback(
      std::unique_ptr<RequestCallback> cb,
      folly::Executor::KeepAlive<> executorKeepAlive)
      : executorKeepAlive_(std::move(executorKeepAlive)), cb_(std::move(cb)) {
    CHECK(executorKeepAlive_);
  }

  void requestSent() override {
    executorKeepAlive_.get()->add([cb = cb_] { cb->requestSent(); });
  }
  void replyReceived(ClientReceiveState&& rs) override {
    executorKeepAlive_.get()->add(
        [cb = std::move(cb_), rs = std::move(rs)]() mutable {
          cb->replyReceived(std::move(rs));
        });
  }
  void requestError(ClientReceiveState&& rs) override {
    executorKeepAlive_.get()->add(
        [cb = std::move(cb_), rs = std::move(rs)]() mutable {
          cb->requestError(std::move(rs));
        });
  }

 private:
  folly::Executor::KeepAlive<> executorKeepAlive_;
  std::shared_ptr<RequestCallback> cb_;
};
} // namespace

uint32_t PooledRequestChannel::sendRequest(
    RpcOptions& options,
    std::unique_ptr<RequestCallback> cob,
    std::unique_ptr<ContextStack> ctx,
    std::unique_ptr<folly::IOBuf> buf,
    std::shared_ptr<transport::THeader> header) {
  if (!cob->isInlineSafe()) {
    cob = std::make_unique<ExecutorRequestCallback>(
        std::move(cob), getKeepAliveToken(callbackExecutor_));
  }
  sendRequestImpl(
      RpcKind::SINGLE_REQUEST_SINGLE_RESPONSE,
      options,
      std::move(cob),
      std::move(ctx),
      std::move(buf),
      std::move(header));
  return 0;
}

uint32_t PooledRequestChannel::sendOnewayRequest(
    RpcOptions& options,
    std::unique_ptr<RequestCallback> cob,
    std::unique_ptr<ContextStack> ctx,
    std::unique_ptr<folly::IOBuf> buf,
    std::shared_ptr<transport::THeader> header) {
  if (!cob->isInlineSafe()) {
    cob = std::make_unique<ExecutorRequestCallback>(
        std::move(cob), getKeepAliveToken(callbackExecutor_));
  }
  sendRequestImpl(
      RpcKind::SINGLE_REQUEST_NO_RESPONSE,
      options,
      std::move(cob),
      std::move(ctx),
      std::move(buf),
      std::move(header));
  return 0;
}

uint32_t PooledRequestChannel::sendStreamRequest(
    RpcOptions& options,
    std::unique_ptr<RequestCallback> cob,
    std::unique_ptr<ContextStack> ctx,
    std::unique_ptr<folly::IOBuf> buf,
    std::shared_ptr<transport::THeader> header) {
  if (!cob->isInlineSafe()) {
    cob = std::make_unique<ExecutorRequestCallback>(
        std::move(cob), getKeepAliveToken(callbackExecutor_));
  }
  sendRequestImpl(
      RpcKind::SINGLE_REQUEST_STREAMING_RESPONSE,
      options,
      std::move(cob),
      std::move(ctx),
      std::move(buf),
      std::move(header));
  return 0;
}

PooledRequestChannel::Impl& PooledRequestChannel::impl(folly::EventBase& evb) {
  DCHECK(evb.inRunningEventBaseThread());

  return impl_.getOrCreateFn(evb, [this, &evb] { return implCreator_(evb); });
}
} // namespace thrift
} // namespace apache
