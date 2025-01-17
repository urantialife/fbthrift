/*
 * Copyright 2004-present Facebook, Inc.
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

#include <thrift/lib/cpp2/server/Cpp2Worker.h>

#include <glog/logging.h>

#include <folly/String.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/portability/Sockets.h>
#include <thrift/lib/cpp/async/TAsyncSSLSocket.h>
#include <thrift/lib/cpp/async/TAsyncSocket.h>
#include <thrift/lib/cpp/concurrency/Util.h>
#include <thrift/lib/cpp2/server/Cpp2Connection.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>
#include <thrift/lib/cpp2/server/peeking/PeekingManager.h>
#include <wangle/acceptor/EvbHandshakeHelper.h>
#include <wangle/acceptor/SSLAcceptorHandshakeHelper.h>
#include <wangle/acceptor/UnencryptedAcceptorHandshakeHelper.h>

namespace apache {
namespace thrift {

using namespace apache::thrift::server;
using namespace apache::thrift::transport;
using namespace apache::thrift::async;
using apache::thrift::concurrency::Util;
using std::shared_ptr;

void Cpp2Worker::onNewConnection(
    folly::AsyncTransportWrapper::UniquePtr sock,
    const folly::SocketAddress* addr,
    const std::string& nextProtocolName,
    wangle::SecureTransportType secureTransportType,
    const wangle::TransportInfo& tinfo) {
  auto* observer = server_->getObserver();
  uint32_t maxConnection = server_->getMaxConnections();
  if (maxConnection > 0 &&
      (getConnectionManager()->getNumConnections() >=
       maxConnection / server_->getNumIOWorkerThreads())) {
    if (observer) {
      observer->connDropped();
      observer->connRejected();
    }
    return;
  }

  // Check the security protocol
  switch (secureTransportType) {
    // If no security, peek into the socket to determine type
    case wangle::SecureTransportType::NONE: {
      auto peekingManager = new PeekingManager(
          shared_from_this(),
          *addr,
          nextProtocolName,
          secureTransportType,
          tinfo,
          server_);
      peekingManager->start(std::move(sock), server_->getObserverShared());
      break;
    }
    case wangle::SecureTransportType::TLS:
      // Use the announced protocol to determine the correct handler
      if (!nextProtocolName.empty()) {
        for (auto& routingHandler : *server_->getRoutingHandlers()) {
          if (routingHandler->canAcceptEncryptedConnection(nextProtocolName)) {
            VLOG(4) << "Cpp2Worker: Routing encrypted connection for protocol "
                    << nextProtocolName;
            routingHandler->handleConnection(
                getConnectionManager(),
                std::move(sock),
                addr,
                tinfo,
                shared_from_this());
            return;
          }
        }
      }
      // Default to header
      handleHeader(std::move(sock), addr);
      break;
    case wangle::SecureTransportType::ZERO:
      LOG(ERROR) << "Unsupported Secure Transport Type: ZERO";
      break;
    default:
      LOG(ERROR) << "Unsupported Secure Transport Type";
      break;
  }
}

void Cpp2Worker::handleHeader(
    folly::AsyncTransportWrapper::UniquePtr sock,
    const folly::SocketAddress* addr) {
  auto fd = sock->getUnderlyingTransport<folly::AsyncSocket>()
                ->getNetworkSocket()
                .toFd();
  VLOG(4) << "Cpp2Worker: Creating connection for socket " << fd;

  auto thriftTransport = createThriftTransport(std::move(sock));
  auto connection = std::make_shared<Cpp2Connection>(
      std::move(thriftTransport), addr, shared_from_this(), nullptr);
  Acceptor::addConnection(connection.get());
  connection->addConnection(connection);
  connection->start();

  VLOG(4) << "Cpp2Worker: created connection for socket " << fd;

  auto observer = server_->getObserver();
  if (observer) {
    observer->connAccepted();
    observer->activeConnections(
        getConnectionManager()->getNumConnections() *
        server_->getNumIOWorkerThreads());
  }
}

std::shared_ptr<async::TAsyncTransport> Cpp2Worker::createThriftTransport(
    folly::AsyncTransportWrapper::UniquePtr sock) {
  auto fizzServer = dynamic_cast<async::TAsyncFizzServer*>(sock.get());
  if (fizzServer) {
    auto asyncSock = sock->getUnderlyingTransport<async::TAsyncSocket>();
    if (asyncSock) {
      markSocketAccepted(asyncSock);
    }
    // give up ownership
    sock.release();
    return std::shared_ptr<async::TAsyncFizzServer>(
        fizzServer, async::TAsyncFizzServer::Destructor());
  }

  TAsyncSocket* tsock = dynamic_cast<TAsyncSocket*>(sock.release());
  CHECK(tsock);
  auto asyncSocket =
      std::shared_ptr<TAsyncSocket>(tsock, TAsyncSocket::Destructor());
  markSocketAccepted(asyncSocket.get());
  return asyncSocket;
}

void Cpp2Worker::markSocketAccepted(TAsyncSocket* sock) {
  sock->setIsAccepted(true);
  sock->setShutdownSocketSet(server_->wShutdownSocketSet_);
}

void Cpp2Worker::plaintextConnectionReady(
    folly::AsyncTransportWrapper::UniquePtr sock,
    const folly::SocketAddress& clientAddr,
    const std::string& nextProtocolName,
    wangle::SecureTransportType secureTransportType,
    wangle::TransportInfo& tinfo) {
  auto asyncSocket = sock->getUnderlyingTransport<folly::AsyncSocket>();
  CHECK(asyncSocket) << "Underlying socket is not a AsyncSocket type";
  asyncSocket->setShutdownSocketSet(server_->wShutdownSocketSet_);
  auto peekingManager = new PeekingManager(
      shared_from_this(),
      clientAddr,
      nextProtocolName,
      secureTransportType,
      tinfo,
      server_,
      /* checkTLS */ true);
  peekingManager->start(std::move(sock), server_->getObserverShared());
}

void Cpp2Worker::useExistingChannel(
    const std::shared_ptr<HeaderServerChannel>& serverChannel) {
  folly::SocketAddress address;

  auto conn = std::make_shared<Cpp2Connection>(
      nullptr, &address, shared_from_this(), serverChannel);
  Acceptor::getConnectionManager()->addConnection(conn.get(), false);
  conn->addConnection(conn);

  conn->start();
}

void Cpp2Worker::stopDuplex(std::shared_ptr<ThriftServer> myServer) {
  // They better have given us the correct ThriftServer
  DCHECK(server_ == myServer.get());

  // This does not really fully drain everything but at least
  // prevents the connections from accepting new requests
  wangle::Acceptor::drainAllConnections();

  // Capture a shared_ptr to our ThriftServer making sure it will outlive us
  // Otherwise our raw pointer to it (server_) will be jeopardized.
  duplexServer_ = myServer;
}

void Cpp2Worker::updateSSLStats(
    const folly::AsyncTransportWrapper* sock,
    std::chrono::milliseconds /* acceptLatency */,
    wangle::SSLErrorEnum error) noexcept {
  if (!sock) {
    return;
  }

  auto observer = getServer()->getObserver();
  if (!observer) {
    return;
  }

  auto fizz = sock->getUnderlyingTransport<fizz::server::AsyncFizzServer>();
  if (fizz) {
    if (sock->good() && error == wangle::SSLErrorEnum::NO_ERROR) {
      observer->tlsComplete();
      auto pskType = fizz->getState().pskType();
      if (pskType && *pskType == fizz::PskType::Resumption) {
        observer->tlsResumption();
      }
    } else {
      observer->tlsError();
    }
  } else {
    auto socket = sock->getUnderlyingTransport<folly::AsyncSSLSocket>();
    if (!socket) {
      return;
    }
    if (socket->good() && error == wangle::SSLErrorEnum::NO_ERROR) {
      observer->tlsComplete();
      if (socket->getSSLSessionReused()) {
        observer->tlsResumption();
      }
    } else {
      observer->tlsError();
    }
  }
}

wangle::AcceptorHandshakeHelper::UniquePtr Cpp2Worker::createSSLHelper(
    const std::vector<uint8_t>& bytes,
    const folly::SocketAddress& clientAddr,
    std::chrono::steady_clock::time_point acceptTime,
    wangle::TransportInfo& tInfo) {
  if (accConfig_.fizzConfig.enableFizz) {
    return getFizzPeeker()->getHelper(bytes, clientAddr, acceptTime, tInfo);
  }
  return defaultPeekingCallback_.getHelper(
      bytes, clientAddr, acceptTime, tInfo);
}

bool Cpp2Worker::shouldPerformSSL(
    const std::vector<uint8_t>& bytes,
    const folly::SocketAddress& clientAddr) {
  auto sslPolicy = getSSLPolicy();
  if (sslPolicy == SSLPolicy::REQUIRED) {
    if (isPlaintextAllowedOnLoopback()) {
      // loopback clients may still be sending TLS so we need to ensure that
      // it doesn't appear that way in addition to verifying it's loopback.
      return !(
          clientAddr.isLoopbackAddress() && !TLSHelper::looksLikeTLS(bytes));
    }
    return true;
  } else {
    return sslPolicy != SSLPolicy::DISABLED && TLSHelper::looksLikeTLS(bytes);
  }
}

wangle::AcceptorHandshakeHelper::UniquePtr Cpp2Worker::getHelper(
    const std::vector<uint8_t>& bytes,
    const folly::SocketAddress& clientAddr,
    std::chrono::steady_clock::time_point acceptTime,
    wangle::TransportInfo& ti) {
  if (!shouldPerformSSL(bytes, clientAddr)) {
    return wangle::AcceptorHandshakeHelper::UniquePtr(
        new wangle::UnencryptedAcceptorHandshakeHelper());
  }

  auto sslAcceptor = createSSLHelper(bytes, clientAddr, acceptTime, ti);

  // If we have a nonzero dedicated ssl handshake pool, offload the SSL
  // handshakes with EvbHandshakeHelper.
  if (server_->sslHandshakePool_->numThreads() > 0) {
    return wangle::EvbHandshakeHelper::UniquePtr(new wangle::EvbHandshakeHelper(
        std::move(sslAcceptor), server_->sslHandshakePool_->getEventBase()));
  } else {
    return sslAcceptor;
  }
}

void Cpp2Worker::requestStop() {
  getEventBase()->runInEventBaseThreadAndWait([&] {
    if (stopping_) {
      return;
    }
    stopping_ = true;
    if (activeRequests_ == 0) {
      stopBaton_.post();
    }
  });
}

void Cpp2Worker::waitForStop(std::chrono::system_clock::time_point deadline) {
  if (!stopBaton_.try_wait_until(deadline)) {
    LOG(ERROR) << "Failed to join outstanding requests.";
  }
}
} // namespace thrift
} // namespace apache
