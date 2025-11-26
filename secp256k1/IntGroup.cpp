/*
 * This file is part of the BSGS distribution (https://github.com/JeanLucPons/BSGS).
 * Copyright (c) 2020 Jean Luc PONS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "IntGroup.h"

using namespace std;

IntGroup::IntGroup(int size) {
  this->size = size;
  // Allocate with 64-byte alignment for cache line optimization
  #ifdef _WIN64
  subp = (Int *)_aligned_malloc(size * sizeof(Int), 64);
  #else
  if (posix_memalign((void**)&subp, 64, size * sizeof(Int)) != 0) {
    subp = (Int *)malloc(size * sizeof(Int)); // Fallback if alignment fails
  }
  #endif
}

IntGroup::~IntGroup() {
  #ifdef _WIN64
  _aligned_free(subp);
  #else
  free(subp);
  #endif
}

void IntGroup::Set(Int *pts) {
  ints = pts;
}

// Compute modular inversion of the whole group
void IntGroup::ModInv() {

  Int newValue;
  Int inverse;

  subp[0].Set(&ints[0]);

  // Forward pass with prefetching and loop unrolling for better cache utilization
  int i = 1;
  // Unroll by 4 for better instruction-level parallelism
  for (; i + 3 < size; i += 4) {
    // Prefetch future elements
    if (i + 7 < size) {
      __builtin_prefetch(&ints[i + 7], 0, 3);
      __builtin_prefetch(&subp[i + 7], 1, 3);
    }
    subp[i].ModMulK1(&subp[i - 1], &ints[i]);
    subp[i + 1].ModMulK1(&subp[i], &ints[i + 1]);
    subp[i + 2].ModMulK1(&subp[i + 1], &ints[i + 2]);
    subp[i + 3].ModMulK1(&subp[i + 2], &ints[i + 3]);
  }
  // Handle remaining elements
  for (; i < size; i++) {
    subp[i].ModMulK1(&subp[i - 1], &ints[i]);
  }

  // Do the inversion
  inverse.Set(&subp[size - 1]);
  inverse.ModInv();

  // Backward pass with prefetching
  // Note: Can't easily unroll backward pass due to data dependencies
  for (int i = size - 1; i > 0; i--) {
    // Prefetch upcoming elements
    if (i > 4) {
      __builtin_prefetch(&subp[i - 4], 0, 3);
      __builtin_prefetch(&ints[i - 4], 1, 3);
    }
    newValue.ModMulK1(&subp[i - 1], &inverse);
    inverse.ModMulK1(&ints[i]);
    ints[i].Set(&newValue);
  }

  ints[0].Set(&inverse);

}

// Optimized batch modular inversion with aggressive unrolling
void IntGroup::ModInvOptimized() {

  Int newValue;
  Int inverse;

  subp[0].Set(&ints[0]);

  // Forward pass with 8x unrolling for maximum ILP
  int i = 1;

  // Unroll by 8 for better instruction-level parallelism
  for (; i + 7 < size; i += 8) {
    // Aggressive prefetching - prefetch 16 elements ahead
    if (i + 15 < size) {
      __builtin_prefetch(&ints[i + 15], 0, 3);
      __builtin_prefetch(&subp[i + 15], 1, 3);
    }
    if (i + 23 < size) {
      __builtin_prefetch(&ints[i + 23], 0, 2);
      __builtin_prefetch(&subp[i + 23], 1, 2);
    }

    subp[i].ModMulK1(&subp[i - 1], &ints[i]);
    subp[i + 1].ModMulK1(&subp[i], &ints[i + 1]);
    subp[i + 2].ModMulK1(&subp[i + 1], &ints[i + 2]);
    subp[i + 3].ModMulK1(&subp[i + 2], &ints[i + 3]);
    subp[i + 4].ModMulK1(&subp[i + 3], &ints[i + 4]);
    subp[i + 5].ModMulK1(&subp[i + 4], &ints[i + 5]);
    subp[i + 6].ModMulK1(&subp[i + 5], &ints[i + 6]);
    subp[i + 7].ModMulK1(&subp[i + 6], &ints[i + 7]);
  }

  // Handle remaining elements with 4x unroll
  for (; i + 3 < size; i += 4) {
    subp[i].ModMulK1(&subp[i - 1], &ints[i]);
    subp[i + 1].ModMulK1(&subp[i], &ints[i + 1]);
    subp[i + 2].ModMulK1(&subp[i + 1], &ints[i + 2]);
    subp[i + 3].ModMulK1(&subp[i + 2], &ints[i + 3]);
  }

  // Handle final elements
  for (; i < size; i++) {
    subp[i].ModMulK1(&subp[i - 1], &ints[i]);
  }

  // Single expensive inversion
  inverse.Set(&subp[size - 1]);
  inverse.ModInv();

  // Backward pass with prefetching
  // Use two separate loops for better cache behavior
  for (i = size - 1; i > 7; i--) {
    // Prefetch 8 elements behind
    __builtin_prefetch(&subp[i - 8], 0, 3);
    __builtin_prefetch(&ints[i - 8], 0, 3);

    newValue.ModMulK1(&subp[i - 1], &inverse);
    inverse.ModMulK1(&ints[i]);
    ints[i].Set(&newValue);
  }

  // Final elements without prefetching
  for (; i > 0; i--) {
    newValue.ModMulK1(&subp[i - 1], &inverse);
    inverse.ModMulK1(&ints[i]);
    ints[i].Set(&newValue);
  }

  ints[0].Set(&inverse);

}