/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2014 The libLTE Developers. See the
 * COPYRIGHT file at the top-level directory of this distribution.
 *
 * \section LICENSE
 *
 * This file is part of the libLTE library.
 *
 * libLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include <strings.h>
#include <complex.h>
#include <math.h>

#include "srslte/utils/debug.h"
#include "srslte/common/phy_common.h"
#include "srslte/sync/sync.h"
#include "srslte/utils/vector.h"
#include "srslte/sync/cfo.h"

#define MEANPEAK_EMA_ALPHA      0.2
#define CFO_EMA_ALPHA           0.01
#define CP_EMA_ALPHA            0.2

static bool fft_size_isvalid(uint32_t fft_size) {
  if (fft_size >= FFT_SIZE_MIN && fft_size <= FFT_SIZE_MAX && (fft_size%64) == 0) {
    return true;
  } else {
    return false;
  }
}

int sync_init(sync_t *q, uint32_t frame_size, uint32_t fft_size) {

  int ret = SRSLTE_ERROR_INVALID_INPUTS; 
  
  if (q                 != NULL         &&
      frame_size        <= 307200       &&
      fft_size_isvalid(fft_size))
  {
    ret = SRSLTE_ERROR; 
    
    bzero(q, sizeof(sync_t));
    q->detect_cp = true;
    q->cp = CPNORM;
    q->mean_peak_value = 0.0;
    q->sss_en = true;
    q->correct_cfo = true; 
    q->mean_cfo = 0; 
    q->N_id_2 = 1000; 
    q->N_id_1 = 1000;
    q->fft_size = fft_size;
    q->frame_size = frame_size;
    q->sss_alg = SSS_PARTIAL_3; 
    
    if (pss_synch_init_fft(&q->pss, frame_size, fft_size)) {
      fprintf(stderr, "Error initializing PSS object\n");
      goto clean_exit;
    }
    if (sss_synch_init(&q->sss, fft_size)) {
      fprintf(stderr, "Error initializing SSS object\n");
      goto clean_exit;
    }

    if (cfo_init(&q->cfocorr, frame_size)) {
      fprintf(stderr, "Error initiating CFO\n");
      goto clean_exit;
    }

    DEBUG("SYNC init with frame_size=%d and fft_size=%d\n", frame_size, fft_size);
    
    ret = SRSLTE_SUCCESS;
  }  else {
    fprintf(stderr, "Invalid parameters frame_size: %d, fft_size: %d\n", frame_size, fft_size);
  }
  
clean_exit: 
  if (ret == SRSLTE_ERROR) {
    sync_free(q);
  }
  return ret;
}

void sync_free(sync_t *q) {
  if (q) {
    pss_synch_free(&q->pss);     
    sss_synch_free(&q->sss);  
    cfo_free(&q->cfocorr);
  }
}

void sync_set_threshold(sync_t *q, float threshold) {
  q->threshold = threshold;
}

void sync_sss_en(sync_t *q, bool enabled) {
  q->sss_en = enabled;
}

bool sync_sss_detected(sync_t *q) {
  return lte_N_id_1_isvalid(q->N_id_1);
}

int sync_get_cell_id(sync_t *q) {
  if (lte_N_id_2_isvalid(q->N_id_2) && lte_N_id_1_isvalid(q->N_id_1)) {
    return q->N_id_1*3 + q->N_id_2;      
  } else {
    return -1; 
  }
}

int sync_set_N_id_2(sync_t *q, uint32_t N_id_2) {
  if (lte_N_id_2_isvalid(N_id_2)) {
    q->N_id_2 = N_id_2;    
    return SRSLTE_SUCCESS;
  } else {
    fprintf(stderr, "Invalid N_id_2=%d\n", N_id_2);
    return SRSLTE_ERROR_INVALID_INPUTS;
  }
}

uint32_t sync_get_sf_idx(sync_t *q) {
  return q->sf_idx;
}

float sync_get_cfo(sync_t *q) {
  return q->mean_cfo;
}

float sync_get_last_peak_value(sync_t *q) {
  return q->peak_value;
}

float sync_get_peak_value(sync_t *q) {
  return q->mean_peak_value;
}

void sync_correct_cfo(sync_t *q, bool enabled) {
  q->correct_cfo = enabled;
}

void sync_cp_en(sync_t *q, bool enabled) {
  q->detect_cp = enabled;
}

bool sync_sss_is_en(sync_t *q) {
  return q->sss_en;
}

void sync_set_em_alpha(sync_t *q, float alpha) {
  pss_synch_set_ema_alpha(&q->pss, alpha);
}

srslte_cp_t sync_get_cp(sync_t *q) {
  return q->cp;
}
void sync_set_cp(sync_t *q, srslte_cp_t cp) {
  q->cp = cp;
}

void sync_set_sss_algorithm(sync_t *q, sss_alg_t alg) {
  q->sss_alg = alg; 
}

/* CP detection algorithm taken from: 
 * "SSS Detection Method for Initial Cell Search in 3GPP LTE FDD/TDD Dual Mode Receiver"
 * by Jung-In Kim et al. 
 */
srslte_cp_t sync_detect_cp(sync_t *q, cf_t *input, uint32_t peak_pos) 
{
  float R_norm, R_ext, C_norm, C_ext; 
  float M_norm=0, M_ext=0; 
  
  uint32_t cp_norm_len = CP_NORM(7, q->fft_size);
  uint32_t cp_ext_len = CP_EXT(q->fft_size);
 
  cf_t *input_cp_norm = &input[peak_pos-2*(q->fft_size+cp_norm_len)]; 
  cf_t *input_cp_ext = &input[peak_pos-2*(q->fft_size+cp_ext_len)]; 

  for (int i=0;i<2;i++) {
    R_norm  = crealf(vec_dot_prod_conj_ccc(&input_cp_norm[q->fft_size], input_cp_norm, cp_norm_len));    
    C_norm  = cp_norm_len * vec_avg_power_cf(input_cp_norm, cp_norm_len);    
    input_cp_norm += q->fft_size+cp_norm_len;
    M_norm += R_norm/C_norm;
  }
  
  q->M_norm_avg = VEC_EMA(M_norm/2, q->M_norm_avg, CP_EMA_ALPHA);
  
  for (int i=0;i<2;i++) {
    R_ext  = crealf(vec_dot_prod_conj_ccc(&input_cp_ext[q->fft_size], input_cp_ext, cp_ext_len));
    C_ext  = cp_ext_len * vec_avg_power_cf(input_cp_ext, cp_ext_len);
    input_cp_ext += q->fft_size+cp_ext_len;
    if (C_ext > 0) {
      M_ext += R_ext/C_ext;      
    }
  }

  q->M_ext_avg = VEC_EMA(M_ext/2, q->M_ext_avg, CP_EMA_ALPHA);

  if (q->M_norm_avg > q->M_ext_avg) {
    return CPNORM;    
  } else if (q->M_norm_avg < q->M_ext_avg) {
    return CPEXT;
  } else {
    if (R_norm > R_ext) {
      return CPNORM;
    } else {
      return CPEXT;
    }
  }
}

/* Returns 1 if the SSS is found, 0 if not and -1 if there is not enough space 
 * to correlate
 */
int sync_sss(sync_t *q, cf_t *input, uint32_t peak_pos, srslte_cp_t cp) {
  int sss_idx, ret;

  sss_synch_set_N_id_2(&q->sss, q->N_id_2);

  /* Make sure we have enough room to find SSS sequence */
  sss_idx = (int) peak_pos-2*q->fft_size-CP(q->fft_size, (CP_ISNORM(q->cp)?CPNORM_LEN:CPEXT_LEN));
  if (sss_idx < 0) {
    INFO("Not enough room to decode CP SSS (sss_idx=%d, peak_pos=%d)\n", sss_idx, peak_pos);
    return SRSLTE_ERROR;
  }
  DEBUG("Searching SSS around sss_idx: %d, peak_pos: %d\n", sss_idx, peak_pos);
      
  switch(q->sss_alg) {
    case SSS_DIFF:
      sss_synch_m0m1_diff(&q->sss, &input[sss_idx], &q->m0, &q->m0_value, &q->m1, &q->m1_value);
      break;
    case SSS_PARTIAL_3:
      sss_synch_m0m1_partial(&q->sss, &input[sss_idx], 3, NULL, &q->m0, &q->m0_value, &q->m1, &q->m1_value);
      break;
    case SSS_FULL:
      sss_synch_m0m1_partial(&q->sss, &input[sss_idx], 1, NULL, &q->m0, &q->m0_value, &q->m1, &q->m1_value);
      break;
  }

  q->sf_idx = sss_synch_subframe(q->m0, q->m1);
  ret = sss_synch_N_id_1(&q->sss, q->m0, q->m1);
  if (ret >= 0) {
    q->N_id_1 = (uint32_t) ret;
    DEBUG("SSS detected N_id_1=%d, sf_idx=%d, %s CP\n",
      q->N_id_1, q->sf_idx, CP_ISNORM(q->cp)?"Normal":"Extended");
    return 1;
  } else {
    q->N_id_1 = 1000;
    return SRSLTE_SUCCESS;
  }
}


/** Finds the PSS sequence previously defined by a call to sync_set_N_id_2()
 * around the position find_offset in the buffer input. 
 * Returns 1 if the correlation peak exceeds the threshold set by sync_set_threshold() 
 * or 0 otherwise. Returns a negative number on error (if N_id_2 has not been set) 
 * 
 * The maximum of the correlation peak is always stored in *peak_position
 */
int sync_find(sync_t *q, cf_t *input, uint32_t find_offset, uint32_t *peak_position) 
{
  
  int ret = SRSLTE_ERROR_INVALID_INPUTS; 
  
  if (q                 != NULL     &&
      input             != NULL     &&
      lte_N_id_2_isvalid(q->N_id_2) && 
      fft_size_isvalid(q->fft_size))
  {
    int peak_pos;
    
    ret = SRSLTE_SUCCESS; 
    
    if (peak_position) {
      *peak_position = 0; 
    }

    pss_synch_set_N_id_2(&q->pss, q->N_id_2);
  
    peak_pos = pss_synch_find_pss(&q->pss, &input[find_offset], &q->peak_value);
    if (peak_pos < 0) {
      fprintf(stderr, "Error calling finding PSS sequence\n");
      return SRSLTE_ERROR; 
    }
    q->mean_peak_value = VEC_EMA(q->peak_value, q->mean_peak_value, MEANPEAK_EMA_ALPHA);

    if (peak_position) {
      *peak_position = (uint32_t) peak_pos;
    }
    
    /* If peak is over threshold, compute CFO and SSS */
    if (q->peak_value >= q->threshold) {
      
      // Make sure we have enough space to estimate CFO
      if (peak_pos + find_offset >= q->fft_size) {
        float cfo = pss_synch_cfo_compute(&q->pss, &input[find_offset+peak_pos-q->fft_size]);

        /* compute cumulative moving average CFO */
        q->mean_cfo = VEC_EMA(cfo, q->mean_cfo, CFO_EMA_ALPHA);
      } else {
        INFO("No space for CFO computation. Frame starts at \n",peak_pos);
      }

      if (q->detect_cp) {
        if (peak_pos + find_offset >= 2*(q->fft_size + CP_EXT(q->fft_size))) {
          q->cp = sync_detect_cp(q, input, peak_pos + find_offset);
        } else {
          INFO("Not enough room to detect CP length. Peak position: %d\n", peak_pos);
        }
      }
  
      // Try to detect SSS 
      if (q->sss_en) {
        /* Correct CFO with the averaged CFO estimation */
        if (q->mean_cfo && q->correct_cfo) {
          cfo_correct(&q->cfocorr, input, input, -q->mean_cfo / q->fft_size);                 
        }
        
        // Set an invalid N_id_1 indicating SSS is yet to be detected
        q->N_id_1 = 1000; 
        
        if (sync_sss(q, input, find_offset + peak_pos, q->cp) < 0) {
          INFO("No space for SSS processing. Frame starts at %d\n", peak_pos);
        }
      }
      // Return 1 (peak detected) even if we couldn't estimate CFO and SSS
      ret = 1;
    } else {
      ret = 0;
    }
    
    INFO("SYNC ret=%d N_id_2=%d find_offset=%d pos=%d peak=%.2f threshold=%.2f sf_idx=%d, CFO=%.3f KHz\n",
         ret, q->N_id_2, find_offset, peak_pos, q->peak_value, q->threshold, q->sf_idx, 15*q->mean_cfo);

  } else if (lte_N_id_2_isvalid(q->N_id_2)) {
    fprintf(stderr, "Must call sync_set_N_id_2() first!\n");
  }
  
  return ret; 
}

void sync_reset(sync_t *q) {
  q->M_ext_avg = 0; 
  q->M_norm_avg = 0; 
  pss_synch_reset(&q->pss);
}
