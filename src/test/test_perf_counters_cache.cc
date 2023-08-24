#include "common/perf_counters_cache.h"
#include "common/perf_counters_key.h"
#include "common/admin_socket_client.h"
#include "global/global_context.h"
#include "global/global_init.h"
#include "include/msgr.h" // for CEPH_ENTITY_TYPE_CLIENT
#include "gtest/gtest.h"

using namespace std;

int main(int argc, char **argv) {
  std::map<string,string> defaults = {
    { "admin_socket", get_rand_socket_path() }
  };
  std::vector<const char*> args;
  auto cct = global_init(&defaults, args, CEPH_ENTITY_TYPE_CLIENT,
			 CODE_ENVIRONMENT_UTILITY,
			 CINIT_FLAG_NO_DEFAULT_CONFIG_FILE|
			 CINIT_FLAG_NO_CCT_PERF_COUNTERS);
  common_init_finish(g_ceph_context);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

enum {
  TEST_PERFCOUNTERS1_ELEMENT_FIRST = 200,
  TEST_PERFCOUNTERS_COUNTER,
  TEST_PERFCOUNTERS_TIME,
  TEST_PERFCOUNTERS_TIME_AVG,
  TEST_PERFCOUNTERS1_ELEMENT_LAST,
};

std::string sd(const char *c)
{
  std::string ret(c);
  std::string::size_type sz = ret.size();
  for (std::string::size_type i = 0; i < sz; ++i) {
    if (ret[i] == '\'') {
      ret[i] = '\"';
    }
  }
  return ret;
}

void add_test_counters(PerfCountersBuilder *pcb) {
  pcb->add_u64(TEST_PERFCOUNTERS_COUNTER, "test_counter");
  pcb->add_time(TEST_PERFCOUNTERS_TIME, "test_time");
  pcb->add_time_avg(TEST_PERFCOUNTERS_TIME_AVG, "test_time_avg");
}


static PerfCountersCache* setup_test_perf_counters_cache(CephContext *cct, uint64_t target_size = 100)
{
  std::function<void(PerfCountersBuilder*)> lpcb = add_test_counters;
  CountersSetup test_counters_setup(TEST_PERFCOUNTERS1_ELEMENT_FIRST, TEST_PERFCOUNTERS1_ELEMENT_LAST, lpcb);
  std::unordered_map<std::string_view, CountersSetup> setups;
  setups["key1"] = test_counters_setup;
  setups["key2"] = test_counters_setup;
  setups["key3"] = test_counters_setup;
  setups["key4"] = test_counters_setup;
  setups["key5"] = test_counters_setup;
  setups["key6"] = test_counters_setup;
  setups["good_ctrs"] = test_counters_setup;
  setups["bad_ctrs1"] = test_counters_setup;
  setups["bad_ctrs2"] = test_counters_setup;
  setups["bad_ctrs3"] = test_counters_setup;
  setups["too_many_delimiters"] = test_counters_setup;
  return new PerfCountersCache(cct, target_size, setups);
}

void cleanup_test(PerfCountersCache *pcc) {
  delete pcc;
}

TEST(PerfCountersCache, NoCacheTest) {
  AdminSocketClient client(get_rand_socket_path());
  std::string message;
  ASSERT_EQ("", client.do_request(R"({ "prefix": "counter dump" })", &message));
  ASSERT_EQ("{}\n", message);
  ASSERT_EQ("", client.do_request(R"({ "prefix": "counter schema" })", &message));
  ASSERT_EQ("{}\n", message);
}

TEST(PerfCountersCache, TestEviction) {
  PerfCountersCache *pcc = setup_test_perf_counters_cache(g_ceph_context, 4);
  std::string label1 = ceph::perf_counters::key_create("key1", {{"label1", "val1"}});
  std::string label2 = ceph::perf_counters::key_create("key2", {{"label2", "val2"}});
  std::string label3 = ceph::perf_counters::key_create("key3", {{"label3", "val3"}});
  std::string label4 = ceph::perf_counters::key_create("key4", {{"label4", "val4"}});
  std::string label5 = ceph::perf_counters::key_create("key5", {{"label5", "val5"}});
  std::string label6 = ceph::perf_counters::key_create("key6", {{"label6", "val6"}});

  pcc->set_counter(label1, TEST_PERFCOUNTERS_COUNTER, 0);
  std::shared_ptr<PerfCounters> counter = pcc->get(label2);
  counter->set(TEST_PERFCOUNTERS_COUNTER, 0);
  pcc->set_counter(label3, TEST_PERFCOUNTERS_COUNTER, 0);
  pcc->set_counter(label4, TEST_PERFCOUNTERS_COUNTER, 0);

  AdminSocketClient client(get_rand_socket_path());
  std::string message;
  ASSERT_EQ("", client.do_request(R"({ "prefix": "counter dump", "format": "raw" })", &message));
  ASSERT_EQ(R"({
    "key1": [
        {
            "labels": {
                "label1": "val1"
            },
            "counters": {
                "test_counter": 0,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ],
    "key2": [
        {
            "labels": {
                "label2": "val2"
            },
            "counters": {
                "test_counter": 0,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ],
    "key3": [
        {
            "labels": {
                "label3": "val3"
            },
            "counters": {
                "test_counter": 0,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ],
    "key4": [
        {
            "labels": {
                "label4": "val4"
            },
            "counters": {
                "test_counter": 0,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ]
}
)", message);

  ASSERT_EQ("", client.do_request(R"({ "prefix": "counter schema", "format": "raw" })", &message));
  ASSERT_EQ(R"({
    "key1": [
        {
            "labels": {
                "label1": "val1"
            },
            "counters": {
                "test_counter": {
                    "type": 2,
                    "metric_type": "gauge",
                    "value_type": "integer",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time": {
                    "type": 1,
                    "metric_type": "gauge",
                    "value_type": "real",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time_avg": {
                    "type": 5,
                    "metric_type": "gauge",
                    "value_type": "real-integer-pair",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                }
            }
        }
    ],
    "key2": [
        {
            "labels": {
                "label2": "val2"
            },
            "counters": {
                "test_counter": {
                    "type": 2,
                    "metric_type": "gauge",
                    "value_type": "integer",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time": {
                    "type": 1,
                    "metric_type": "gauge",
                    "value_type": "real",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time_avg": {
                    "type": 5,
                    "metric_type": "gauge",
                    "value_type": "real-integer-pair",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                }
            }
        }
    ],
    "key3": [
        {
            "labels": {
                "label3": "val3"
            },
            "counters": {
                "test_counter": {
                    "type": 2,
                    "metric_type": "gauge",
                    "value_type": "integer",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time": {
                    "type": 1,
                    "metric_type": "gauge",
                    "value_type": "real",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time_avg": {
                    "type": 5,
                    "metric_type": "gauge",
                    "value_type": "real-integer-pair",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                }
            }
        }
    ],
    "key4": [
        {
            "labels": {
                "label4": "val4"
            },
            "counters": {
                "test_counter": {
                    "type": 2,
                    "metric_type": "gauge",
                    "value_type": "integer",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time": {
                    "type": 1,
                    "metric_type": "gauge",
                    "value_type": "real",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time_avg": {
                    "type": 5,
                    "metric_type": "gauge",
                    "value_type": "real-integer-pair",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                }
            }
        }
    ]
}
)", message);

  pcc->set_counter(label5, TEST_PERFCOUNTERS_COUNTER, 0);
  pcc->set_counter(label6, TEST_PERFCOUNTERS_COUNTER, 0);
  ASSERT_EQ("", client.do_request(R"({ "prefix": "counter dump", "format": "raw" })", &message));
  ASSERT_EQ(R"({
    "key3": [
        {
            "labels": {
                "label3": "val3"
            },
            "counters": {
                "test_counter": 0,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ],
    "key4": [
        {
            "labels": {
                "label4": "val4"
            },
            "counters": {
                "test_counter": 0,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ],
    "key5": [
        {
            "labels": {
                "label5": "val5"
            },
            "counters": {
                "test_counter": 0,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ],
    "key6": [
        {
            "labels": {
                "label6": "val6"
            },
            "counters": {
                "test_counter": 0,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ]
}
)", message);


  ASSERT_EQ("", client.do_request(R"({ "prefix": "counter schema", "format": "raw" })", &message));
  ASSERT_EQ(R"({
    "key3": [
        {
            "labels": {
                "label3": "val3"
            },
            "counters": {
                "test_counter": {
                    "type": 2,
                    "metric_type": "gauge",
                    "value_type": "integer",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time": {
                    "type": 1,
                    "metric_type": "gauge",
                    "value_type": "real",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time_avg": {
                    "type": 5,
                    "metric_type": "gauge",
                    "value_type": "real-integer-pair",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                }
            }
        }
    ],
    "key4": [
        {
            "labels": {
                "label4": "val4"
            },
            "counters": {
                "test_counter": {
                    "type": 2,
                    "metric_type": "gauge",
                    "value_type": "integer",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time": {
                    "type": 1,
                    "metric_type": "gauge",
                    "value_type": "real",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time_avg": {
                    "type": 5,
                    "metric_type": "gauge",
                    "value_type": "real-integer-pair",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                }
            }
        }
    ],
    "key5": [
        {
            "labels": {
                "label5": "val5"
            },
            "counters": {
                "test_counter": {
                    "type": 2,
                    "metric_type": "gauge",
                    "value_type": "integer",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time": {
                    "type": 1,
                    "metric_type": "gauge",
                    "value_type": "real",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time_avg": {
                    "type": 5,
                    "metric_type": "gauge",
                    "value_type": "real-integer-pair",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                }
            }
        }
    ],
    "key6": [
        {
            "labels": {
                "label6": "val6"
            },
            "counters": {
                "test_counter": {
                    "type": 2,
                    "metric_type": "gauge",
                    "value_type": "integer",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time": {
                    "type": 1,
                    "metric_type": "gauge",
                    "value_type": "real",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time_avg": {
                    "type": 5,
                    "metric_type": "gauge",
                    "value_type": "real-integer-pair",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                }
            }
        }
    ]
}
)", message);
  cleanup_test(pcc);
}

TEST(PerfCountersCache, TestLabeledCounters) {
  PerfCountersCache *pcc = setup_test_perf_counters_cache(g_ceph_context);
  std::string label1 = ceph::perf_counters::key_create("key1", {{"label1", "val1"}});
  std::string label2 = ceph::perf_counters::key_create("key2", {{"label2", "val2"}});
  std::string label3 = ceph::perf_counters::key_create("key3", {{"label3", "val3"}});

  // test inc()
  pcc->inc(label1, TEST_PERFCOUNTERS_COUNTER, 1);
  pcc->inc(label2, TEST_PERFCOUNTERS_COUNTER, 2);

  AdminSocketClient client(get_rand_socket_path());
  std::string message;
  ASSERT_EQ("", client.do_request(R"({ "prefix": "counter dump", "format": "raw" })", &message));
  ASSERT_EQ(R"({
    "key1": [
        {
            "labels": {
                "label1": "val1"
            },
            "counters": {
                "test_counter": 1,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ],
    "key2": [
        {
            "labels": {
                "label2": "val2"
            },
            "counters": {
                "test_counter": 2,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ]
}
)", message);


  ASSERT_EQ("", client.do_request(R"({ "prefix": "counter schema", "format": "raw"  })", &message));
  ASSERT_EQ(R"({
    "key1": [
        {
            "labels": {
                "label1": "val1"
            },
            "counters": {
                "test_counter": {
                    "type": 2,
                    "metric_type": "gauge",
                    "value_type": "integer",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time": {
                    "type": 1,
                    "metric_type": "gauge",
                    "value_type": "real",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time_avg": {
                    "type": 5,
                    "metric_type": "gauge",
                    "value_type": "real-integer-pair",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                }
            }
        }
    ],
    "key2": [
        {
            "labels": {
                "label2": "val2"
            },
            "counters": {
                "test_counter": {
                    "type": 2,
                    "metric_type": "gauge",
                    "value_type": "integer",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time": {
                    "type": 1,
                    "metric_type": "gauge",
                    "value_type": "real",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time_avg": {
                    "type": 5,
                    "metric_type": "gauge",
                    "value_type": "real-integer-pair",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                }
            }
        }
    ]
}
)", message);

  // tests to ensure there is no interaction with normal perf counters
  ASSERT_EQ("", client.do_request(R"({ "prefix": "perf dump", "format": "raw" })", &message));
  ASSERT_EQ("{}\n", message);
  ASSERT_EQ("", client.do_request(R"({ "prefix": "perf schema", "format": "raw" })", &message));
  ASSERT_EQ("{}\n", message);

  // test dec()
  pcc->dec(label2, TEST_PERFCOUNTERS_COUNTER, 1);
  ASSERT_EQ("", client.do_request(R"({ "prefix": "counter dump", "format": "raw" })", &message));
  ASSERT_EQ(R"({
    "key1": [
        {
            "labels": {
                "label1": "val1"
            },
            "counters": {
                "test_counter": 1,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ],
    "key2": [
        {
            "labels": {
                "label2": "val2"
            },
            "counters": {
                "test_counter": 1,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ]
}
)", message);


  // test set_counters()
  pcc->set_counter(label3, TEST_PERFCOUNTERS_COUNTER, 4);
  uint64_t val = pcc->get_counter(label3, TEST_PERFCOUNTERS_COUNTER);
  ASSERT_EQ(val, 4);
  ASSERT_EQ("", client.do_request(R"({ "prefix": "counter dump", "format": "raw" })", &message));
  ASSERT_EQ(R"({
    "key1": [
        {
            "labels": {
                "label1": "val1"
            },
            "counters": {
                "test_counter": 1,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ],
    "key2": [
        {
            "labels": {
                "label2": "val2"
            },
            "counters": {
                "test_counter": 1,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ],
    "key3": [
        {
            "labels": {
                "label3": "val3"
            },
            "counters": {
                "test_counter": 4,
                "test_time": 0.000000000,
                "test_time_avg": {
                    "avgcount": 0,
                    "sum": 0.000000000,
                    "avgtime": 0.000000000
                }
            }
        }
    ]
}
)", message);

  cleanup_test(pcc);
}

TEST(PerfCountersCache, TestLabeledTimes) {
  PerfCountersCache *pcc = setup_test_perf_counters_cache(g_ceph_context);
  std::string label1 = ceph::perf_counters::key_create("key1", {{"label1", "val1"}});
  std::string label2 = ceph::perf_counters::key_create("key2", {{"label2", "val2"}});
  std::string label3 = ceph::perf_counters::key_create("key3", {{"label3", "val3"}});

  // test inc()
  pcc->tinc(label1, TEST_PERFCOUNTERS_TIME, utime_t(100,0));
  pcc->tinc(label2, TEST_PERFCOUNTERS_TIME, utime_t(200,0));

  //tinc() that takes a ceph_timespan
  ceph::timespan ceph_timespan = std::chrono::seconds(10);
  pcc->tinc(label1, TEST_PERFCOUNTERS_TIME, ceph_timespan);

  pcc->tinc(label1, TEST_PERFCOUNTERS_TIME_AVG, utime_t(200,0));
  pcc->tinc(label1, TEST_PERFCOUNTERS_TIME_AVG, utime_t(400,0));
  pcc->tinc(label2, TEST_PERFCOUNTERS_TIME_AVG, utime_t(100,0));
  pcc->tinc(label2, TEST_PERFCOUNTERS_TIME_AVG, utime_t(200,0));

  AdminSocketClient client(get_rand_socket_path());
  std::string message;
  ASSERT_EQ("", client.do_request(R"({ "prefix": "counter dump", "format": "raw" })", &message));
  ASSERT_EQ(R"({
    "key1": [
        {
            "labels": {
                "label1": "val1"
            },
            "counters": {
                "test_counter": 0,
                "test_time": 110.000000000,
                "test_time_avg": {
                    "avgcount": 2,
                    "sum": 600.000000000,
                    "avgtime": 300.000000000
                }
            }
        }
    ],
    "key2": [
        {
            "labels": {
                "label2": "val2"
            },
            "counters": {
                "test_counter": 0,
                "test_time": 200.000000000,
                "test_time_avg": {
                    "avgcount": 2,
                    "sum": 300.000000000,
                    "avgtime": 150.000000000
                }
            }
        }
    ]
}
)", message);


  ASSERT_EQ("", client.do_request(R"({ "prefix": "counter schema", "format": "raw"  })", &message));
  ASSERT_EQ(R"({
    "key1": [
        {
            "labels": {
                "label1": "val1"
            },
            "counters": {
                "test_counter": {
                    "type": 2,
                    "metric_type": "gauge",
                    "value_type": "integer",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time": {
                    "type": 1,
                    "metric_type": "gauge",
                    "value_type": "real",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time_avg": {
                    "type": 5,
                    "metric_type": "gauge",
                    "value_type": "real-integer-pair",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                }
            }
        }
    ],
    "key2": [
        {
            "labels": {
                "label2": "val2"
            },
            "counters": {
                "test_counter": {
                    "type": 2,
                    "metric_type": "gauge",
                    "value_type": "integer",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time": {
                    "type": 1,
                    "metric_type": "gauge",
                    "value_type": "real",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                },
                "test_time_avg": {
                    "type": 5,
                    "metric_type": "gauge",
                    "value_type": "real-integer-pair",
                    "description": "",
                    "nick": "",
                    "priority": 0,
                    "units": "none"
                }
            }
        }
    ]
}
)", message);

  // test tset() & tget()
  pcc->tset(label1, TEST_PERFCOUNTERS_TIME, utime_t(500,0));
  utime_t label1_time = pcc->tget(label1, TEST_PERFCOUNTERS_TIME);
  ASSERT_EQ(utime_t(500,0), label1_time);

  cleanup_test(pcc);
}
