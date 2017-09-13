/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2015 Software Radio Systems Limited
 *
 * \section LICENSE
 *
 * This file is part of the srsLTE library.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srslte/phy/ue/ue_dl.h"

#include <string.h>


#define CURRENT_FFTSIZE   srslte_symbol_sz(q->cell.nof_prb)
#define CURRENT_SFLEN     SRSLTE_SF_LEN(CURRENT_FFTSIZE)

#define CURRENT_SLOTLEN_RE SRSLTE_SLOT_LEN_RE(q->cell.nof_prb, q->cell.cp)
#define CURRENT_SFLEN_RE SRSLTE_SF_LEN_RE(q->cell.nof_prb, q->cell.cp)


const static srslte_dci_format_t ue_dci_formats[8][2] = {
    /* Mode 1 */ {SRSLTE_DCI_FORMAT1A, SRSLTE_DCI_FORMAT1},
    /* Mode 2 */ {SRSLTE_DCI_FORMAT1A, SRSLTE_DCI_FORMAT1},
    /* Mode 3 */ {SRSLTE_DCI_FORMAT1A, SRSLTE_DCI_FORMAT2A},
    /* Mode 4 */ {SRSLTE_DCI_FORMAT1A, SRSLTE_DCI_FORMAT2},
    /* Mode 5 */ {SRSLTE_DCI_FORMAT1A, SRSLTE_DCI_FORMAT1D},
    /* Mode 6 */ {SRSLTE_DCI_FORMAT1A, SRSLTE_DCI_FORMAT1B},
    /* Mode 7 */ {SRSLTE_DCI_FORMAT1A, SRSLTE_DCI_FORMAT1},
    /* Mode 8 */ {SRSLTE_DCI_FORMAT1A, SRSLTE_DCI_FORMAT2B}
};

static srslte_dci_format_t common_formats[] = {SRSLTE_DCI_FORMAT1A,SRSLTE_DCI_FORMAT1C};
const uint32_t nof_common_formats = 2; 

int srslte_ue_dl_init(srslte_ue_dl_t *q,
                      srslte_cell_t cell,
                      uint32_t nof_rx_antennas)
{
  int ret = SRSLTE_ERROR_INVALID_INPUTS; 
  
  if (q               != NULL             &&
      nof_rx_antennas <= SRSLTE_MAX_PORTS &&
      srslte_cell_isvalid(&cell))   
  {
    ret = SRSLTE_ERROR;
    
    bzero(q, sizeof(srslte_ue_dl_t));
    
    q->cell = cell; 
    q->pkt_errors = 0;
    q->pkts_total = 0;
    q->pending_ul_dci_rnti = 0; 
    q->sample_offset = 0; 
    q->nof_rx_antennas = nof_rx_antennas; 
    
    if (srslte_ofdm_rx_init(&q->fft, q->cell.cp, q->cell.nof_prb)) {
      fprintf(stderr, "Error initiating FFT\n");
      goto clean_exit;
    }
    if (srslte_chest_dl_init(&q->chest, cell)) {
      fprintf(stderr, "Error initiating channel estimator\n");
      goto clean_exit;
    }
    if (srslte_regs_init(&q->regs, q->cell)) {
      fprintf(stderr, "Error initiating REGs\n");
      goto clean_exit;
    }
    if (srslte_pcfich_init_multi(&q->pcfich, &q->regs, q->cell, nof_rx_antennas)) {
      fprintf(stderr, "Error creating PCFICH object\n");
      goto clean_exit;
    }
    if (srslte_phich_init(&q->phich, &q->regs, q->cell)) {
      fprintf(stderr, "Error creating PHICH object\n");
      goto clean_exit;
    }

    if (srslte_pdcch_init_multi(&q->pdcch, &q->regs, q->cell, nof_rx_antennas)) {
      fprintf(stderr, "Error creating PDCCH object\n");
      goto clean_exit;
    }

    if (srslte_pdsch_init_rx(&q->pdsch, q->cell, nof_rx_antennas)) {
      fprintf(stderr, "Error creating PDSCH object\n");
      goto clean_exit;
    }

    for (int i = 0; i < SRSLTE_MAX_TB; i++) {
      q->softbuffers[i] = srslte_vec_malloc(sizeof(srslte_softbuffer_rx_t));
      if (!q->softbuffers[i]) {
        fprintf(stderr, "Error allocating soft buffer\n");
        goto clean_exit;
      }

      if (srslte_softbuffer_rx_init(q->softbuffers[i], q->cell.nof_prb)) {
        fprintf(stderr, "Error initiating soft buffer\n");
        goto clean_exit;
      }
    }
    if (srslte_cfo_init(&q->sfo_correct, q->cell.nof_prb*SRSLTE_NRE)) {
      fprintf(stderr, "Error initiating SFO correct\n");
      goto clean_exit;
    }
    srslte_cfo_set_tol(&q->sfo_correct, 1e-5f/q->fft.symbol_sz);
    
    for (int j=0;j<nof_rx_antennas;j++) {
      q->sf_symbols_m[j] = srslte_vec_malloc(CURRENT_SFLEN_RE * sizeof(cf_t));
      if (!q->sf_symbols_m[j]) {
        perror("malloc");
        goto clean_exit; 
      }
      for (uint32_t i=0;i<q->cell.nof_ports;i++) {
        q->ce_m[i][j] = srslte_vec_malloc(CURRENT_SFLEN_RE * sizeof(cf_t));
        if (!q->ce_m[i][j]) {
          perror("malloc");
          goto clean_exit; 
        }
      }
    }
    
    q->sf_symbols = q->sf_symbols_m[0];
    for (int i=0;i<q->cell.nof_ports;i++) {
      q->ce[i] = q->ce_m[i][0];
    }
    
    ret = SRSLTE_SUCCESS;
  } else {
    fprintf(stderr, "Invalid cell properties: Id=%d, Ports=%d, PRBs=%d\n",
            cell.id, cell.nof_ports, cell.nof_prb);      
  }

clean_exit: 
  if (ret == SRSLTE_ERROR) {
    srslte_ue_dl_free(q);
  }
  return ret;
}

void srslte_ue_dl_free(srslte_ue_dl_t *q) {
  if (q) {
    srslte_ofdm_rx_free(&q->fft);
    srslte_chest_dl_free(&q->chest);
    srslte_regs_free(&q->regs);
    srslte_pcfich_free(&q->pcfich);
    srslte_phich_free(&q->phich);
    srslte_pdcch_free(&q->pdcch);
    srslte_pdsch_free(&q->pdsch);
    srslte_cfo_free(&q->sfo_correct);
    for (int i = 0; i < SRSLTE_MAX_TB; i++) {
      srslte_softbuffer_rx_free(q->softbuffers[i]);
      if (q->softbuffers[i]) {
        free(q->softbuffers[i]);
      }
    }
    for (int j=0;j<q->nof_rx_antennas;j++) {
      if (q->sf_symbols_m[j]) {
        free(q->sf_symbols_m[j]);
      }
      for (uint32_t i=0;i<q->cell.nof_ports;i++) {
        if (q->ce_m[i][j]) {
          free(q->ce_m[i][j]);
        }
      }
    }
    bzero(q, sizeof(srslte_ue_dl_t));
  }
}

/* Precalculate the PDSCH scramble sequences for a given RNTI. This function takes a while 
 * to execute, so shall be called once the final C-RNTI has been allocated for the session.
 * For the connection procedure, use srslte_pusch_encode_rnti() or srslte_pusch_decode_rnti() functions 
 */
void srslte_ue_dl_set_rnti(srslte_ue_dl_t *q, uint16_t rnti) {
  srslte_pdsch_set_rnti(&q->pdsch, rnti);
  
  // Compute UE-specific and Common search space for this RNTI
  for (int cfi=0;cfi<3;cfi++) {
    for (int sf_idx=0;sf_idx<10;sf_idx++) {
      q->current_ss_ue[cfi][sf_idx].nof_locations = srslte_pdcch_ue_locations(&q->pdcch, q->current_ss_ue[cfi][sf_idx].loc, MAX_CANDIDATES_UE, sf_idx, cfi+1, rnti);              
    }
    q->current_ss_common[cfi].nof_locations = srslte_pdcch_common_locations(&q->pdcch, q->current_ss_common[cfi].loc, MAX_CANDIDATES_COM, cfi+1);
  }  
  
  q->current_rnti = rnti; 
}

void srslte_ue_dl_reset(srslte_ue_dl_t *q) {
  for(int i = 0; i < SRSLTE_MAX_CODEWORDS; i++){
    srslte_softbuffer_rx_reset(q->softbuffers[i]);
  }
  bzero(&q->pdsch_cfg, sizeof(srslte_pdsch_cfg_t));
}

void srslte_ue_dl_set_sample_offset(srslte_ue_dl_t * q, float sample_offset) {
  q->sample_offset = sample_offset; 
}

/** Applies the following operations to a subframe of synchronized samples: 
 *    - OFDM demodulation
 *    - Channel estimation 
 *    - PCFICH decoding
 *    - PDCCH decoding: Find DCI for RNTI given by previous call to srslte_ue_dl_set_rnti()
 *    - PDSCH decoding: Decode TB scrambling with RNTI given by srslte_ue_dl_set_rnti()
 */
int srslte_ue_dl_decode(srslte_ue_dl_t *q, cf_t *input[SRSLTE_MAX_PORTS], uint8_t *data[SRSLTE_MAX_CODEWORDS],
                              uint32_t tm, uint32_t tti, bool acks[SRSLTE_MAX_CODEWORDS]) {
  return srslte_ue_dl_decode_rnti(q, input, data, tm, tti, q->current_rnti, acks);
}

int srslte_ue_dl_decode_fft_estimate(srslte_ue_dl_t *q, cf_t *input[SRSLTE_MAX_PORTS], uint32_t sf_idx, uint32_t *cfi)
{
  if (input && q && cfi && sf_idx < SRSLTE_NSUBFRAMES_X_FRAME) {
    
    /* Run FFT for all subframe data */
    for (int j=0;j<q->nof_rx_antennas;j++) {
      srslte_ofdm_rx_sf(&q->fft, input[j], q->sf_symbols_m[j]);

      /* Correct SFO multiplying by complex exponential in the time domain */
      if (q->sample_offset) {
        for (int i=0;i<2*SRSLTE_CP_NSYMB(q->cell.cp);i++) {
          srslte_cfo_correct(&q->sfo_correct, 
                          &q->sf_symbols_m[j][i*q->cell.nof_prb*SRSLTE_NRE], 
                          &q->sf_symbols_m[j][i*q->cell.nof_prb*SRSLTE_NRE], 
                          q->sample_offset / q->fft.symbol_sz);
        }
      }
    }
    return srslte_ue_dl_decode_estimate(q, sf_idx, cfi); 
  } else {
    return SRSLTE_ERROR_INVALID_INPUTS; 
  }
}

int srslte_ue_dl_decode_estimate(srslte_ue_dl_t *q, uint32_t sf_idx, uint32_t *cfi) {
  float cfi_corr; 
  if (q && cfi && sf_idx < SRSLTE_NSUBFRAMES_X_FRAME) {
    
    /* Get channel estimates for each port */
    srslte_chest_dl_estimate_multi(&q->chest, q->sf_symbols_m, q->ce_m, sf_idx, q->nof_rx_antennas);

    /* First decode PCFICH and obtain CFI */
    if (srslte_pcfich_decode_multi(&q->pcfich, q->sf_symbols_m, q->ce_m, 
                             srslte_chest_dl_get_noise_estimate(&q->chest), 
                             sf_idx, cfi, &cfi_corr)<0) {
      fprintf(stderr, "Error decoding PCFICH\n");
      return SRSLTE_ERROR;
    }

    INFO("Decoded CFI=%d with correlation %.2f, sf_idx=%d\n", *cfi, cfi_corr, sf_idx);

    if (srslte_regs_set_cfi(&q->regs, *cfi)) {
      fprintf(stderr, "Error setting CFI\n");
      return SRSLTE_ERROR;
    }
    
    return SRSLTE_SUCCESS; 
  } else {
    return SRSLTE_ERROR_INVALID_INPUTS; 
  }
}


int srslte_ue_dl_cfg_grant(srslte_ue_dl_t *q, srslte_ra_dl_grant_t *grant, uint32_t cfi, uint32_t sf_idx,
                                 int rvidx[SRSLTE_MAX_CODEWORDS], srslte_mimo_type_t mimo_type) {
  uint32_t pmi = 0;

  /* Translates Precoding Information (pinfo) to Precoding matrix Index (pmi) as 3GPP 36.212 Table 5.3.3.1.5-4 */
  if (mimo_type == SRSLTE_MIMO_TYPE_SPATIAL_MULTIPLEX) {
    if (grant->nof_tb == 1) {
      if (grant->pinfo > 0 && grant->pinfo < 5) {
        pmi = grant->pinfo - 1;
      } else {
        ERROR("Not Implemented (nof_tb=%d, pinfo=%d)", q->pdsch_cfg.grant.nof_tb, grant->pinfo);
        return SRSLTE_ERROR;
      }
    } else {
      if (grant->pinfo < 2) {
        pmi = grant->pinfo;
      } else {
        ERROR("Not Implemented (nof_tb=%d, pinfo=%d)", q->pdsch_cfg.grant.nof_tb, grant->pinfo);
        return SRSLTE_ERROR;
      }
    }
  }
  return srslte_pdsch_cfg_mimo(&q->pdsch_cfg, q->cell, grant, cfi, sf_idx, rvidx, mimo_type, pmi);
}

int srslte_ue_dl_decode_rnti(srslte_ue_dl_t *q, cf_t *input[SRSLTE_MAX_PORTS],
                             uint8_t *data[SRSLTE_MAX_CODEWORDS], uint32_t tm, uint32_t tti, uint16_t rnti,
                             bool acks[SRSLTE_MAX_CODEWORDS]) {
  srslte_mimo_type_t mimo_type;
  srslte_dci_msg_t dci_msg;
  srslte_ra_dl_dci_t dci_unpacked;
  srslte_ra_dl_grant_t grant; 
  int ret = SRSLTE_ERROR; 
  uint32_t cfi;
  uint32_t sf_idx = tti%10;
  
  if ((ret = srslte_ue_dl_decode_fft_estimate(q, input, sf_idx, &cfi)) < 0) {
    return ret; 
  }
  
  float noise_estimate = srslte_chest_dl_get_noise_estimate(&q->chest);
  // Uncoment next line to do ZF by default in pdsch_ue example
  //float noise_estimate = 0; 

  if (srslte_pdcch_extract_llr_multi(&q->pdcch, q->sf_symbols_m, q->ce_m, noise_estimate, sf_idx, cfi)) {
    fprintf(stderr, "Error extracting LLRs\n");
    return SRSLTE_ERROR;
  }

  int found_dci = srslte_ue_dl_find_dl_dci(q, tm, cfi, sf_idx, rnti, &dci_msg);
  if (found_dci == 1) {
    
    if (srslte_dci_msg_to_dl_grant(&dci_msg, rnti, q->cell.nof_prb, q->cell.nof_ports, &dci_unpacked, &grant)) {
      fprintf(stderr, "Error unpacking DCI\n");
      return SRSLTE_ERROR;   
    }
    
    /* ===== These lines of code are supposed to be MAC functionality === */

    
    int rvidx[SRSLTE_MAX_CODEWORDS] = {1};
    if (dci_unpacked.rv_idx < 0) {
      uint32_t sfn = tti/10; 
      uint32_t k   = (sfn/2)%4;
      for (int i = 0; i < grant.nof_tb; i++) {
        rvidx[i] = ((uint32_t) ceilf((float) 1.5 * k)) % 4;
        srslte_softbuffer_rx_reset_tbs(q->softbuffers[i], (uint32_t) grant.mcs[i].tbs);
      }
    } else {
      for (int i = 0; i < grant.nof_tb; i++) {
        switch(i) {
          case 0:
            rvidx[i] = (uint32_t) dci_unpacked.rv_idx;
            break;
          case 1:
            rvidx[i] = (uint32_t) dci_unpacked.rv_idx_1;
            break;
          default:
            ERROR("Wrong number of transport blocks");
            return SRSLTE_ERROR;
        }
        srslte_softbuffer_rx_reset_tbs(q->softbuffers[i], (uint32_t) grant.mcs[i].tbs);
      }
    }

    switch(dci_msg.format) {
      case SRSLTE_DCI_FORMAT1:
      case SRSLTE_DCI_FORMAT1A:
        if (q->cell.nof_ports == 1) {
          mimo_type = SRSLTE_MIMO_TYPE_SINGLE_ANTENNA;
        } else {
          mimo_type = SRSLTE_MIMO_TYPE_TX_DIVERSITY;
        }
        break;
      case SRSLTE_DCI_FORMAT2:
        if (grant.nof_tb == 1 && dci_unpacked.pinfo == 0) {
          mimo_type = SRSLTE_MIMO_TYPE_TX_DIVERSITY;
        } else {
          mimo_type = SRSLTE_MIMO_TYPE_SPATIAL_MULTIPLEX;
        }
        break;
      case SRSLTE_DCI_FORMAT2A:
        if (grant.nof_tb == 1 && dci_unpacked.pinfo == 0) {
          mimo_type = SRSLTE_MIMO_TYPE_TX_DIVERSITY;
        } else {
          mimo_type = SRSLTE_MIMO_TYPE_CDD;
        }
        break;

      /* Not implemented formats */
      case SRSLTE_DCI_FORMAT0:
      case SRSLTE_DCI_FORMAT1C:
      case SRSLTE_DCI_FORMAT1B:
      case SRSLTE_DCI_FORMAT1D:
      case SRSLTE_DCI_FORMAT2B:
      default:
        ERROR("Transmission mode not supported.");
        return SRSLTE_ERROR;
    }

    if (srslte_ue_dl_cfg_grant(q, &grant, cfi, sf_idx, rvidx, mimo_type)) {
      ERROR("Configuing PDSCH");
      return SRSLTE_ERROR; 
    }
    
    /* ===== End of MAC functionality ========== */

    q->nof_detected++;
  
    
    if (q->pdsch_cfg.grant.mcs[0].mod > 0 && q->pdsch_cfg.grant.mcs[0].tbs >= 0) {
      ret = srslte_pdsch_decode(&q->pdsch, &q->pdsch_cfg, q->softbuffers,
                                q->sf_symbols_m, q->ce_m,
                                noise_estimate,
                                rnti, data, acks);

      for (int tb = 0; tb < q->pdsch_cfg.grant.nof_tb; tb++) {
        if (!acks[tb]) {
          q->pkt_errors++;
        }
        q->pkts_total++;
      }

      if (ret == SRSLTE_ERROR) {
      } else if (ret == SRSLTE_ERROR_INVALID_INPUTS) {
        fprintf(stderr, "Error calling srslte_pdsch_decode()\n");      
      }

      /* If we are in TM4 (Closed-Loop MIMO), compute condition number */

    }
  
  /*
    printf("Saving signal...\n");
    srslte_vec_save_file("input", input, sizeof(cf_t)*SRSLTE_SF_LEN_PRB(q->cell.nof_prb));
    srslte_ue_dl_save_signal(q, &q->softbuffer, sf_idx, rvidx, rnti, cfi);
    //exit(-1);
  */
  
  }

  if (found_dci == 1 && ret == SRSLTE_SUCCESS) { 
    return q->pdsch_cfg.grant.mcs[0].tbs;
  } else {
    return 0;
  }
}

/* Compute the Rank Indicator (RI) and Precoder Matrix Indicator (PMI) by computing the Signal to Interference plus
 * Noise Ratio (SINR), valid for TM4 */
int srslte_ue_dl_ri_pmi_select(srslte_ue_dl_t *q, uint32_t *ri, uint32_t *pmi, float *current_sinr) {
  float noise_estimate = srslte_chest_dl_get_noise_estimate(&q->chest);
  float best_sinr = -INFINITY;
  uint32_t best_pmi = 0, best_ri = 0;

  if (q->cell.nof_ports < 2 || q->nof_rx_antennas < 2) {
    /* Do nothing */
    return SRSLTE_SUCCESS;
  } else if (q->cell.nof_ports == 2 && q->nof_rx_antennas == 2) {
    if (srslte_pdsch_pmi_select(&q->pdsch, &q->pdsch_cfg, q->ce_m, noise_estimate,
                                  SRSLTE_SF_LEN_RE(q->cell.nof_prb, q->cell.cp), q->pmi, q->sinr)) {
      ERROR("SINR calculation error");
      return SRSLTE_ERROR;
    }

    /* Select the best Rank indicator (RI) and Precoding Matrix Indicator (PMI) */
    for (uint32_t nof_layers = 1; nof_layers <= 2; nof_layers++) {
      float _sinr = q->sinr[nof_layers - 1][q->pmi[nof_layers - 1]] * nof_layers;
      if (_sinr > best_sinr + 0.1) {
        best_sinr = _sinr;
        best_pmi = q->pmi[nof_layers - 1];
        best_ri = nof_layers;
      }
    }

    /* Set RI */
    if (ri != NULL) {
      *ri = best_ri;
    }

    /* Set PMI */
    if (pmi != NULL) {
      *pmi = best_pmi;
    }

    /* Set current SINR */
    if (current_sinr != NULL && q->pdsch_cfg.mimo_type == SRSLTE_MIMO_TYPE_SPATIAL_MULTIPLEX) {
      if (q->pdsch_cfg.nof_layers == 1) {
        *current_sinr = q->sinr[0][q->pdsch_cfg.codebook_idx];
      } else if (q->pdsch_cfg.nof_layers == 2) {
        *current_sinr = q->sinr[1][q->pdsch_cfg.codebook_idx - 1];
      } else {
        ERROR("Not implemented number of layers (%d)", q->pdsch_cfg.nof_layers);
        return SRSLTE_ERROR;
      }
    }

    /* Print Trace */
    if (ri != NULL && pmi != NULL && current_sinr != NULL) {
      INFO("PDSCH Select RI=%d; PMI=%d; Current SINR=%.1fdB (nof_layers=%d, codebook_idx=%d)\n", *ri, *pmi,
           10*log10(*current_sinr), q->pdsch_cfg.nof_layers, q->pdsch_cfg.codebook_idx);
    }
  } else {
    ERROR("Not implemented configuration");
    return SRSLTE_ERROR_INVALID_INPUTS;
  }

  return SRSLTE_SUCCESS;
}


/* Compute the Rank Indicator (RI) by computing the condition number, valid for TM3 */
int srslte_ue_dl_ri_select(srslte_ue_dl_t *q, uint32_t *ri, float *cn) {
  float _cn;
  int ret = srslte_pdsch_cn_compute(&q->pdsch, q->ce_m, SRSLTE_SF_LEN_RE(q->cell.nof_prb, q->cell.cp), &_cn);

  /* Set Condition number */
  if (cn) {
    *cn = _cn;
  }

  /* Set rank indicator */
  if (!ret && ri) {
    *ri = (_cn > 3.0f)? 1:0;
  }

  return ret;
}


uint32_t srslte_ue_dl_get_ncce(srslte_ue_dl_t *q) {
  return q->last_location.ncce; 
}

static int dci_blind_search(srslte_ue_dl_t *q, dci_blind_search_t *search_space, uint16_t rnti, srslte_dci_msg_t *dci_msg) 
{
  int ret = SRSLTE_ERROR; 
  uint16_t crc_rem = 0; 
  if (rnti) {
    ret = 0; 
    int i=0;
    while (!ret && i < search_space->nof_locations) {
      DEBUG("Searching format %s in %d,%d (%d/%d)\n", 
             srslte_dci_format_string(search_space->format), search_space->loc[i].ncce, search_space->loc[i].L, 
             i, search_space->nof_locations);
      
      if (srslte_pdcch_decode_msg(&q->pdcch, dci_msg, &search_space->loc[i], search_space->format, &crc_rem)) {
        fprintf(stderr, "Error decoding DCI msg\n");
        return SRSLTE_ERROR;
      }
      if (crc_rem == rnti) {        
        // If searching for Format1A but found Format0 save it for later 
        if (dci_msg->format == SRSLTE_DCI_FORMAT0 && search_space->format == SRSLTE_DCI_FORMAT1A) 
        {
          if (!q->pending_ul_dci_rnti) {
            q->pending_ul_dci_rnti = crc_rem; 
            memcpy(&q->pending_ul_dci_msg, dci_msg, sizeof(srslte_dci_msg_t));          
            memcpy(&q->last_location_ul, &search_space->loc[i], sizeof(srslte_dci_location_t));          
          }
        // Else if we found it, save location and leave
        } else if (dci_msg->format == search_space->format) {
          ret = 1; 
          if (dci_msg->format == SRSLTE_DCI_FORMAT0) {
            memcpy(&q->last_location_ul, &search_space->loc[i], sizeof(srslte_dci_location_t));          
          } else {
            memcpy(&q->last_location, &search_space->loc[i], sizeof(srslte_dci_location_t));          
          }
        } 
      }
      i++; 
    }    
  } else {
    fprintf(stderr, "RNTI not specified\n");
  }
  return ret; 
}

int srslte_ue_dl_find_ul_dci(srslte_ue_dl_t *q, uint32_t cfi, uint32_t sf_idx, uint16_t rnti, srslte_dci_msg_t *dci_msg)
{
  if (rnti && cfi > 0 && cfi < 4) {
    /* Do not search if an UL DCI is already pending */    
    if (q->pending_ul_dci_rnti == rnti) {
      q->pending_ul_dci_rnti = 0;      
      memcpy(dci_msg, &q->pending_ul_dci_msg, sizeof(srslte_dci_msg_t));
      return 1; 
    }
    
    // Configure and run DCI blind search 
    dci_blind_search_t search_space; 
    dci_blind_search_t *current_ss = &search_space;
    if (q->current_rnti == rnti) {
      current_ss = &q->current_ss_ue[cfi-1][sf_idx];
    } else {
      // If locations are not pre-generated, generate them now
      current_ss->nof_locations = srslte_pdcch_ue_locations(&q->pdcch, current_ss->loc, MAX_CANDIDATES_UE, sf_idx, cfi, rnti);        
    }
    
    srslte_pdcch_set_cfi(&q->pdcch, cfi);
    
    current_ss->format = SRSLTE_DCI_FORMAT0; 
    INFO("Searching UL C-RNTI in %d ue locations\n", search_space.nof_locations);
    return dci_blind_search(q, current_ss, rnti, dci_msg);
  } else {
    return 0; 
  }
}

int srslte_ue_dl_find_dl_dci(srslte_ue_dl_t *q, uint32_t tm, uint32_t cfi, uint32_t sf_idx, uint16_t rnti, srslte_dci_msg_t *dci_msg)
{
  srslte_rnti_type_t rnti_type; 
  if (rnti == SRSLTE_SIRNTI) {
    rnti_type = SRSLTE_RNTI_SI;
  } else if (rnti == SRSLTE_PRNTI) {
    rnti_type = SRSLTE_RNTI_PCH;    
  } else if (rnti <= SRSLTE_RARNTI_END) {
    rnti_type = SRSLTE_RNTI_RAR;    
  } else {
    rnti_type = SRSLTE_RNTI_USER;
  }
  return srslte_ue_dl_find_dl_dci_type(q, tm, cfi, sf_idx, rnti, rnti_type, dci_msg);
}

// Blind search for SI/P/RA-RNTI
static int find_dl_dci_type_siprarnti(srslte_ue_dl_t *q, uint32_t cfi, uint16_t rnti, srslte_dci_msg_t *dci_msg)
{
  int ret = 0; 
  // Configure and run DCI blind search 
  dci_blind_search_t search_space; 
  search_space.nof_locations = srslte_pdcch_common_locations(&q->pdcch, search_space.loc, MAX_CANDIDATES_COM, cfi);
  INFO("Searching SI/P/RA-RNTI in %d common locations, %d formats\n", search_space.nof_locations, nof_common_formats);
  // Search for RNTI only if there is room for the common search space 
  if (search_space.nof_locations > 0) {    
    for (int f=0;f<nof_common_formats;f++) {
      search_space.format = common_formats[f];   
      if ((ret = dci_blind_search(q, &search_space, rnti, dci_msg))) {
        return ret; 
      }
    }
  }
  return SRSLTE_SUCCESS;   
}

// Blind search for C-RNTI
static int find_dl_dci_type_crnti(srslte_ue_dl_t *q, uint32_t tm, uint32_t cfi,
                                  uint32_t sf_idx, uint16_t rnti, srslte_dci_msg_t *dci_msg) {
  int ret = SRSLTE_SUCCESS; 
  dci_blind_search_t search_space; 
  dci_blind_search_t *current_ss = &search_space;

  if (cfi < 1 || cfi > 3) {
    ERROR("CFI must be 1 ≤ cfi ≤ 3", cfi);
    return SRSLTE_ERROR;
  }

  // Search UE-specific search space 
  if (q->current_rnti == rnti) {
    current_ss = &q->current_ss_ue[cfi-1][sf_idx];
  } else {
    // If locations are not pre-generated, generate them now
    current_ss->nof_locations = srslte_pdcch_ue_locations(&q->pdcch, current_ss->loc, MAX_CANDIDATES_UE, sf_idx, cfi, rnti);        
  }
  
  srslte_pdcch_set_cfi(&q->pdcch, cfi);

  for (int f = 0; f < 2; f++) {
    srslte_dci_format_t format = ue_dci_formats[tm][f];

    INFO("Searching DL C-RNTI %s in %d ue locations\n", srslte_dci_format_string(format),
         current_ss->nof_locations);

    current_ss->format = format;
    if ((ret = dci_blind_search(q, current_ss, rnti, dci_msg))) {
      return ret; 
    }
  }

  // Search Format 1A in the Common SS also
  if (q->current_rnti == rnti) {
    current_ss = &q->current_ss_common[cfi-1];
  } else {
    // If locations are not pre-generated, generate them now
    current_ss->nof_locations = srslte_pdcch_common_locations(&q->pdcch, current_ss->loc, MAX_CANDIDATES_COM, cfi);
  }
  
  srslte_pdcch_set_cfi(&q->pdcch, cfi);
  
  // Search for RNTI only if there is room for the common search space 
  if (current_ss->nof_locations > 0) {    
    current_ss->format = SRSLTE_DCI_FORMAT1A; 
    INFO("Searching DL C-RNTI in %d ue locations, format 1A\n", current_ss->nof_locations);
    return dci_blind_search(q, current_ss, rnti, dci_msg);   
  }
  return SRSLTE_SUCCESS; 
}

int srslte_ue_dl_find_dl_dci_type(srslte_ue_dl_t *q, uint32_t tm, uint32_t cfi, uint32_t sf_idx,
                                  uint16_t rnti, srslte_rnti_type_t rnti_type, srslte_dci_msg_t *dci_msg)
{  
  if (rnti_type == SRSLTE_RNTI_SI || rnti_type == SRSLTE_RNTI_PCH || rnti_type == SRSLTE_RNTI_RAR) {
    return find_dl_dci_type_siprarnti(q, cfi, rnti, dci_msg);
  } else {
    return find_dl_dci_type_crnti(q, tm, cfi, sf_idx, rnti, dci_msg);
  }
}

bool srslte_ue_dl_decode_phich(srslte_ue_dl_t *q, uint32_t sf_idx, uint32_t n_prb_lowest, uint32_t n_dmrs)
{
  uint8_t ack_bit; 
  float distance;
  uint32_t ngroup, nseq; 
  srslte_phich_calc(&q->phich, n_prb_lowest, n_dmrs, &ngroup, &nseq);
  INFO("Decoding PHICH sf_idx=%d, n_prb_lowest=%d, n_dmrs=%d, n_group=%d, n_seq=%d, Ngroups=%d, Nsf=%d\n", 
    sf_idx, n_prb_lowest, n_dmrs, ngroup, nseq, 
    srslte_phich_ngroups(&q->phich), srslte_phich_nsf(&q->phich));
  
  cf_t *ce0[SRSLTE_MAX_PORTS];
  for (int i=0;i<SRSLTE_MAX_PORTS;i++) {
    ce0[i] = q->ce_m[i][0];
  }

  
  if (!srslte_phich_decode(&q->phich, q->sf_symbols_m[0], ce0, 0, ngroup, nseq, sf_idx, &ack_bit, &distance)) {
    INFO("Decoded PHICH %d with distance %f\n", ack_bit, distance);    
  } else {
    fprintf(stderr, "Error decoding PHICH\n");
    return false; 
  }
  if (ack_bit) {
    return true; 
  } else {
    return false; 
  }
}

void srslte_ue_dl_save_signal(srslte_ue_dl_t *q, srslte_softbuffer_rx_t *softbuffer, uint32_t tti, uint32_t rv_idx, uint16_t rnti, uint32_t cfi) {
  srslte_vec_save_file("sf_symbols", q->sf_symbols_m, SRSLTE_SF_LEN_RE(q->cell.nof_prb, q->cell.cp)*sizeof(cf_t));
  printf("%d samples\n", SRSLTE_SF_LEN_RE(q->cell.nof_prb, q->cell.cp));
  srslte_vec_save_file("ce0", q->ce_m[0], SRSLTE_SF_LEN_RE(q->cell.nof_prb, q->cell.cp)*sizeof(cf_t));
  if (q->cell.nof_ports > 1) {
    srslte_vec_save_file("ce1", q->ce_m[1], SRSLTE_SF_LEN_RE(q->cell.nof_prb, q->cell.cp)*sizeof(cf_t));
  }
  srslte_vec_save_file("pcfich_ce0", q->pcfich.ce[0], q->pcfich.nof_symbols*sizeof(cf_t));
  srslte_vec_save_file("pcfich_ce1", q->pcfich.ce[1], q->pcfich.nof_symbols*sizeof(cf_t));
  srslte_vec_save_file("pcfich_symbols", q->pcfich.symbols[0], q->pcfich.nof_symbols*sizeof(cf_t));
  srslte_vec_save_file("pcfich_eq_symbols", q->pcfich.d, q->pcfich.nof_symbols*sizeof(cf_t));
  srslte_vec_save_file("pcfich_llr", q->pcfich.data_f, PCFICH_CFI_LEN*sizeof(float));
  
  srslte_vec_save_file("pdcch_ce0", q->pdcch.ce[0], q->pdcch.nof_cce*36*sizeof(cf_t));
  srslte_vec_save_file("pdcch_ce1", q->pdcch.ce[1], q->pdcch.nof_cce*36*sizeof(cf_t));
  srslte_vec_save_file("pdcch_symbols", q->pdcch.symbols[0], q->pdcch.nof_cce*36*sizeof(cf_t));
  srslte_vec_save_file("pdcch_eq_symbols", q->pdcch.d, q->pdcch.nof_cce*36*sizeof(cf_t));
  srslte_vec_save_file("pdcch_llr", q->pdcch.llr, q->pdcch.nof_cce*72*sizeof(float));
  
  
  srslte_vec_save_file("pdsch_symbols", q->pdsch.d, q->pdsch_cfg.nbits[0].nof_re*sizeof(cf_t));
  srslte_vec_save_file("llr", q->pdsch.e, q->pdsch_cfg.nbits[0].nof_bits*sizeof(cf_t));
  int cb_len = q->pdsch_cfg.cb_segm[0].K1;
  for (int i=0;i<q->pdsch_cfg.cb_segm[0].C;i++) {
    char tmpstr[64]; 
    snprintf(tmpstr,64,"rmout_%d.dat",i);
    srslte_vec_save_file(tmpstr, softbuffer->buffer_f[i], (3*cb_len+12)*sizeof(int16_t));  
  }
  printf("Saved files for tti=%d, sf=%d, cfi=%d, mcs=%d, rv=%d, rnti=0x%x\n", tti, tti%10, cfi, 
         q->pdsch_cfg.grant.mcs[0].idx, rv_idx, rnti);
}



