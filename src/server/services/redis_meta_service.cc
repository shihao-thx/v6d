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

#include "common/util/boost.h"
#include "common/util/logging.h"

namespace vineyard {

void RedisMetaService::Stop() {
  /*if (stopped_.exchange(true)) {
    return;
  }
  if (backoff_timer_) {
    boost::system::error_code ec;
    backoff_timer_->cancel(ec);
  }
  if (watcher_) {
    try {
      watcher_->Cancel();
    } catch (...) {}
  }
  if (etcd_proc_) {
    std::error_code err;
    etcd_proc_->terminate(err);
    kill(etcd_proc_->id(), SIGTERM);
    etcd_proc_->wait(err);
  }*/
}

void RedisMetaService::requestLock(
      std::string lock_name,
      callback_t<std::shared_ptr<ILock>> callback_after_locked) {

}

void RedisMetaService::requestAll(
    const std::string& prefix, unsigned base_rev,
    callback_t<const std::vector<op_t>&, unsigned> callback) {
    auto self(shared_from_base());
/*
    auto cursor = 0LL;
    auto pattern = "vineyard";
    std::unordered_set<std::string> keys;
    while (true) {
        // 两次scan之间数据可能变动
        cursor = redis_.scan(cursor, pattern, std::inserter(keys, keys.begin()));
        if (cursor == 0) {
            break;
        }
    }

    std::vector<IMetaService::op_t> ops(keys.size());
    std::string op_key;
    for(auto const& temp : keys) {
        op_key = boost::algorithm::erase_head_copy(
            temp , self->prefix_.size());
        auto op = RedisMetaService::op_t::Put(op_key, *redis_.get(temp), 0);
        ops.emplace_back(op);

    }
    //auto status = Status::EtcdError(resp.error_code(), resp.error_message());
    self->server_ptr_->GetMetaContext().post( 
        boost::bind(callback, status, ops, 0));*/
}

void RedisMetaService::requestUpdates(
    const std::string& prefix, unsigned since_rev,
    callback_t<const std::vector<op_t>&, unsigned> callback) {

}

void RedisMetaService::commitUpdates(const std::vector<op_t>&,
                    callback_t<unsigned> callback_after_updated) {

}

void RedisMetaService::startDaemonWatch(
    const std::string& prefix, unsigned since_rev,
    callback_t<const std::vector<op_t>&, unsigned, callback_t<unsigned>>
    callback) {

}

Status RedisMetaService::preStart() {
  RedisLauncher launcher(redis_spec_);
  return launcher.LaunchRedisServer(redis_, meta_sync_lock_, redis_proc_);
}

}