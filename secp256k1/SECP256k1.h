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

#ifndef SECP256K1H
#define SECP256K1H

#include "Point.h"
#include <vector>

// Address type
#define P2PKH  0
#define P2SH   1
#define BECH32 2


class Secp256K1 {

public:

  Secp256K1();
  ~Secp256K1();
  void  Init();
  Point ComputePublicKey(Int *privKey);
  Point NextKey(Point &key);
  bool  EC(Point &p);
  
  Point ScalarMultiplication(Point &P,Int *scalar);
  
  char* GetPublicKeyHex(bool compressed, Point &p);
  void GetPublicKeyHex(bool compressed, Point &pubKey,char *dst);
  
  char* GetPublicKeyRaw(bool compressed, Point &p);
  void GetPublicKeyRaw(bool compressed, Point &pubKey,char *dst);
  
  bool ParsePublicKeyHex(char *str,Point &p,bool &isCompressed);

  void GetHash160(int type,bool compressed,
    Point &k0, Point &k1, Point &k2, Point &k3,
    uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3);

  void GetHash160(int type,bool compressed, Point &pubKey, unsigned char *hash);

  void GetHash160_fromX(int type,unsigned char prefix,
  Int *k0,Int *k1,Int *k2,Int *k3,
  uint8_t *h0,uint8_t *h1,uint8_t *h2,uint8_t *h3);

  // Optimized dual-prefix variant for BTC compressed-only scanning.
  // Computes hash160(0x02||X) and hash160(0x03||X) using a single key-buffer build.
  void GetHash160_fromX_02_03(int type,
    Int *k0, Int *k1, Int *k2, Int *k3,
    uint8_t *h02_0, uint8_t *h02_1, uint8_t *h02_2, uint8_t *h02_3,
    uint8_t *h03_0, uint8_t *h03_1, uint8_t *h03_2, uint8_t *h03_3);

  // AVX2 optimized versions (8-way parallel)
  void GetHash160_AVX2(int type,bool compressed,
    Point &k0, Point &k1, Point &k2, Point &k3,
    Point &k4, Point &k5, Point &k6, Point &k7,
    uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3,
    uint8_t *h4, uint8_t *h5, uint8_t *h6, uint8_t *h7);

  void GetHash160_fromX_AVX2(int type,unsigned char prefix,
  Int *k0,Int *k1,Int *k2,Int *k3,
  Int *k4,Int *k5,Int *k6,Int *k7,
  uint8_t *h0,uint8_t *h1,uint8_t *h2,uint8_t *h3,
  uint8_t *h4,uint8_t *h5,uint8_t *h6,uint8_t *h7);

  void GetHash160_fromX_02_03_AVX2(int type,
    Int *k0, Int *k1, Int *k2, Int *k3,
    Int *k4, Int *k5, Int *k6, Int *k7,
    uint8_t *h02_0, uint8_t *h02_1, uint8_t *h02_2, uint8_t *h02_3,
    uint8_t *h02_4, uint8_t *h02_5, uint8_t *h02_6, uint8_t *h02_7,
    uint8_t *h03_0, uint8_t *h03_1, uint8_t *h03_2, uint8_t *h03_3,
    uint8_t *h03_4, uint8_t *h03_5, uint8_t *h03_6, uint8_t *h03_7);

  // AVX-512 optimized versions (16-way parallel)
  void GetHash160_AVX512(int type, bool compressed,
    Point &k0, Point &k1, Point &k2, Point &k3,
    Point &k4, Point &k5, Point &k6, Point &k7,
    Point &k8, Point &k9, Point &k10, Point &k11,
    Point &k12, Point &k13, Point &k14, Point &k15,
    uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3,
    uint8_t *h4, uint8_t *h5, uint8_t *h6, uint8_t *h7,
    uint8_t *h8, uint8_t *h9, uint8_t *h10, uint8_t *h11,
    uint8_t *h12, uint8_t *h13, uint8_t *h14, uint8_t *h15);

  void GetHash160_fromX_AVX512(int type, unsigned char prefix,
    Int *k0, Int *k1, Int *k2, Int *k3,
    Int *k4, Int *k5, Int *k6, Int *k7,
    Int *k8, Int *k9, Int *k10, Int *k11,
    Int *k12, Int *k13, Int *k14, Int *k15,
    uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3,
    uint8_t *h4, uint8_t *h5, uint8_t *h6, uint8_t *h7,
    uint8_t *h8, uint8_t *h9, uint8_t *h10, uint8_t *h11,
    uint8_t *h12, uint8_t *h13, uint8_t *h14, uint8_t *h15);

  void GetHash160_fromX_02_03_AVX512(int type,
    Int *k0, Int *k1, Int *k2, Int *k3,
    Int *k4, Int *k5, Int *k6, Int *k7,
    Int *k8, Int *k9, Int *k10, Int *k11,
    Int *k12, Int *k13, Int *k14, Int *k15,
    uint8_t *h02_0, uint8_t *h02_1, uint8_t *h02_2, uint8_t *h02_3,
    uint8_t *h02_4, uint8_t *h02_5, uint8_t *h02_6, uint8_t *h02_7,
    uint8_t *h02_8, uint8_t *h02_9, uint8_t *h02_10, uint8_t *h02_11,
    uint8_t *h02_12, uint8_t *h02_13, uint8_t *h02_14, uint8_t *h02_15,
    uint8_t *h03_0, uint8_t *h03_1, uint8_t *h03_2, uint8_t *h03_3,
    uint8_t *h03_4, uint8_t *h03_5, uint8_t *h03_6, uint8_t *h03_7,
    uint8_t *h03_8, uint8_t *h03_9, uint8_t *h03_10, uint8_t *h03_11,
    uint8_t *h03_12, uint8_t *h03_13, uint8_t *h03_14, uint8_t *h03_15);


  Point Add(Point &p1, Point &p2);
  Point Add2(Point &p1, Point &p2);
  Point AddDirect(Point &p1, Point &p2);
  Point Double(Point &p);
  Point DoubleDirect(Point &p);
  Point Negation(Point &p);

  Point G;                 // Generator
  Int P;                   // Prime for the finite field
  Int   order;             // Curve order

private:

  uint8_t GetByte(char *str,int idx);
  Int GetY(Int x, bool isEven);
  Point GTable[256*32];       // Generator table

};

#endif // SECP256K1H
