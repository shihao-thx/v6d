/** Copyright 2020-2021 Alibaba Group Holding Limited.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "server/services/redis_meta_service.h"

#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <pplx/pplxtasks.h>

#include "common/util/boost.h"
#include "common/util/logging.h"

#define BACKOFF_RETRY_TIME 10

namespace vineyard {

void RedisWatchHandler::operator()(
  std::unique_ptr<redis::AsyncRedis>& redis, std::string rev) {
  // need to ensure handled_rev_ update before next publish
  if (this->meta_service_ptr_->stopped()) {
    return;
  }
  auto resp = redis->lrange<std::vector<std::string>>("opslist", handled_rev_, stoi(rev));
  
  std::vector<std::string> puts;
  std::vector<std::string> dels;
  for (auto const& item : resp.get()) {
      auto hresp = redis->hgetall<std::vector<std::string>>(item);
      auto const& vec = hresp.get();
      int j;
      for (size_t i = 0; i < (vec.size() >> 1); i++){
          j = i << 1;
          if(vec[j+1] == "0") {
              // keys need to Put
              puts.emplace_back(vec[j]);
          } else if(vec[j+1] == "1") {
              // keys need to Del
              dels.emplace_back(vec[j]);
          }   
      }   
  }
  std::vector<RedisMetaService::op_t> ops;
  if (puts.size() > 0) {
    auto mresp = redis->mget<std::vector<std::string>>(puts.begin(), puts.end());
    auto const& vals = mresp.get();
    for (size_t i = 0; i < vals.size(); i++) {
      ops.emplace_back(RedisMetaService::op_t::Put(
      boost::algorithm::erase_head_copy(puts[i], prefix_.size()),
      vals[i], stoi(rev) + 1));
    }
  }

  for (auto const& item : dels) {
    ops.emplace_back(RedisMetaService::op_t::Del(item, stoi(rev) + 1));
  }

#ifndef NDEBUG
  static unsigned processed = 0;
#endif

  auto status = Status::EtcdError(0, "redis message");

  ctx_.post(boost::bind(
    callback_, status, ops, stoi(rev) + 1,
    [this, status](Status const&, unsigned rev) -> Status {
        if (this->meta_service_ptr_->stopped()) {
          return Status::AlreadyStopped("redis metadata service");
        }
        std::lock_guard<std::mutex> scope_lock(
            this->registered_callbacks_mutex_);
        this->handled_rev_.store(rev);
        // after handled_rev_ updates, next publish can work
        this->watch_mutex_.unlock();
        // handle registered callbacks
        while (!this->registered_callbacks_.empty()) {
          auto iter = this->registered_callbacks_.top();
#ifndef NDEBUG
          VINEYARD_ASSERT(iter.first >= processed);
          processed = iter.first;
#endif
          if (iter.first > rev) {
            break;
          }
          this->ctx_.post(boost::bind(
              iter.second, status, std::vector<RedisMetaService::op_t>{}, rev));
          this->registered_callbacks_.pop();
        }
        return Status::OK();
      }));
}

void RedisMetaService::Stop() {
  if (stopped_.exchange(true)) {
    return;
  }
  if (backoff_timer_) {
    boost::system::error_code ec;
    backoff_timer_->cancel(ec);
  }
  /*if (watcher_) {
    try {
      watcher_->Cancel();
    } catch (...) {}
  }*/
  if (redis_proc_) {
    std::error_code err;
    redis_proc_->terminate(err);
    kill(redis_proc_->id(), SIGTERM);
    redis_proc_->wait(err);
  }
}

void RedisMetaService::requestLock(
    std::string lock_name,
    callback_t<std::shared_ptr<ILock>> callback_after_locked) {
  auto self(shared_from_base());
  pplx::create_task([self]() {
      self->redlock_->try_lock(std::chrono::seconds(30));
      auto val = *(self->redis_->get("redis_revision").get());
      return val;
    })
    .then([self, callback_after_locked](std::string val) {
        auto lock_ptr = std::make_shared<RedisLock>(
            self,
            [self](const Status& status, unsigned& rev) {
              // ensure the lock get released.
              self->redlock_->unlock();
              if (self->stopped_.load()) {
                return Status::AlreadyStopped("redis metadata service");
              }
              return Status::EtcdError(0, "redis lock message");
            },
            stoi(val));
        Status status;
        if (self->stopped_.load()) {
          status = Status::AlreadyStopped("redis metadata service");
        } else {
          status = Status::EtcdError(0, "redis message");
        }
        self->server_ptr_->GetMetaContext().post(
            boost::bind(callback_after_locked, status, lock_ptr));
      });
}

void RedisMetaService::requestAll(
    const std::string& prefix, unsigned base_rev,
    callback_t<const std::vector<op_t>&, unsigned> callback) {
    auto self(shared_from_base());
    // We must ensure that the redis_revision matches the local data.
    // But we're not getting kvs at the same time, redis_revision can be changed 
    // when getting kvs in two steps.
    // Local data over revision is fine. They can matches when publish coming
    auto val = *redis_->get("redis_revision").get();
    redis_->command<std::vector<std::string>>("KEYS", "vineyard/*", 
      [self, callback, val](redis::Future<std::vector<std::string>> &&resp) {
        auto const& vec = resp.get();
        std::vector<std::string> keys;
        keys.emplace_back("MGET");
        for (size_t i = 0; i < vec.size(); i++) {
          if (!boost::algorithm::starts_with(vec[i],
                                          self->prefix_ + "/")) {
            // ignore garbage values
            continue;
          }   
          keys.emplace_back(vec[i]);
        }
        if (keys.size() > 1) {
          // mget   
          self->redis_->command<std::vector<std::string>>(keys.begin(), keys.end(),
            [self, keys, callback, val](redis::Future<std::vector<std::string>> &&resp) {
              //try {
                auto const& vals = resp.get(); 
                std::string op_key;
                //std::vector<op_t> ops(vals.size());
                std::vector<op_t> ops;
                ops.reserve(vals.size());
                // collect kvs
                for (size_t i = 1; i < keys.size(); i++) {
                    op_key = boost::algorithm::erase_head_copy(
                    keys[i], self->prefix_.size());
                    ops.emplace_back(RedisMetaService::op_t::Put(
                      op_key, vals[i-1], stoi(val)));
                }
                auto status =
                Status::EtcdError(0, "redis message");  
                self->server_ptr_->GetMetaContext().post(
                    boost::bind(callback, status, ops, stoi(val)));
              // } catch (const redis::ReplyError& err) {
              //   LOG(INFO) << "from shihao: " << err.what();
              //   std::cout << "from shihao: " << err.what() << std::endl;
              // }
          }); 
        } else {
          std::vector<op_t> ops;
          auto status =
            Status::EtcdError(0, "redis message");  
          self->server_ptr_->GetMetaContext().post(
                    boost::bind(callback, status, ops, stoi(val)));
        }
     });   
}

void RedisMetaService::requestUpdates(
    const std::string& prefix, unsigned,
    callback_t<const std::vector<op_t>&, unsigned> callback) {
  // all updates from publish
  auto self(shared_from_base());
  redis_->get("redis_revision",
    [self, callback](redis::Future<redis::OptionalString> &&resp) {
      if (self->stopped_.load()) {
        return;
      }
      //try { // in vain
        auto head_rev = static_cast<unsigned>(stoi(*resp.get()));
        {
          std::lock_guard<std::mutex> scope_lock(self->registered_callbacks_mutex_);
          auto handled_rev = self->handled_rev_.load();
          if (head_rev <= handled_rev) {
            self->server_ptr_->GetMetaContext().post(
              boost::bind(callback, Status::OK(), std::vector<op_t>{},
                        self->handled_rev_.load()));
            return;
          }
          self->registered_callbacks_.emplace(std::make_pair(head_rev, callback));
        }
      //} catch (const Error &err) {
        // handle error
      //}
    });
}

void RedisMetaService::commitUpdates(
    const std::vector<op_t>& changes,
    callback_t<unsigned> callback_after_updated) {
  // TODO: When the number of hash entries exceeds 500, hash tables are used instead of ZipList, 
  // which occupies a large memory.

  // the operation number is ready to publish
  auto rev = *redis_->get("redis_revision").get();
  auto op_prefix = "op" + rev;

  std::vector<std::string> kvs;
  kvs.emplace_back("MSET");
  std::unordered_map<std::string, unsigned> ops;
  std::vector<std::string> keys;
  for (auto const& op : changes) {
    if(op.op == op_t::kPut) {
      kvs.emplace_back(prefix_ + op.kv.key);
      kvs.emplace_back(op.kv.value);
      ops.insert({prefix_ + op.kv.key, op_t::kPut}); 
    } else if(op.op == op_t::kDel) {
      keys.emplace_back(prefix_ + op.kv.key);
      ops.insert({op.kv.key, op_t::kDel});
    }
  }

  // delete keys
  if (keys.size() > 0) {
    redis_->del(
      keys.begin(), keys.end(),
      [](redis::Future<long long> &&resp) {
  
    });
  }

  auto self(shared_from_base());
  // mset kvs
  redis_->command<void>(
    kvs.begin(), kvs.end(),
    [self, callback_after_updated, ops, op_prefix, rev](redis::Future<void> &&resp) {
      self->redis_->hset(
        op_prefix, ops.begin(), ops.end(),
        [self, callback_after_updated, op_prefix, rev](redis::Future<long long> &&resp) {
          self->redis_->command<long long>(
            "RPUSH", "opslist", op_prefix,
              [self, callback_after_updated, rev](redis::Future<long long> &&resp) {
                self->redis_->command<long long>(
                  "INCR", "redis_revision",
                  [self, callback_after_updated, rev](redis::Future<long long> &&resp) {
                    self->redis_->command<long long>(
                      "PUBLISH", "operations", rev,
                      [self, callback_after_updated, rev](redis::Future<long long> &&resp) {
                        Status status;
                        if (self->stopped_.load()) {
                          status = Status::AlreadyStopped("redis metadata service");
                        } else {
                          status = Status::EtcdError(0, "redis commit message");
                        }
                        self->server_ptr_->GetMetaContext().post(
                        boost::bind(callback_after_updated, status, std::stoi(rev)));
          });
        });
      });
    });
  });
}

void RedisMetaService::startDaemonWatch(
    const std::string& prefix, unsigned since_rev,
    callback_t<const std::vector<op_t>&, unsigned, callback_t<unsigned>>
    callback) {
  LOG(INFO) << "start background redis watch, since " << rev_;
  try { 
    this->handled_rev_.store(since_rev);
    if (!handler_) {
      handler_.reset(new RedisWatchHandler(
        shared_from_base(), server_ptr_->GetMetaContext(), callback, prefix_,
        prefix_ + meta_sync_lock_, this->registered_callbacks_,
        this->handled_rev_, this->registered_callbacks_mutex_, this->watch_mutex_));
    }
    auto self(shared_from_base());
    this->watcher_.reset(new redis::AsyncSubscriber(redis_->subscriber()));
    this->watcher_->on_message([self](std::string channel, std::string msg) {
      // can't ensure that post first execute first
      // so try to lock, unlock when this watch callback finished
      self->watch_mutex_.lock();
      self->server_ptr_->GetMetaContext().post(boost::bind<void>(
            std::ref(*(self->handler_)), std::ref(self->redis_), msg));
    });
    this->watcher_->subscribe("operations");
  } catch (std::runtime_error& e) {
    LOG(ERROR) << "Failed to create daemon redis watcher: " << e.what();
    this->watcher_.reset();
    this->retryDaeminWatch(prefix, callback);
  }
}

void RedisMetaService::retryDaeminWatch(
    const std::string& prefix,
    callback_t<const std::vector<op_t>&, unsigned, callback_t<unsigned>>
        callback) {
  auto self(shared_from_base());
  backoff_timer_.reset(new asio::steady_timer(
      server_ptr_->GetMetaContext(), std::chrono::seconds(BACKOFF_RETRY_TIME)));
  backoff_timer_->async_wait([self, prefix, callback](
                                const boost::system::error_code& error) {
    if (self->stopped_.load()) {
      return;
    }
    if (error) {
      LOG(ERROR) << "backoff timer error: " << error << ", " << error.message();
    }
    if (!error || error != boost::asio::error::operation_aborted) {
      // retry
      LOG(INFO) << "retrying to connect redis ...";
      self->startDaemonWatch(prefix, self->handled_rev_.load(), callback);
    }
  });
}

Status RedisMetaService::preStart() {
  RedisLauncher launcher(redis_spec_);
  return launcher.LaunchRedisServer(redis_, syncredis_, mtx_, redlock_, redis_proc_);
}

}