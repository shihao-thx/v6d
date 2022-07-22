/** Copyright 2020 Alibaba Group Holding Limited.
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

#ifndef SRC_SERVER_SERVICES_REDIS_META_SERVICE_H_
#define SRC_SERVER_SERVICES_REDIS_META_SERVICE_H_

#include <memory>
#include <string>
#include <vector>



#include "server/services/meta_service.h"
#include "server/util/redis_launcher.h"

using namespace sw;

namespace vineyard {

/**
 * @brief RedisLock is designed as the lock for accessing redis
 *
 */



/**
 * @brief LocalMetaService provides meta services in regards to local, e.g.
 * requesting and committing udpates
 *
 */
class RedisMetaService : public IMetaService {
 public:
  inline void Stop() override;
  ~RedisMetaService() override {}

 protected:
  explicit RedisMetaService(vs_ptr_t& server_ptr) : IMetaService(server_ptr),
      redis_spec_(server_ptr_->GetSpec()["metastore_spec"]),
      prefix_(redis_spec_["redis_prefix"].get<std::string>() + "/" +
                SessionIDToString(server_ptr->session_id())) {
    //this->handled_rev_.store(0);   
  }

  void requestLock(
      std::string lock_name,
      callback_t<std::shared_ptr<ILock>> callback_after_locked) override;

  void requestAll(
      const std::string& prefix, unsigned base_rev,
      callback_t<const std::vector<op_t>&, unsigned> callback) override;

  void requestUpdates(
      const std::string& prefix, unsigned since_rev,
      callback_t<const std::vector<op_t>&, unsigned> callback) override;

  void commitUpdates(const std::vector<op_t>&,
                     callback_t<unsigned> callback_after_updated) override;

  void startDaemonWatch(
      const std::string& prefix, unsigned since_rev,
      callback_t<const std::vector<op_t>&, unsigned, callback_t<unsigned>>
          callback) override;

  void retryDaeminWatch(
      const std::string& prefix,
      callback_t<const std::vector<op_t>&, unsigned, callback_t<unsigned>>
          callback);

  Status probe() override {
    if (Redisauncher::probeRedisServer(etcd_, prefix_)) {
      return Status::OK();
    } else {
      return Status::Invalid(
          "Failed to startup meta service, please check your redis");
    }
  }

  const json redis_spec_;
  const std::string prefix_;

 private:
  std::shared_ptr<RedisMetaService> shared_from_base() {
    return std::static_pointer_cast<RedisMetaService>(shared_from_this());
  }

  Status preStart() override;

  std::unique_ptr<redis::AsyncRedis> redis_;
  std::unique_ptr<boost::process::child> etcd_proc_; // 什么用处

  friend class IMetaService;
};

}  // namespace vineyard

#endif  // SRC_SERVER_SERVICES_LOCAL_META_SERVICE_H_
