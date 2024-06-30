#include <unordered_set>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <sstream>
#include <cassert>

using namespace std::chrono_literals;

std::mutex coutMutex;

/*
 * Структура, имитирующая некоторое условное "соединение" с базой данных, каким-то сервисом в сети и т.п.
 */
struct FakeConnection {
    int id;

    FakeConnection(int id) : id(id) {}

    /*
     * Как будто отправляем какие-то данные через соединение
     */
    template <typename T>
    void WriteSomething(T&& message) {
        std::this_thread::sleep_for(10ms);
        std::lock_guard<std::mutex> coutLock(coutMutex);
        std::cout << "Connection #" << id << " write something: " << message << std::endl;
    }
};

using ConnectionPtr = std::shared_ptr<FakeConnection>;

/*
 * Пул соединений хранит не более чем `poolSize` соединений типа `FakeConnection`.
 * При необходимости он создает новые соединения, но их количество ни в какой момент времени не может превышать значения
 * `poolSize`
 * Различные потоки могут обращаться к пулу, чтобы получить соединение, а затем должны вернуть его в пул, чтобы
 * другие потоки также могли им воспользоваться
 */
class ConnectionPool {
public:
    ConnectionPool(size_t poolSize): poolSize_(poolSize), free_(poolSize) {}

    /*
     * Узнать максимальное количество соединений в пуле
     */
    size_t PoolSize() const {
        return poolSize_;
    }

    /*
     * Сколько соединений существует на данный момент в пуле
     */
    size_t ConnectionsAlive() const {
        return connectionsAlive_;
    }

    /*
     * Сколько соединений на данный момент используется в потоках
     */
    size_t ConnectionsInUse() const {
        return poolSize_ - free_;
    }

    /*
     * Получить соединение для использования.
     * Этот метод вызывается из разных потоков, чтобы взять одно соединение из пула.
     * До тех пор, пока соединение не будет освобождено с помощью метода `FreeConnection`,
     * другие потоки не могут получить к нему доступ с помощью этого метода.
     * Если в пуле нет свободных соединений, но количество созданных соединений не достигло значения `poolSize`,
     * то в этом методе мы должны создать новое соединение, добавить его в пул и отдать для использования.
     * Если в пуле нет свободных соединений, но количество созданных соединений уже достигло значения `poolSize`,
     * то поток должен ждать, пока не освободится какое-то соединение.
     */
    ConnectionPtr GetConnection(std::string threadId){
        std::unique_lock<std::mutex> lock(mt_);
        cv_.wait(lock, [this](){return free_ > 0;});
        --free_;
        if (connections_.size() == 0) {
            auto NewPtr = std::make_shared<FakeConnection>(stoi(threadId));
            ++connectionsAlive_;
            return NewPtr;
        }
        ConnectionPtr FreePtr = connections_.back();
        connections_.pop_back();
        return FreePtr;
    }

    /*
     * Освободить соединение, ранее полученное с помощью метода `GetConnection`.
     * Если какой-то поток ожидает свободного соединения, то нужно его оповестить.
     */
    void FreeConnection(ConnectionPtr& connection) {
        std::unique_lock<std::mutex> lock(mt_);
        connections_.push_back(std::move(connection));
        ++free_;
        cv_.notify_one();
    }
private:

    std::condition_variable cv_;
    std::vector<ConnectionPtr> connections_;
    std::mutex mt_;
    size_t connectionsAlive_ = 0;
    size_t free_;
    size_t poolSize_;
};

// Код, помогающий в отладке

void ThreadFunc(ConnectionPool& pool) {
    std::stringstream ss;
    ss << std::this_thread::get_id();
    std::string threadId = ss.str();

    std::vector<std::string> messagesToSendThroughConnections = {
            "First message from thread #",
            "Second message from thread #",
            "Third message from thread #",
            "Fourth message from thread #",
            "Fifth message from thread #",
    };

    for (const std::string& message : messagesToSendThroughConnections) {
        // Как будто делаем какие-то долгие полезные вычисления
        std::this_thread::sleep_for(10ms);

        // Хотим теперь отправить результат в "базу данных"
        // Получаем соединение
        ConnectionPtr connection = pool.GetConnection(threadId);
        assert(connection);

        // Отправляем через соединение наше полезнейшее сообщение в "базу данных"
        connection->WriteSomething(message + threadId);

        // Соединение нам пока что не нужно, дадим другим потокам воспользоваться им -- вернем его в пул
        pool.FreeConnection(connection);
        assert(!connection);
    }
}

int main() {
    // Наша "база данных" не выдержит большого количества одновременных соединений.
    // Ограничимся пятью соединениями
    ConnectionPool pool(5);

    // Потоков, которые будут что-то отправлять в "базу данных", гораздо больше, чем соединений.
    // Это не страшно, так как отправлять что-то в соединение нужно редко, и можно по очередь пользоваться
    // соединениями из общего пула
    std::vector<std::thread> threads;
    for (int i = 0; i < 12; ++i) {
        threads.emplace_back([&pool](){ThreadFunc(pool);});
    }

    // Ждем, пока появится хотя бы одно соединение
    while (!pool.ConnectionsAlive()) {}

    for (int i = 0; i < 40; ++i) {
        const size_t connectionsInUse = pool.ConnectionsInUse();
        const size_t connectionsAlive = pool.ConnectionsAlive();
        const size_t poolSize = pool.PoolSize();
        assert(connectionsInUse <= connectionsAlive);
        assert(connectionsAlive <= poolSize);

        std::this_thread::sleep_for(3ms);

        std::lock_guard<std::mutex> coutLock(coutMutex);
        std::cout << "Pool stats: connections in use " << connectionsInUse << "; connections alive " <<
                  connectionsAlive << "; pool size " << poolSize << std::endl;
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

/*
 * На выходе должно получиться что-то похожее на это:
Pool stats: connections in use 1; connections alive 1; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #0 write something: First message from thread #0x16d713000
Connection #1 write something: First message from thread #0x16d687000
Connection #2 write something: First message from thread #0x16dc8b000
Connection #4 write something: First message from thread #0x16d9cf000
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #3 write something: First message from thread #0x16d8b7000
Pool stats: connections in use 4; connections alive 5; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #0 write something: First message from thread #0x16da5b000
Connection #1 write something: First message from thread #0x16dae7000
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #2 write something: First message from thread #0x16db73000
Connection #4 write something: First message from thread #0x16dbff000
Connection #3 write something: First message from thread #0x16d82b000
Pool stats: connections in use 5; connections alive 5; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #0 write something: First message from thread #0x16d79f000
Connection #1 write something: First message from thread #0x16d943000
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #2 write something: Second message from thread #0x16d713000
Connection #4 write something: Second message from thread #0x16d687000
Connection #3 write something: Second message from thread #0x16dc8b000
Pool stats: connections in use 5; connections alive 5; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #1 write something: Second message from thread #0x16d9cf000
Connection #0 write something: Second message from thread #0x16d8b7000
Pool stats: connections in use 5; connections alive 5; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #2 write something: Second message from thread #0x16dae7000
Connection #3 write something: Second message from thread #0x16d82b000
Connection #4 write something: Second message from thread #0x16da5b000
Pool stats: connections in use 5; connections alive 5; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #0 write something: Second message from thread #0x16dbff000
Connection #1 write something: Second message from thread #0x16db73000
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #2 write something: Third message from thread #0x16dc8b000
Connection #4 write something: Second message from thread #0x16d79f000
Connection #3 write something: Third message from thread #0x16d713000
Pool stats: connections in use 5; connections alive 5; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #1 write something: Third message from thread #0x16d687000
Connection #0 write something: Second message from thread #0x16d943000
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #2 write something: Third message from thread #0x16d8b7000
Connection #4 write something: Third message from thread #0x16d9cf000
Connection #3 write something: Third message from thread #0x16dae7000
Pool stats: connections in use 5; connections alive 5; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #1 write something: Third message from thread #0x16d82b000
Connection #0 write something: Third message from thread #0x16da5b000
Pool stats: connections in use 5; connections alive 5; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #3 write something: Third message from thread #0x16d79f000
Connection #2 write something: Fourth message from thread #0x16d713000
Connection #4 write something: Third message from thread #0x16dbff000
Pool stats: connections in use 5; connections alive 5; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #0 write something: Fourth message from thread #0x16dc8b000
Connection #1 write something: Third message from thread #0x16db73000
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #3 write something: Fourth message from thread #0x16d9cf000
Connection #4 write something: Fourth message from thread #0x16d8b7000
Connection #2 write something: Fourth message from thread #0x16dae7000
Pool stats: connections in use 5; connections alive 5; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #0 write something: Fourth message from thread #0x16d687000
Connection #1 write something: Third message from thread #0x16d943000
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #4 write something: Fourth message from thread #0x16da5b000
Connection #3 write something: Fourth message from thread #0x16d82b000
Connection #2 write something: Fourth message from thread #0x16dbff000
Pool stats: connections in use 5; connections alive 5; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #0 write something: Fifth message from thread #0x16d713000
Connection #1 write something: Fourth message from thread #0x16d79f000
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #3 write something: Fourth message from thread #0x16db73000
Connection #2 write something: Fifth message from thread #0x16d9cf000
Connection #4 write something: Fifth message from thread #0x16dc8b000
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #0 write something: Fifth message from thread #0x16d8b7000
Pool stats: connections in use 5; connections alive 5; pool size 5
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #1 write something: Fifth message from thread #0x16dae7000
Connection #4 write something: Fifth message from thread #0x16d687000
Connection #2 write something: Fourth message from thread #0x16d943000
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #3 write something: Fifth message from thread #0x16da5b000
Pool stats: connections in use 5; connections alive 5; pool size 5
Connection #0 write something: Fifth message from thread #0x16d82b000
Pool stats: connections in use 4; connections alive 5; pool size 5
Connection #1 write something: Fifth message from thread #0x16dbff000
Pool stats: connections in use 3; connections alive 5; pool size 5
Connection #4 write something: Fifth message from thread #0x16d79f000
Connection #2 write something: Fifth message from thread #0x16db73000
Pool stats: connections in use 2; connections alive 5; pool size 5
Connection #4 write something: Fifth message from thread #0x16d943000
 */
