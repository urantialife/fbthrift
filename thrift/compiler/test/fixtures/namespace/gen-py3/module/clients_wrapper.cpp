/**
 * Autogenerated by Thrift
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */

#include <src/gen-py3/module/clients_wrapper.h>

namespace cpp2 {


TestServiceClientWrapper::TestServiceClientWrapper(
    std::shared_ptr<::cpp2::TestServiceAsyncClient> async_client,
    std::shared_ptr<apache::thrift::RequestChannel> channel) : 
    async_client(async_client),
      channel_(channel) {}

TestServiceClientWrapper::~TestServiceClientWrapper() {}

folly::Future<folly::Unit> TestServiceClientWrapper::disconnect() {
  return folly::via(
    this->async_client->getChannel()->getEventBase(),
    [cha = std::move(channel_), cli = std::move(async_client)] {});
}

void TestServiceClientWrapper::setPersistentHeader(const std::string& key, const std::string& value) {
    auto headerChannel = async_client->getHeaderChannel();
    if (headerChannel != nullptr) {
        headerChannel->setPersistentHeader(key, value);
    }
}


folly::Future<int64_t>
TestServiceClientWrapper::init(
    apache::thrift::RpcOptions& rpcOptions,
    int64_t arg_int1) {
  folly::Promise<int64_t> _promise;
  auto _future = _promise.getFuture();
  auto callback = std::make_unique<::thrift::py3::FutureCallback<int64_t>>(
    std::move(_promise), rpcOptions, async_client->recv_wrapped_init, channel_);
  async_client->init(
    rpcOptions,
    std::move(callback),
    arg_int1
  );
  return _future;
}


} // namespace cpp2
