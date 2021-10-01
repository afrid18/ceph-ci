// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#pragma once

#include "LRemRadosClient.h"
#include "include/ceph_assert.h"
#include <list>
#include <string>

namespace librados {

class AioCompletionImpl;
class LRemMemCluster;

class LRemMemRadosClient : public LRemRadosClient {
public:
  LRemMemRadosClient(CephContext *cct, LRemMemCluster *lrem_mem_cluster);
  ~LRemMemRadosClient() override;

  LRemIoCtxImpl *create_ioctx(int64_t pool_id,
                                      const std::string &pool_name) override;

  uint32_t get_nonce() override {
    return m_nonce;
  }
  uint64_t get_instance_id() override {
    return m_global_id;
  }

  int get_min_compatible_osd(int8_t* require_osd_release) override {
    *require_osd_release = CEPH_RELEASE_OCTOPUS;
    return 0;
  }

  int get_min_compatible_client(int8_t* min_compat_client,
                                int8_t* require_min_compat_client) override {
    *min_compat_client = CEPH_RELEASE_MIMIC;
    *require_min_compat_client = CEPH_RELEASE_MIMIC;
    return 0;
  }

  int object_list_open(int64_t pool_id,
                       std::shared_ptr<ObjListOp> *op) override;

  int service_daemon_register(const std::string& service,
                              const std::string& name,
                              const std::map<std::string,std::string>& metadata) override {
    return 0;
  }
  int service_daemon_update_status(std::map<std::string,std::string>&& status) override {
    return 0;
  }

  int pool_create(const std::string &pool_name) override;
  int pool_delete(const std::string &pool_name) override;
  int pool_get_base_tier(int64_t pool_id, int64_t* base_tier) override;
  int pool_list(std::list<std::pair<int64_t, std::string> >& v) override;
  int64_t pool_lookup(const std::string &name) override;
  int pool_reverse_lookup(int64_t id, std::string *name) override;

  int watch_flush() override;

  bool is_blocklisted() const override;
  int blocklist_add(const std::string& client_address,
                    uint32_t expire_seconds) override;

  int cluster_stat(cluster_stat_t& result) override {
    return -ENOTSUP;
  }

protected:
  LRemMemCluster *get_mem_cluster() {
    return m_mem_cluster;
  }

protected:
  void transaction_start(LRemTransactionStateRef& state) override;
  void transaction_finish(LRemTransactionStateRef& state) override;

private:
  LRemMemCluster *m_mem_cluster;
  uint32_t m_nonce;
  uint64_t m_global_id;

};

} // namespace librados
