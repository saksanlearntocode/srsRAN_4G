/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2014 The srsLTE Developers. See the
 * COPYRIGHT file at the top-level directory of this distribution.
 *
 * \section LICENSE
 *
 * This file is part of the srsLTE library.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#ifndef UEDL_H
#define UEDL_H

/*******************************************************
 * 
 * This module is a frontend to all the data and control channels processing 
 * modules. 
 ********************************************************/



#include "srslte/ch_estimation/chest_dl.h"
#include "srslte/common/fft.h"
#include "srslte/common/phy_common.h"

#include "srslte/phch/dci.h"
#include "srslte/phch/pcfich.h"
#include "srslte/phch/pdcch.h"
#include "srslte/phch/pdsch.h"
#include "srslte/phch/phich.h"
#include "srslte/phch/ra.h"
#include "srslte/phch/regs.h"

#include "srslte/utils/vector.h"
#include "srslte/utils/debug.h"

#include "srslte/config.h"

#define NOF_HARQ_PROCESSES 8

typedef struct SRSLTE_API {
  pcfich_t pcfich;
  pdcch_t pdcch;
  pdsch_t pdsch;
  harq_t harq_process[NOF_HARQ_PROCESSES];
  regs_t regs;
  srslte_fft_t fft;
  srslte_chest_dl_t chest;
  
  ra_pdsch_t ra_dl;

  srslte_cell_t cell;

  cf_t *sf_symbols; 
  cf_t *ce[SRSLTE_MAX_PORTS];
  
  uint64_t pkt_errors; 
  uint64_t pkts_total;
  uint64_t nof_pdcch_detected; 

  uint16_t current_rnti;
}ue_dl_t;

/* This function shall be called just after the initial synchronization */
SRSLTE_API int ue_dl_init(ue_dl_t *q, 
                          srslte_cell_t cell);

SRSLTE_API void ue_dl_free(ue_dl_t *q);

SRSLTE_API int ue_dl_decode_fft_estimate(ue_dl_t *q, 
                                         cf_t *input, 
                                         uint32_t sf_idx, 
                                         uint32_t *cfi); 

SRSLTE_API int ue_dl_decode_rnti_rv_packet(ue_dl_t *q, 
                                           dci_msg_t *dci_msg, 
                                           uint8_t *data, 
                                           uint32_t cfi, 
                                           uint32_t sf_idx, 
                                           uint16_t rnti, 
                                           uint32_t rvidx); 

SRSLTE_API int ue_dl_find_ul_dci(ue_dl_t *q, 
                                 dci_msg_t *dci_msg, 
                                 uint32_t cfi, 
                                 uint32_t sf_idx, 
                                 uint16_t rnti); 

SRSLTE_API int ue_dl_decode(ue_dl_t * q, 
                            cf_t *input, 
                            uint8_t *data,
                            uint32_t sf_idx);

SRSLTE_API int ue_dl_decode_rnti(ue_dl_t * q, 
                                 cf_t *input, 
                                 uint8_t *data,
                                 uint32_t sf_idx,
                                 uint16_t rnti);

SRSLTE_API int ue_dl_decode_rnti_rv(ue_dl_t * q, 
                                    cf_t *input, 
                                    uint8_t * data,
                                    uint32_t sf_idx, 
                                    uint16_t rnti, 
                                    uint32_t rvidx); 

SRSLTE_API void ue_dl_reset(ue_dl_t *q);

SRSLTE_API void ue_dl_set_rnti(ue_dl_t *q, 
                               uint16_t rnti);

#endif