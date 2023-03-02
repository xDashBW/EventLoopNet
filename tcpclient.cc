#include "tcpclient.h"

#include "connector.h"
#include "tcpconnection.h"

TcpClient::TcpClient(EventLoop *loop, const char *ip, int16_t port,
                     const std::any &context)
    : connector(new Connector(loop, ip, port)),
      loop(loop),
      context(context),
      ip(ip),
      port(port),
      retry(false),
      connecting(true),
      connection(nullptr) {
  connector->setNewConnectionCallback(
      std::bind(&TcpClient::newConnection, this, std::placeholders::_1));
  connector->setConnectionErrorCallBack(
      std::bind(&TcpClient::errorConnection, this));
}

namespace detail {
void removeConnection(EventLoop *loop, const TcpConnectionPtr &conn) {
  loop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}

void removeConnector(const ConnectorPtr &connector) {}
}  // namespace detail

TcpClient::~TcpClient() {
  TcpConnectionPtr conn;
  bool unique = false;
  {
    std::unique_lock<std::mutex> lk(mutex);
    unique = connection.unique();
    conn = connection;
  }

  if (conn) {
    assert(loop == conn->getLoop());
    CloseCallback cb = std::bind(&detail::removeConnection, loop, conn);
    loop->runInLoop(std::bind(&TcpConnection::setCloseCallback, conn, cb));
    if (unique) {
      conn->forceClose();
    }
  } else {
    connector->stop();
    loop->runAfter(1, false, std::bind(&detail::removeConnector, connector));
  }
}

void TcpClient::connect(bool s) {
  connecting = true;
  connector->start(s);
}

void TcpClient::disConnect() {
  connecting = false;
  {
    std::unique_lock<std::mutex> lk(mutex);
    if (connection) {
      connection->shutdown();
    }
  }
}

TcpConnectionPtr TcpClient::getConnection() {
  std::unique_lock<std::mutex> lk(mutex);
  return connection;
}

void TcpClient::stop() {
  connecting = false;
  connector->stop();
}

void TcpClient::errorConnection() { connectionErrorCallBack(context); }

void TcpClient::newConnection(int32_t sockfd) {
  TcpConnectionPtr conn(new TcpConnection(loop, sockfd, context));
  conn->setConnectionCallback(std::move(connectionCallback));
  conn->setMessageCallback(std::move(messageCallback));
  conn->setWriteCompleteCallback(std::move(writeCompleteCallback));
  conn->setCloseCallback(
      std::bind(&TcpClient::removeConnection, this, std::placeholders::_1));

  {
    std::unique_lock<std::mutex> lk(mutex);
    connection = conn;
  }
  conn->connectEstablished();
}

void TcpClient::removeConnection(const TcpConnectionPtr &conn) {
  loop->assertInLoopThread();
  assert(loop == conn->getLoop());
  {
    std::unique_lock<std::mutex> lk(mutex);
    assert(connection == conn);
    connection.reset();
  }

  loop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
  if (retry && connecting) {
    connector->restart();
  }
}
