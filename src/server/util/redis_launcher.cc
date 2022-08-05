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
#include "redis++/recipes/redlock.h"

#include <netdb.h>

#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include "boost/algorithm/string.hpp"

#include "common/util/env.h"

namespace vineyard {

Status RedisLauncher::LaunchRedisServer(
  std::unique_ptr<redis::AsyncRedis>& redis_client,
  std::unique_ptr<redis::Redis>& syncredis_client,
  std::shared_ptr<redis::RedLock<redis::RedMutex>>& lock,
  std::unique_ptr<boost::process::child>& redis_proc) {
  // async redis client
  redis::ConnectionOptions opts;
  opts.host = redis_spec_["redis_endpoint"].get_ref<std::string const&>();
  opts.password = "root";
  opts.port = 6379;
  redis::ConnectionPoolOptions pool_opts;
  pool_opts.size = 1;
  redis_client.reset(new redis::AsyncRedis(opts, pool_opts));
  redis_client->command<long long>("SETNX", "redis_revision", 0).get();

  // sync redis client
  syncredis_client.reset(new redis::Redis(opts, pool_opts));
  redis::RedMutex mtx(*syncredis_client, "resource");
  lock.reset(new redis::RedLock<redis::RedMutex>(mtx, std::defer_lock));

  if (probeRedisServer(redis_client, syncredis_client, "sync_lock")) {
        return Status::OK();
  }

  LOG(INFO) << "Starting the etcd server";
  return Status::OK();
}

void RedisLauncher::parseEndpoint() {}

void RedisLauncher::initHostInfo() {}

bool RedisLauncher::probeRedisServer(
  std::unique_ptr<redis::AsyncRedis>& redis_client,
  std::unique_ptr<redis::Redis>& syncredis_client,
  std::string const& key) {
  auto task = redis_client->ping(); 
  auto response = task.get();
  auto sync_response = syncredis_client->ping();
  // TODO:
  return redis_client && syncredis_client && (response == "PONG") && (sync_response == "PONG"); 
}

}