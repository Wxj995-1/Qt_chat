#include "redis.hpp"
#include <iostream>
#include <chrono>
#include <sys/socket.h>
using namespace std;

Redis::Redis()
    : _publish_context(nullptr), _subcribe_context(nullptr)
{
}

Redis::~Redis()
{
    stop();
    if (_observerThread.joinable())
        _observerThread.join();

    if (_publish_context != nullptr)
    {
        redisFree(_publish_context);
        _publish_context = nullptr;
    }
    if (_subcribe_context != nullptr)
    {
        redisFree(_subcribe_context);
        _subcribe_context = nullptr;
    }
}

bool Redis::connect()
{
    // 清理旧连接（如果之前调用过 stop，再次 connect 时重置状态）
    if (_publish_context != nullptr)
    {
        redisFree(_publish_context);
        _publish_context = nullptr;
    }
    if (_subcribe_context != nullptr)
    {
        redisFree(_subcribe_context);
        _subcribe_context = nullptr;
    }

    // 负责publish发布消息的上下文连接
    _publish_context = redisConnect("127.0.0.1", 6379);
    if (_publish_context == nullptr || _publish_context->err != 0)
    {
        if (_publish_context != nullptr)
        {
            cerr << "connect redis failed: " << _publish_context->errstr << endl;
            redisFree(_publish_context);
            _publish_context = nullptr;
        }
        else
        {
            cerr << "connect redis failed: can't allocate context" << endl;
        }
        return false;
    }

    // 负责subscribe订阅消息的上下文连接
    {
        lock_guard<mutex> lock(_subscribeMutex);
        _subcribe_context = redisConnect("127.0.0.1", 6379);
        if (_subcribe_context == nullptr || _subcribe_context->err != 0)
        {
            if (_subcribe_context != nullptr)
            {
                cerr << "connect redis failed: " << _subcribe_context->errstr << endl;
                redisFree(_subcribe_context);
                _subcribe_context = nullptr;
            }
            else
            {
                cerr << "connect redis failed: can't allocate context" << endl;
            }
            return false;
        }
    }

    // 在单独的线程中，监听通道上的事件，有消息给业务层进行上报
    if (_observerThread.joinable())
        _observerThread.join();
    _running = true;
    _observerThread = thread([this]() {
        observer_channel_message();
    });

    cout << "connect redis-server success!" << endl;

    return true;
}

// 向redis指定的通道channel发布消息
bool Redis::publish(int channel, string message)
{
    if (_publish_context == nullptr || _publish_context->err != 0)
        return false;

    redisReply *reply = (redisReply *)redisCommand(_publish_context, "PUBLISH %d %s", channel, message.c_str());
    if (nullptr == reply)
    {
        cerr << "publish command failed!" << endl;
        return false;
    }
    if (reply->type == REDIS_REPLY_ERROR)
    {
        cerr << "publish error: " << reply->str << endl;
        freeReplyObject(reply);
        return false;
    }
    freeReplyObject(reply);
    return true;
}

// 向redis指定的通道subscribe订阅消息
bool Redis::subscribe(int channel)
{
    if (_subcribe_context == nullptr || _subcribe_context->err != 0)
        return false;

    {
        lock_guard<mutex> lock(_subscribeMutex);

        if (REDIS_ERR == redisAppendCommand(this->_subcribe_context, "SUBSCRIBE %d", channel))
        {
            cerr << "subscribe command failed!" << endl;
            return false;
        }
        int done = 0;
        while (!done)
        {
            if (REDIS_ERR == redisBufferWrite(this->_subcribe_context, &done))
            {
                cerr << "subscribe command failed!" << endl;
                return false;
            }
        }
    }

    {
        lock_guard<mutex> lock(_channelMutex);
        _subscribed_channels.insert(channel);
    }

    return true;
}

// 向redis指定的通道unsubscribe取消订阅消息
bool Redis::unsubscribe(int channel)
{
    if (_subcribe_context == nullptr || _subcribe_context->err != 0)
        return false;

    {
        lock_guard<mutex> lock(_subscribeMutex);

        if (REDIS_ERR == redisAppendCommand(this->_subcribe_context, "UNSUBSCRIBE %d", channel))
        {
            cerr << "unsubscribe command failed!" << endl;
            return false;
        }
        int done = 0;
        while (!done)
        {
            if (REDIS_ERR == redisBufferWrite(this->_subcribe_context, &done))
            {
                cerr << "unsubscribe command failed!" << endl;
                return false;
            }
        }
    }

    {
        lock_guard<mutex> lock(_channelMutex);
        _subscribed_channels.erase(channel);
    }

    return true;
}

// 在独立线程中接收订阅通道中的消息
void Redis::observer_channel_message()
{
    while (_running)
    {
        redisReply *reply = nullptr;
        int ret = redisGetReply(this->_subcribe_context, (void **)&reply);
        if (!_running)
            break;

        if (ret != REDIS_OK)
        {
            cerr << "observer_channel_message redisGetReply failed, retrying..." << endl;

            {
                lock_guard<mutex> lock(_subscribeMutex);
                if (_subcribe_context != nullptr)
                {
                    redisFree(_subcribe_context);
                    _subcribe_context = nullptr;
                }
            }

            if (!resubscribe())
            {
                this_thread::sleep_for(chrono::seconds(3));
            }
            continue;
        }

        if (reply != nullptr
            && reply->type == REDIS_REPLY_ARRAY
            && reply->elements >= 3
            && reply->element[2] != nullptr
            && reply->element[2]->str != nullptr)
        {
            _notify_message_handler(atoi(reply->element[1]->str), reply->element[2]->str);
        }

        freeReplyObject(reply);
    }

    cerr << "observer_channel_message quit" << endl;
}

bool Redis::resubscribe()
{
    {
        lock_guard<mutex> lock(_subscribeMutex);

        if (_subcribe_context == nullptr)
        {
            _subcribe_context = redisConnect("127.0.0.1", 6379);
            if (_subcribe_context == nullptr || _subcribe_context->err != 0)
            {
                if (_subcribe_context != nullptr)
                {
                    cerr << "resubscribe redisConnect failed: " << _subcribe_context->errstr << endl;
                    redisFree(_subcribe_context);
                    _subcribe_context = nullptr;
                }
                return false;
            }
        }

        lock_guard<mutex> lock2(_channelMutex);
        for (int ch : _subscribed_channels)
        {
            if (REDIS_ERR == redisAppendCommand(_subcribe_context, "SUBSCRIBE %d", ch))
            {
                cerr << "resubscribe command failed for channel " << ch << endl;
                return false;
            }
            int done = 0;
            while (!done)
            {
                if (REDIS_ERR == redisBufferWrite(_subcribe_context, &done))
                {
                    cerr << "resubscribe bufferWrite failed for channel " << ch << endl;
                    return false;
                }
            }
        }
    }

    return true;
}

void Redis::stop()
{
    _running = false;
    // shutdown socket 而非 redisFree，避免与 observer 线程的 redisGetReply 产生竞态
    if (_subcribe_context != nullptr && _subcribe_context->fd > 0)
    {
        ::shutdown(_subcribe_context->fd, SHUT_RDWR);
    }
}

void Redis::init_notify_handler(function<void(int,string)> fn)
{
    this->_notify_message_handler = fn;
}