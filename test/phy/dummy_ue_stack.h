/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#ifndef SRSRAN_DUMMY_UE_STACK_H
#define SRSRAN_DUMMY_UE_STACK_H

#include <srsran/interfaces/ue_nr_interfaces.h>

class ue_dummy_stack : public srsue::stack_interface_phy_nr
{
public:
  struct prach_metrics_t {
    uint32_t count;
  };

  struct metrics_t {
    std::map<uint32_t, prach_metrics_t> prach    = {}; ///< PRACH metrics indexed with premable index
    uint32_t                            sr_count = 0;  ///< Counts number of transmitted SR
  };

private:
  uint16_t                       rnti           = 0;
  bool                           valid          = false;
  uint32_t                       sr_period      = 0;
  uint32_t                       sr_count       = 0;
  uint32_t                       prach_period   = 0;
  uint32_t                       prach_preamble = 0;
  metrics_t                      metrics        = {};
  srsue::phy_interface_stack_nr& phy;

  dummy_tx_harq_entity tx_harq_proc;
  dummy_rx_harq_entity rx_harq_proc;

public:
  struct args_t {
    uint16_t rnti           = 0x1234; ///< C-RNTI for PUSCH and PDSCH transmissions
    uint32_t sr_period      = 0;      ///< Indicates positive SR period in number of opportunities. Set to 0 to disable.
    uint32_t prach_period   = 0;      ///< Requests PHY to transmit PRACH periodically in frames. Set to 0 to disable.
    uint32_t prach_preamble = 0;      ///< Requests PHY to transmit PRACH periodically in frames. Set to 0 to disable.
  };
  ue_dummy_stack(const args_t& args, srsue::phy_interface_stack_nr& phy_) :
    rnti(args.rnti),
    sr_period(args.sr_period),
    prach_period(args.prach_period),
    prach_preamble(args.prach_preamble),
    phy(phy_)
  {
    valid = true;
  }
  ~ue_dummy_stack() = default;
  void in_sync() override {}
  void out_of_sync() override {}
  void run_tti(const uint32_t tti) override
  {
    // Run PRACH
    if (prach_period != 0) {
      uint32_t slot_idx = tti % SRSRAN_NSLOTS_PER_FRAME_NR(srsran_subcarrier_spacing_15kHz);
      uint32_t sfn      = tti / SRSRAN_NSLOTS_PER_FRAME_NR(srsran_subcarrier_spacing_15kHz);
      if (slot_idx == 0 and sfn % prach_period == 0) {
        phy.send_prach(0, prach_preamble, 0.0f, 0.0f);
        metrics.prach[prach_preamble].count++;
      }
    }
  }
  int          sf_indication(const uint32_t tti) override { return 0; }
  sched_rnti_t get_dl_sched_rnti_nr(const uint32_t tti) override { return {rnti, srsran_rnti_type_c}; }
  sched_rnti_t get_ul_sched_rnti_nr(const uint32_t tti) override { return {rnti, srsran_rnti_type_c}; }
  void         new_grant_dl(const uint32_t cc_idx, const mac_nr_grant_dl_t& grant, tb_action_dl_t* action) override
  {
    action->tb.enabled    = true;
    action->tb.softbuffer = &rx_harq_proc[grant.pid].get_softbuffer(grant.ndi);
  }
  void tb_decoded(const uint32_t cc_idx, const mac_nr_grant_dl_t& grant, tb_action_dl_result_t result) override {}
  void new_grant_ul(const uint32_t cc_idx, const mac_nr_grant_ul_t& grant, tb_action_ul_t* action) override
  {
    if (action == nullptr) {
      return;
    }
    action->tb.enabled    = true;
    action->tb.payload    = &tx_harq_proc[grant.pid].get_tb(grant.tbs);
    action->tb.softbuffer = &tx_harq_proc[grant.pid].get_softbuffer(grant.ndi);
  }
  void prach_sent(uint32_t tti, uint32_t s_id, uint32_t t_id, uint32_t f_id, uint32_t ul_carrier_id) override {}
  bool sr_opportunity(uint32_t tti, uint32_t sr_id, bool meas_gap, bool ul_sch_tx) override
  {
    if (sr_period == 0) {
      return false;
    }

    if (sr_count >= (sr_period - 1) and not ul_sch_tx) {
      metrics.sr_count++;
      sr_count = 0;
      return true;
    }

    sr_count++;
    return false;
  }
  bool is_valid() const { return valid; }

  metrics_t get_metrics() { return metrics; }
};

#endif // SRSRAN_DUMMY_UE_STACK_H
