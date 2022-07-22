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

namespace vineyard {

void RedisMetaService::requestAll(
    const std::string& prefix, unsigned base_rev,
    callback_t<const std::vector<op_t>&, unsigned> callback) {
    auto self(shared_from_base());

    auto cursor = 0LL;
    auto pattern = "vineyard/*";
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
        boost::bind(callback, status, ops, 0));
}

Status EtcdMetaService::preStart() {
  //auto launcher = RedisLauncher(redis_spec_);
  RedisLauncher launcher(redis_spec_);
  return launcher.LaunchRedisServer(etcd_, meta_sync_lock_, etcd_proc_);
}
}