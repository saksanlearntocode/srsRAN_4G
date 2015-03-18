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


#ifndef DFT_H_
#define DFT_H_
 
#include <stdbool.h>
#include "srslte/config.h"


/* Generic DFT module.
 * Supports one-dimensional complex and real transforms. Options are set
 * using the dft_plan_set_x functions.
 *
 * Options (default is false):
 * mirror - Rearranges negative and positive frequency bins. Swaps after
 *            transform for FORWARD, swaps before transform for BACKWARD.
 * db     - Provides output in dB (10*log10(x)).
 * norm   - Normalizes output (by sqrt(len) for complex, len for real).
 * dc     - Handles insertion and removal of null DC carrier internally.
 */

typedef enum {
  COMPLEX, REAL
}dft_mode_t;

typedef enum {
  FORWARD, BACKWARD
}dft_dir_t;

typedef struct SRSLTE_API {
  int size;           // DFT length
  void *in;           // Input buffer
  void *out;          // Output buffer
  void *p;            // DFT plan
  bool forward;       // Forward transform?
  bool mirror;        // Shift negative and positive frequencies?
  bool db;            // Provide output in dB?
  bool norm;          // Normalize output?
  bool dc;            // Handle insertion/removal of null DC carrier internally?
  dft_dir_t dir;     // Forward/Backward
  dft_mode_t mode;   // Complex/Real
}srslte_dft_plan_t;

typedef _Complex float dft_c_t;
typedef float dft_r_t;

/* Create DFT plans */

SRSLTE_API int dft_plan(srslte_dft_plan_t *plan, const int dft_points, dft_dir_t dir,
                         dft_mode_t type);
SRSLTE_API int dft_plan_c(srslte_dft_plan_t *plan, const int dft_points, dft_dir_t dir);
SRSLTE_API int dft_plan_r(srslte_dft_plan_t *plan, const int dft_points, dft_dir_t dir);
SRSLTE_API void dft_plan_free(srslte_dft_plan_t *plan);

/* Set options */

SRSLTE_API void dft_plan_set_mirror(srslte_dft_plan_t *plan, bool val);
SRSLTE_API void dft_plan_set_db(srslte_dft_plan_t *plan, bool val);
SRSLTE_API void dft_plan_set_norm(srslte_dft_plan_t *plan, bool val);
SRSLTE_API void dft_plan_set_dc(srslte_dft_plan_t *plan, bool val);

/* Compute DFT */

SRSLTE_API void dft_run(srslte_dft_plan_t *plan, void *in, void *out);
SRSLTE_API void dft_run_c(srslte_dft_plan_t *plan, dft_c_t *in, dft_c_t *out);
SRSLTE_API void dft_run_r(srslte_dft_plan_t *plan, dft_r_t *in, dft_r_t *out);

#endif // DFT_H_

