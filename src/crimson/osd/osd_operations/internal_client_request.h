// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#pragma once

#include "crimson/osd/osd_operation.h"
#include "crimson/osd/pg.h"

namespace crimson::osd {

class InternalClientRequest : public OperationT<InternalClientRequest> {
public:
  InternalClientRequest(Ref<PG> pg);
  ~InternalClientRequest();

  // imposed by `ShardService::start_operation<T>(...)`.
  seastar::future<> start();

protected:
  virtual const hobject_t& get_target_oid() const = 0;
  virtual PG::do_osd_ops_params_t get_do_osd_ops_params() const = 0;
  virtual seastar::future<std::vector<OSDOp>> fabricate_osd_ops() = 0;

  PG& get_pg() {
    return *pg;
  }

private:
  friend OperationT<InternalClientRequest>;

  static constexpr OperationTypeCode type =
    OperationTypeCode::internal_client_request;

  void print(std::ostream &) const final;
  void dump_detail(Formatter *f) const final;

  CommonPGPipeline& pp();

  interruptible_future<> do_recover_missing(Ref<PG>& pgref, const hobject_t& soid);
  seastar::future<> do_process();

  Ref<PG> pg;
  PipelineHandle handle;
  OpInfo op_info;
};

} // namespace crimson::osd
