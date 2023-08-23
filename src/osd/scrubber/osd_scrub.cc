// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
#include "./osd_scrub.h"

#include <iostream>

#include "osd/PG.h"
#include "osd/scrubber/osd_scrub_sched.h"
#include "osd/scrubber/scrub_resources.h"

using namespace std::chrono;
using namespace std::chrono_literals;

#define dout_context (cct)
#define dout_subsys ceph_subsys_osd
#undef dout_prefix
#define dout_prefix _prefix_target(_dout, this)

template <class T>
static std::ostream& _prefix_target(std::ostream* _dout, T* t)
{
  return t->gen_prefix(*_dout);
}


OsdScrub::OsdScrub(
    CephContext* cct,
    Scrub::ScrubSchedListener& osd_svc,
    const ceph::common::ConfigProxy& config)
    : cct{cct}
    , m_osd_svc{osd_svc}
    , conf(config)
    , m_resource_bookkeeper{[this](std::string msg) { log_fwd(msg); }, conf}
    , m_queue{cct, m_osd_svc}
    , m_log_prefix{fmt::format("osd.{}: osd-scrub::", m_osd_svc.get_nodeid())}
    , m_load_tracker{cct, conf, m_osd_svc.get_nodeid()}
{}

std::ostream& OsdScrub::gen_prefix(std::ostream& out) const
{
  return out << m_log_prefix;
}


void OsdScrub::dump_scrubs(ceph::Formatter* f) const
{
  m_queue.dump_scrubs(f);
}

void OsdScrub::on_config_change()
{
  // RRR locking?
  auto to_notify = m_queue.list_registered_jobs();

  for (const auto& p : to_notify) {
    dout(15) << fmt::format("{}: rescheduling {}", __func__, *p) << dendl;
    auto locked_pg = m_osd_svc.get_locked_pg(p->pgid);
    if (!locked_pg)
      continue;

    dout(15) << fmt::format(
		    "{}: updating scrub schedule on {}", __func__,
		    (locked_pg->pg())->get_pgid())
	     << dendl;
    locked_pg->pg()->on_scrub_schedule_input_change();
  }
}

void OsdScrub::initiate_scrub(bool is_recovery_active)
{
  if (auto blocked_pgs = get_blocked_pgs_count(); blocked_pgs > 0) {
    // some PGs managed by this OSD were blocked by a locked object during
    // scrub. This means we might not have the resources needed to scrub now.
    dout(10) << fmt::format(
		    "{}: PGs are blocked while scrubbing due to locked objects "
		    "({} PGs)",
		    __func__, blocked_pgs)
	     << dendl;
  }

  // fail fast if no resources are available
  if (!m_resource_bookkeeper.can_inc_scrubs()) {
    dout(20) << fmt::format(
		    "{}: too many scrubs already running on this OSD", __func__)
	     << dendl;
    return;
  }

  // if there is a PG that is just now trying to reserve scrub replica resources -
  // we should wait and not initiate a new scrub
  if (is_reserving_now()) {
    dout(20) << fmt::format(
		    "{}: scrub resources reservation in progress", __func__)
	     << dendl;
    return;
  }

  m_scrub_tick_time = ceph_clock_now();
  dout(10) << fmt::format(
		  "{}: time now:{}, is_recovery_active:{}", __func__,
		  m_scrub_tick_time, is_recovery_active)
	   << dendl;


  // check the OSD-wide environment conditions (scrub resources, time, etc.).
  // These may restrict the type of scrubs we are allowed to start, or just
  // prevent us from starting any scrub at all.
  auto env_restrictions =
      restrictions_on_scrubbing(is_recovery_active, m_scrub_tick_time);
  if (!env_restrictions) {
    return;
  }

#ifdef NOT_YET
  list the queue - will be done in select_pg_to_scrub()
#endif

  /*
   at this phase of the refactoring: no change to the actual interface used
   to initiate a scrub (via the OSD).
   Also - no change to the queue interface used here: we ask for a list of (up to N)
   eligible targets (based on the known restrictions).
   We try all elements of this list until a (possibly temporary) success.
  */

#ifdef NOT_YET
      // let the queue handle the load/time issues?
      auto candidate =
      m_queue.select_pg_to_scrub(*env_restrictions, m_scrub_tick_time);
  if (!candidate) {
    dout(20) << fmt::format("{}: no more PGs to try", __func__) << dendl;
    break;
  }
#endif

  auto candidates =
      m_queue.ready_to_scrub(*env_restrictions, m_scrub_tick_time);
  auto res{Scrub::schedule_result_t::none_ready};
  for (const auto& candidate : candidates) {
    // State @ entering:
    // - the target was already dequeued from the queue
    //
    // process:
    // - mark the OSD as 'reserving now'
    // - queue the initiation message on the PG
    // - (later) set a timer for initiation confirmation/failure
    set_reserving_now();
    dout(20) << fmt::format(
		    "{}: initiating scrub on pg[{}]", __func__, candidate)
	     << dendl;

    // we have a candidate to scrub. We turn to the OSD to verify that the PG
    // configuration allows the specified type of scrub, and to initiate the
    // scrub.
    res = initiate_a_scrub(
	candidate, env_restrictions->allow_requested_repair_only);
    switch (res) {
      case Scrub::schedule_result_t::scrub_initiated:
	// the happy path. We are done
	dout(20) << fmt::format(
			"{}: scrub initiated for pg[{}]", __func__,
			candidate.pgid)
		 << dendl;
	break;

      case Scrub::schedule_result_t::already_started:
      case Scrub::schedule_result_t::preconditions:
      case Scrub::schedule_result_t::bad_pg_state:
	// continue with the next job
	dout(20) << fmt::format(
			"{}: pg[{}] failed (state/cond/started)", __func__,
			candidate.pgid)
		 << dendl;
	break;

      case Scrub::schedule_result_t::no_such_pg:
	// The pg is no longer there
	dout(20) << fmt::format(
			"{}: pg[{}] failed (no PG)", __func__, candidate.pgid)
		 << dendl;
	// RRR not handled here
	break;

      case Scrub::schedule_result_t::no_local_resources:
	// failure to secure local resources. No point in trying the other
	// PGs at this time. Note that this is not the same as replica resources
	// failure!
	dout(20) << "failed (local resources)" << dendl;
	break;

      case Scrub::schedule_result_t::none_ready:
	// can't happen. Just for the compiler.
	dout(5) << fmt::format(
		       "{}: failed!! (possible bug. pg[{}])", __func__,
		       candidate.pgid)
		<< dendl;
	break;
    }

    if (res == Scrub::schedule_result_t::no_local_resources) {
      break;
    }

    if (res == Scrub::schedule_result_t::scrub_initiated) {
      // in the temporary implementation: we need to dequeue the target at this time
      m_queue.scrub_initiated(candidate);
      break;
    }
  }

  // this is definitely not how the queue would be managed in the 2'nd phase, when
  // only one target would be selected at a time - and that target would have been dequeued.

  if (res != Scrub::schedule_result_t::scrub_initiated) {
    clear_reserving_now();
    dout(20) << fmt::format("{}: no more PGs to try", __func__) << dendl;
  }

  dout(20) << fmt::format("{}: sched_scrub done", __func__) << dendl;
}

Scrub::schedule_result_t OsdScrub::initiate_a_scrub(
    spg_t pgid,
    bool allow_requested_repair_only)
{
  dout(20) << fmt::format("{}: trying pg[{}]", __func__, pgid) << dendl;

  // we have a candidate to scrub. We need some PG information to know if scrubbing is
  // allowed

  auto locked_pg = m_osd_svc.get_locked_pg(pgid);
  if (!locked_pg) {
    // the PG was dequeued in the short timespan between creating the candidates list
    // (collect_ripe_jobs()) and here
    dout(5) << fmt::format("{}: pg[{}] not found", __func__, pgid) << dendl;
    return Scrub::schedule_result_t::no_such_pg;
  }

  // This has already started, so go on to the next scrub job
  if (locked_pg->pg()->is_scrub_queued_or_active()) {
    dout(10) << fmt::format(
		    "{}: pg[{}]: scrub already in progress", __func__, pgid)
	     << dendl;
    return Scrub::schedule_result_t::already_started;
  }
  // Skip other kinds of scrubbing if only explicitly requested repairing is allowed
  if (allow_requested_repair_only &&
      !locked_pg->pg()->get_planned_scrub().must_repair) {
    dout(10) << fmt::format(
		    "{}: skipping pg[{}] as repairing was not explicitly "
		    "requested for that pg",
		    __func__, pgid)
	     << dendl;
    return Scrub::schedule_result_t::preconditions;
  }

  auto scrub_attempt = locked_pg->pg()->sched_scrub();
  return scrub_attempt;
}

void OsdScrub::log_fwd(std::string_view text)
{
  dout(20) << text << dendl;
}

std::optional<Scrub::OSDRestrictions> OsdScrub::restrictions_on_scrubbing(
    bool is_recovery_active,
    utime_t scrub_clock_now) const
{
  // sometimes we just skip the scrubbing
  if (random_bool_with_probability(conf->osd_scrub_backoff_ratio)) {
    dout(20) << fmt::format(
		    "{}: lost coin flip, randomly backing off (ratio: {:f})",
		    __func__, conf->osd_scrub_backoff_ratio)
	     << dendl;
    return std::nullopt;
  }

  // our local OSD may already be running too many scrubs
  if (!m_resource_bookkeeper.can_inc_scrubs()) {
    dout(10) << fmt::format("{}: OSD cannot inc scrubs", __func__) << dendl;
    return std::nullopt;
  }

  // if there is a PG that is just now trying to reserve scrub replica resources
  // - we should wait and not initiate a new scrub
  if (is_reserving_now()) {
    dout(10) << fmt::format(
		    "{}: scrub resources reservation in progress", __func__)
	     << dendl;
    return std::nullopt;
  }

  Scrub::OSDRestrictions env_conditions;
  env_conditions.time_permit = scrub_time_permit();
  env_conditions.load_is_low = m_load_tracker.scrub_load_below_threshold();
  env_conditions.only_deadlined =
      !env_conditions.time_permit || !env_conditions.load_is_low;

  if (is_recovery_active && !conf->osd_scrub_during_recovery) {
    if (!conf->osd_repair_during_recovery) {
      dout(15) << fmt::format(
		      "{}: not scheduling scrubs due to active recovery",
		      __func__)
	       << dendl;
      return std::nullopt;
    }

    dout(10) << fmt::format(
		    "{}: will only schedule explicitly requested repair due to "
		    "active recovery",
		    __func__)
	     << dendl;
    env_conditions.allow_requested_repair_only = true;
  }

  return env_conditions;
}


// ////////////////////////////////////////////////////////////////////////// //
// CPU load tracking and related

OsdScrub::LoadTracker::LoadTracker(
    CephContext* cct,
    const ceph::common::ConfigProxy& config,
    int node_id)
    : cct{cct}
    , conf(config)
{
  log_prefix = fmt::format("osd.{} scrub-queue::load-tracker::", node_id);

  // initialize the daily loadavg with current 15min loadavg
  if (double loadavgs[3]; getloadavg(loadavgs, 3) == 3) {
    daily_loadavg = loadavgs[2];
  } else {
    derr << "OSD::init() : couldn't read loadavgs\n" << dendl;
    daily_loadavg = 1.0;
  }
}

std::optional<double> OsdScrub::LoadTracker::update_load_average()
{
  int hb_interval = conf->osd_heartbeat_interval;
  int n_samples = 60 * 24 * 24;
  if (hb_interval > 1) {
    n_samples = std::max(n_samples / hb_interval, 1);
  }

  double loadavg;
  if (getloadavg(&loadavg, 1) == 1) {
    daily_loadavg = (daily_loadavg * (n_samples - 1) + loadavg) / n_samples;
    return 100 * loadavg;
  }

  return std::nullopt;
}

bool OsdScrub::LoadTracker::scrub_load_below_threshold() const
{
  double loadavgs[3];
  if (getloadavg(loadavgs, 3) != 3) {
    dout(10) << fmt::format("{}: couldn't read loadavgs", __func__) << dendl;
    return false;
  }

  // allow scrub if below configured threshold
  long cpus = sysconf(_SC_NPROCESSORS_ONLN);
  double loadavg_per_cpu = cpus > 0 ? loadavgs[0] / cpus : loadavgs[0];
  if (loadavg_per_cpu < conf->osd_scrub_load_threshold) {
    dout(20) << fmt::format(
		    "loadavg per cpu {:.3f} < max {:.3f} = yes",
		    loadavg_per_cpu, conf->osd_scrub_load_threshold)
	     << dendl;
    return true;
  }

  // allow scrub if below daily avg and currently decreasing
  if (loadavgs[0] < daily_loadavg && loadavgs[0] < loadavgs[2]) {
    dout(20) << fmt::format(
		    "loadavg {:.3f} < daily_loadavg {:.3f} and < 15m avg "
		    "{:.3f} = yes",
		    loadavgs[0], daily_loadavg, loadavgs[2])
	     << dendl;
    return true;
  }

  dout(10) << fmt::format(
		  "loadavg {:.3f} >= max {:.3f} and ( >= daily_loadavg {:.3f} "
		  "or >= 15m "
		  "avg {:.3f} ) = no",
		  loadavgs[0], conf->osd_scrub_load_threshold, daily_loadavg,
		  loadavgs[2])
	   << dendl;
  return false;
}

std::ostream& OsdScrub::LoadTracker::gen_prefix(std::ostream& out) const
{
  return out << log_prefix;
}

std::optional<double> OsdScrub::update_load_average()
{
  return m_load_tracker.update_load_average();
}

// ////////////////////////////////////////////////////////////////////////// //

// checks for half-closed ranges. Modify the (p<till)to '<=' to check for
// closed.
static inline bool isbetween_modulo(int64_t from, int64_t till, int p)
{
  // the 1st condition is because we have defined from==till as "always true"
  return (till == from) || ((till >= from) ^ (p >= from) ^ (p < till));
}

bool OsdScrub::scrub_time_permit(utime_t now) const
{
  const time_t tt = now.sec();
  tm bdt;
  localtime_r(&tt, &bdt);

  bool day_permits = isbetween_modulo(
      conf->osd_scrub_begin_week_day, conf->osd_scrub_end_week_day,
      bdt.tm_wday);
  if (!day_permits) {
    dout(20) << fmt::format(
		    "{}: should run between week day {} - {} now {} - no",
		    __func__, conf->osd_scrub_begin_week_day,
		    conf->osd_scrub_end_week_day, bdt.tm_wday)
	     << dendl;
    return false;
  }

  bool time_permits = isbetween_modulo(
      conf->osd_scrub_begin_hour, conf->osd_scrub_end_hour, bdt.tm_hour);
  dout(20) << fmt::format(
		  "{}: should run between {} - {} now {} = {}", __func__,
		  conf->osd_scrub_begin_hour, conf->osd_scrub_end_hour,
		  bdt.tm_hour, (time_permits ? "yes" : "no"))
	   << dendl;
  return time_permits;
}

bool OsdScrub::scrub_time_permit() const
{
  return scrub_time_permit(ceph_clock_now());
}

milliseconds OsdScrub::scrub_sleep_time(bool high_priority_scrub) const
{
  const milliseconds regular_sleep_period =
      milliseconds{int64_t(1'000 * conf->osd_scrub_sleep)};

  if (high_priority_scrub || scrub_time_permit()) {
    return regular_sleep_period;
  }

  // relevant if scrubbing started during allowed time, but continued into
  // forbidden hours
  const milliseconds extended_sleep =
      milliseconds{int64_t(1'000 * conf->osd_scrub_extended_sleep)};
  dout(20)
      << fmt::format(
	     "{}: scrubbing started during allowed time, but continued into "
	     "forbidden hours. regular_sleep_period {} extended_sleep {}",
	     __func__, regular_sleep_period, extended_sleep)
      << dendl;
  return std::max(extended_sleep, regular_sleep_period);
}

// ////////////////////////////////////////////////////////////////////////// //


Scrub::sched_params_t OsdScrub::determine_scrub_time(
    const requested_scrub_t& request_flags,
    const pg_info_t& pg_info,
    const pool_opts_t& pool_conf) const
{
  return m_queue.determine_scrub_time(request_flags, pg_info, pool_conf);
}

void OsdScrub::update_job(
    Scrub::ScrubJobRef sjob,
    const Scrub::sched_params_t& suggested)
{
  m_queue.update_job(sjob, suggested);
}


void OsdScrub::register_with_osd(
    Scrub::ScrubJobRef sjob,
    const Scrub::sched_params_t& suggested)
{
  m_queue.register_with_osd(sjob, suggested);
}

void OsdScrub::remove_from_osd_queue(Scrub::ScrubJobRef sjob)
{
  m_queue.remove_from_osd_queue(sjob);
}

bool OsdScrub::inc_scrubs_local()
{
  return m_resource_bookkeeper.inc_scrubs_local();
}

void OsdScrub::dec_scrubs_local()
{
  m_resource_bookkeeper.dec_scrubs_local();
}

bool OsdScrub::inc_scrubs_remote()
{
  return m_resource_bookkeeper.inc_scrubs_remote();
}

void OsdScrub::dec_scrubs_remote()
{
  m_resource_bookkeeper.dec_scrubs_remote();
}

void OsdScrub::mark_pg_scrub_blocked(spg_t blocked_pg)
{
  m_queue.mark_pg_scrub_blocked(blocked_pg);
}

void OsdScrub::clear_pg_scrub_blocked(spg_t blocked_pg)
{
  m_queue.clear_pg_scrub_blocked(blocked_pg);
}

int OsdScrub::get_blocked_pgs_count() const
{
  return m_queue.get_blocked_pgs_count();
}

void OsdScrub::set_reserving_now()
{
  m_queue.set_reserving_now();
}

void OsdScrub::clear_reserving_now()
{
  m_queue.clear_reserving_now();
}

bool OsdScrub::is_reserving_now() const
{
  return m_queue.is_reserving_now();
}
