/*
 * NVMeGWMonitor.h
 *
 *  Created on: Oct 17, 2023
 *      Author: 227870756
 */
#ifndef  MON_NVMEGWMONITOR_H_
#define  MON_NVMEGWMONITOR_H_

#include "NVMeofGwMap.h"

struct LastBeacon {
    GW_ID_T gw_id;
    GROUP_KEY group_key;

    // Comparison operators to allow usage as a map key
    bool operator<(const LastBeacon& other) const {
        if (gw_id != other.gw_id) return gw_id < other.gw_id;
        return group_key < other.group_key;
    }

    bool operator==(const LastBeacon& other) const {
        return gw_id == other.gw_id &&
		group_key == other.group_key;
    }
};

class NVMeofGwMon: public PaxosService,
                   public md_config_obs_t
{
    NVMeofGwMap map;  //NVMeGWMap
    NVMeofGwMap pending_map;

    //TODO  the key of the beacon is a unique gw-id; for example string consisting from  gw_num + subsystem_nqn
    std::map<LastBeacon, ceph::coarse_mono_clock::time_point> last_beacon;


    // when the mon was not updating us for some period (e.g. during slow
    // election) to reset last_beacon timeouts
    ceph::coarse_mono_clock::time_point last_tick;

    std::vector<MonCommand> command_descs;
    std::vector<MonCommand> pending_command_descs;

public:
    NVMeofGwMon(Monitor &mn, Paxos &p, const std::string& service_name): PaxosService(mn, p, service_name) {map.mon = &mn; }
    ~NVMeofGwMon() override {}


    // config observer
    const char** get_tracked_conf_keys() const override;
    void handle_conf_change(const ConfigProxy& conf, const std::set<std::string> &changed) override;

    // 3 pure virtual methods of the paxosService
    void create_initial()override{};
    void create_pending()override ;
    void encode_pending(MonitorDBStore::TransactionRef t)override ;

    void init() override;
    void on_shutdown() override;
    void on_restart() override;
    void update_from_paxos(bool *need_bootstrap) override;


    bool preprocess_query(MonOpRequestRef op) override;
    bool prepare_update(MonOpRequestRef op) override;

    bool preprocess_command(MonOpRequestRef op);
    bool prepare_command(MonOpRequestRef op);

    void encode_full(MonitorDBStore::TransactionRef t) override { }

    bool preprocess_beacon(MonOpRequestRef op);
    bool prepare_beacon(MonOpRequestRef op);

    void tick() override;

    void print_summary(ceph::Formatter *f, std::ostream *ss) const;

    void check_subs(bool type);
    void check_sub(Subscription *sub);
};

#endif /* MON_NVMEGWMONITOR_H_ */
