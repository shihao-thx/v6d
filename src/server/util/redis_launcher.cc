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

#include "server/util/redis_launcher.h"

#include <netdb.h>

#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include "boost/algorithm/string.hpp"

#include "common/util/env.h"

namespace vineyard {

Status RedisLauncher::LaunchRedisServer(std::unique_ptr<redis::AsyncRedis>& redis_client,
                          std::string& sync_lock,
                          std::unique_ptr<boost::process::child>& redis_proc) {
  redis::ConnectionOptions opts;
  opts.host = redis_spec_["redis_endpoint"].get_ref<std::string const&>();
  opts.host = "192.168.127.130";
  opts.password = "root";
  opts.port = 6379;

  redis::ConnectionPoolOptions pool_opts;
  pool_opts.size = 3;
 
  redis_client.reset(new redis::AsyncRedis(opts, pool_opts));
  
  if (probeRedisServer(redis_client, sync_lock)) {
        return Status::OK();
  }

  LOG(INFO) << "Starting the etcd server";

}

void RedisLauncher::parseEndpoint() {}

void RedisLauncher::initHostInfo() {}

bool RedisLauncher::probeRedisServer(std::unique_ptr<redis::AsyncRedis>& redis_client, std::string const& key) {
  auto task = redis_client->ping(); 
  auto response = task.get();
  // TODO:
  return redis_client && (response == "PONG"); 
}

}