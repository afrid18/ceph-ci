// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2022 Red Hat
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "gtest/gtest.h"

#include "common/async/context_pool.h"
#include "global/global_context.h"

#include "msg/Messenger.h"
#include "mon/MonClient.h"
#include "osdc/ObjectCacher.h"
#include "client/MetaRequest.h"
#include "client/Client.h"

#define dout_subsys ceph_subsys_client

class TestNewOpsClient : public Client {
public:
    //TestNewOpsClient();
    TestNewOpsClient(Messenger *m, MonClient *mc, Objecter *objecter_) : Client(m, mc, objecter_) {}
    ~TestNewOpsClient();
    int check_dummy_op(const UserPerm& perms){
      RWRef_t mref_reader(mount_state, CLIENT_MOUNTING);
      if (!mref_reader.is_state_satisfied()) {
        return -CEPHFS_ENOTCONN;
      }
      MetaRequest *req = new MetaRequest(CEPH_MDS_OP_DUMMY);
      int res = make_request(req, perms);
      ldout(cct, 10) << __func__ << " result=" << res << dendl;
      return res;
    }
};

class TestNewOps : public ::testing::Test { 
public:
    ~TestNewOps();
    static void SetUpTestSuite() {
      icp.start(g_ceph_context->_conf.get_val<std::uint64_t>("client_asio_thread_count"));
    }
    static void TearDownTestSuite() {
      icp.stop();
    }
    void SetUp() override {
      messenger = Messenger::create_client_messenger(g_ceph_context, "client");
      if (messenger->start() != 0) {
        throw std::runtime_error("failed to start messenger");
      }

      mc = new MonClient(g_ceph_context, icp);
      if (mc->build_initial_monmap() < 0) {
        throw std::runtime_error("build monmap");
      }
      mc->set_messenger(messenger);
      mc->set_want_keys(CEPH_ENTITY_TYPE_MDS | CEPH_ENTITY_TYPE_OSD);
      if (mc->init() < 0) {
        throw std::runtime_error("init monclient");
      }

      objecter = new Objecter(g_ceph_context, messenger, mc, icp);
      objecter->set_client_incarnation(0);
      objecter->init();
      messenger->add_dispatcher_tail(objecter);
      objecter->start();

      client = new TestNewOpsClient(messenger, mc, objecter);
      client->init();
      client->mount("/", myperm, true);
    }
    void TearDown() override {
      if (client->is_mounted())
        client->unmount();
      client->shutdown();
      objecter->shutdown();
      mc->shutdown();
      messenger->shutdown();
      messenger->wait();

      delete client;
      client = nullptr;
      delete objecter;
      objecter = nullptr;
      delete mc;
      mc = nullptr;
      delete messenger;
      messenger = nullptr;
    }
protected:
    static inline ceph::async::io_context_pool icp;
    static inline UserPerm myperm{0,0};
    MonClient* mc = nullptr;
    Messenger* messenger = nullptr;
    Objecter* objecter = nullptr;
    TestNewOpsClient* client = nullptr;
};
