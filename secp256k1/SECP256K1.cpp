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

#include <cstdio>
#include <cstring>
#include "SECP256k1.h"
#include "Point.h"
#include "../util.h"
#include "../hash/sha256.h"
#include "../hash/sha256_avx2.h"
#include "../hash/ripemd160.h"

Secp256K1::Secp256K1() {
}

void Secp256K1::Init() {
  // Prime for the finite field
  P.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F");

  // Set up field
  Int::SetupField(&P);

  // Generator point and order
  G.x.SetBase16("79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798");
  G.y.SetBase16("483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8");
  G.z.SetInt32(1);
  order.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");

  Int::InitK1(&order);

  // Compute Generator table
  Point N(G);
  for(int i = 0; i < 32; i++) {
    GTable[i * 256] = N;
    N = DoubleDirect(N);
    for (int j = 1; j < 255; j++) {
      GTable[i * 256 + j] = N;
      N = AddDirect(N, GTable[i * 256]);
    }
    GTable[i * 256 + 255] = N; // Dummy point for check function
  }

}

Secp256K1::~Secp256K1() {
}

void Secp256K1::ExportGTable(uint8_t *out_xy_be) {
  if (!out_xy_be) return;
  const size_t total = 256 * 32;
  for (size_t i = 0; i < total; i++) {
    GTable[i].x.Get32Bytes(out_xy_be + i * 64);
    GTable[i].y.Get32Bytes(out_xy_be + i * 64 + 32);
  }
}

Point Secp256K1::ComputePublicKey(Int *privKey) {
  int i = 0;
  uint8_t b;
  Point Q;
  Q.Clear();
  // Search first significant byte
  for (i = 0; i < 32; i++) {
    b = privKey->GetByte(i);
    if(b)
      break;
  }
  Q = GTable[256 * i + (b-1)];
  i++;

  for(; i < 32; i++) {
    b = privKey->GetByte(i);
    if(b)
      Q = Add2(Q, GTable[256 * i + (b-1)]);
  }
  Q.Reduce();
  return Q;
}

Point Secp256K1::NextKey(Point &key) {
  // Input key must be reduced and different from G
  // in order to use AddDirect
  return AddDirect(key,G);
}

uint8_t Secp256K1::GetByte(char *str, int idx) {
  char tmp[3];
  int  val;
  tmp[0] = str[2 * idx];
  tmp[1] = str[2 * idx + 1];
  tmp[2] = 0;
  if (sscanf(tmp, "%X", &val) != 1) {
    printf("ParsePublicKeyHex: Error invalid public key specified (unexpected hexadecimal digit)\n");
    exit(-1);
  }
  return (uint8_t)val;
}

Point Secp256K1::Negation(Point &p) {
  Point Q;
  Q.Clear();
  Q.x.Set(&p.x);
  Q.y.Set(&this->P);
  Q.y.Sub(&p.y);
  Q.z.SetInt32(1);
  return Q;
}


bool Secp256K1::ParsePublicKeyHex(char *str,Point &ret,bool &isCompressed) {
  int len = strlen(str);
  ret.Clear();
  if (len < 2) {
    printf("ParsePublicKeyHex: Error invalid public key specified (66 or 130 character length)\n");
    return false;
  }
  uint8_t type = GetByte(str, 0);
  switch (type) {
    case 0x02:
      if (len != 66) {
        printf("ParsePublicKeyHex: Error invalid public key specified (66 character length)\n");
        return false;
      }
      for (int i = 0; i < 32; i++)
        ret.x.SetByte(31 - i, GetByte(str, i + 1));
      ret.y = GetY(ret.x, true);
      isCompressed = true;
      break;

    case 0x03:
      if (len != 66) {
        printf("ParsePublicKeyHex: Error invalid public key specified (66 character length)\n");
        return false;
      }
      for (int i = 0; i < 32; i++)
        ret.x.SetByte(31 - i, GetByte(str, i + 1));
      ret.y = GetY(ret.x, false);
      isCompressed = true;
      break;

    case 0x04:
      if (len != 130) {
        printf("ParsePublicKeyHex: Error invalid public key specified (130 character length)\n");
        exit(-1);
      }
      for (int i = 0; i < 32; i++)
        ret.x.SetByte(31 - i, GetByte(str, i + 1));
      for (int i = 0; i < 32; i++)
        ret.y.SetByte(31 - i, GetByte(str, i + 33));
      isCompressed = false;
      break;

    default:
      printf("ParsePublicKeyHex: Error invalid public key specified (Unexpected prefix (only 02,03 or 04 allowed)\n");
      return false;
  }

  ret.z.SetInt32(1);

  if (!EC(ret)) {
    printf("ParsePublicKeyHex: Error invalid public key specified (Not lie on elliptic curve)\n");
    return false;
  }

  return true;
}

char* Secp256K1::GetPublicKeyHex(bool compressed, Point &pubKey) {
  unsigned char publicKeyBytes[65];
  char *ret = NULL;
  if (!compressed) {
    //Uncompressed public key
    publicKeyBytes[0] = 0x4;
    pubKey.x.Get32Bytes(publicKeyBytes + 1);
    pubKey.y.Get32Bytes(publicKeyBytes + 33);
    ret = (char*) tohex((char*)publicKeyBytes,65);
  }
  else {
    // Compressed public key
    publicKeyBytes[0] = pubKey.y.IsEven() ? 0x2 : 0x3;
    pubKey.x.Get32Bytes(publicKeyBytes + 1);
    ret = (char*) tohex((char*)publicKeyBytes,33);
  }
  return ret;
}

void Secp256K1::GetPublicKeyHex(bool compressed, Point &pubKey,char *dst){
  unsigned char publicKeyBytes[65];
  if (!compressed) {
    //Uncompressed public key
    publicKeyBytes[0] = 0x4;
    pubKey.x.Get32Bytes(publicKeyBytes + 1);
    pubKey.y.Get32Bytes(publicKeyBytes + 33);
    tohex_dst((char*)publicKeyBytes,65,dst);
  }
  else {
    // Compressed public key
    publicKeyBytes[0] = pubKey.y.IsEven() ? 0x2 : 0x3;
    pubKey.x.Get32Bytes(publicKeyBytes + 1);
	tohex_dst((char*)publicKeyBytes,33,dst);
  }
}

char* Secp256K1::GetPublicKeyRaw(bool compressed, Point &pubKey) {
  char *ret = (char*) malloc(65);
  if(ret == NULL) {
    ::fprintf(stderr,"Can't alloc memory\n");
    exit(0);
  }
  if (!compressed) {
    //Uncompressed public key
    ret[0] = 0x4;
    pubKey.x.Get32Bytes((unsigned char*) (ret + 1));
    pubKey.y.Get32Bytes((unsigned char*) (ret + 33));
  }
  else {
    // Compressed public key
    ret[0] = pubKey.y.IsEven() ? 0x2 : 0x3;
    pubKey.x.Get32Bytes((unsigned char*) (ret + 1));
  }
  return ret;
}

void Secp256K1::GetPublicKeyRaw(bool compressed, Point &pubKey,char *dst) {
  if (!compressed) {
    //Uncompressed public key
    dst[0] = 0x4;
    pubKey.x.Get32Bytes((unsigned char*) (dst + 1));
    pubKey.y.Get32Bytes((unsigned char*) (dst + 33));
  }
  else {
    // Compressed public key
    dst[0] = pubKey.y.IsEven() ? 0x2 : 0x3;
    pubKey.x.Get32Bytes((unsigned char*) (dst + 1));
  }
}

Point Secp256K1::AddDirect(Point &p1,Point &p2) {
  Int _s;
  Int _p;
  Int dy;
  Int dx;
  Point r;
  r.z.SetInt32(1);

  dy.ModSub(&p2.y,&p1.y);
  dx.ModSub(&p2.x,&p1.x);
  dx.ModInv();
  _s.ModMulK1(&dy,&dx);     // s = (p2.y-p1.y)*inverse(p2.x-p1.x);

  _p.ModSquareK1(&_s);       // _p = pow2(s)

  r.x.ModSub(&_p,&p1.x);
  r.x.ModSub(&p2.x);       // rx = pow2(s) - p1.x - p2.x;

  r.y.ModSub(&p2.x,&r.x);
  r.y.ModMulK1(&_s);
  r.y.ModSub(&p2.y);       // ry = - p2.y - s*(ret.x-p2.x);

  return r;
}


Point Secp256K1::Add2(Point &p1, Point &p2) {
  // P2.z = 1
  Int u;
  Int v;
  Int u1;
  Int v1;
  Int vs2;
  Int vs3;
  Int us2;
  Int a;
  Int us2w;
  Int vs2v2;
  Int vs3u2;
  Int _2vs2v2;
  Point r;
  u1.ModMulK1(&p2.y, &p1.z);
  v1.ModMulK1(&p2.x, &p1.z);
  u.ModSub(&u1, &p1.y);
  v.ModSub(&v1, &p1.x);
  us2.ModSquareK1(&u);
  vs2.ModSquareK1(&v);
  vs3.ModMulK1(&vs2, &v);
  us2w.ModMulK1(&us2, &p1.z);
  vs2v2.ModMulK1(&vs2, &p1.x);
  _2vs2v2.ModAdd(&vs2v2, &vs2v2);
  a.ModSub(&us2w, &vs3);
  a.ModSub(&_2vs2v2);

  r.x.ModMulK1(&v, &a);

  vs3u2.ModMulK1(&vs3, &p1.y);
  r.y.ModSub(&vs2v2, &a);
  r.y.ModMulK1(&r.y, &u);
  r.y.ModSub(&vs3u2);

  r.z.ModMulK1(&vs3, &p1.z);

  return r;
}

Point Secp256K1::Add(Point &p1,Point &p2) {
  Int u;
  Int v;
  Int u1;
  Int u2;
  Int v1;
  Int v2;
  Int vs2;
  Int vs3;
  Int us2;
  Int w;
  Int a;
  Int us2w;
  Int vs2v2;
  Int vs3u2;
  Int _2vs2v2;
  Int x3;
  Int vs3y1;
  Point r;

  /*
  U1 = Y2 * Z1
  U2 = Y1 * Z2
  V1 = X2 * Z1
  V2 = X1 * Z2
  if (V1 == V2)
    if (U1 != U2)
      return POINT_AT_INFINITY
    else
      return POINT_DOUBLE(X1, Y1, Z1)
  U = U1 - U2
  V = V1 - V2
  W = Z1 * Z2
  A = U ^ 2 * W - V ^ 3 - 2 * V ^ 2 * V2
  X3 = V * A
  Y3 = U * (V ^ 2 * V2 - A) - V ^ 3 * U2
  Z3 = V ^ 3 * W
  return (X3, Y3, Z3)
  */

  u1.ModMulK1(&p2.y,&p1.z);
  u2.ModMulK1(&p1.y,&p2.z);
  v1.ModMulK1(&p2.x,&p1.z);
  v2.ModMulK1(&p1.x,&p2.z);
  u.ModSub(&u1,&u2);
  v.ModSub(&v1,&v2);
  w.ModMulK1(&p1.z,&p2.z);
  us2.ModSquareK1(&u);
  vs2.ModSquareK1(&v);
  vs3.ModMulK1(&vs2,&v);
  us2w.ModMulK1(&us2,&w);
  vs2v2.ModMulK1(&vs2,&v2);
  _2vs2v2.ModAdd(&vs2v2,&vs2v2);
  a.ModSub(&us2w,&vs3);
  a.ModSub(&_2vs2v2);

  r.x.ModMulK1(&v,&a);

  vs3u2.ModMulK1(&vs3,&u2);
  r.y.ModSub(&vs2v2,&a);
  r.y.ModMulK1(&r.y,&u);
  r.y.ModSub(&vs3u2);

  r.z.ModMulK1(&vs3,&w);

  return r;
}

Point Secp256K1::DoubleDirect(Point &p) {
  Int _s;
  Int _p;
  Int a;
  Point r;
  r.z.SetInt32(1);
  _s.ModMulK1(&p.x,&p.x);
  _p.ModAdd(&_s,&_s);
  _p.ModAdd(&_s);

  a.ModAdd(&p.y,&p.y);
  a.ModInv();
  _s.ModMulK1(&_p,&a);     // s = (3*pow2(p.x))*inverse(2*p.y);

  _p.ModMulK1(&_s,&_s);
  a.ModAdd(&p.x,&p.x);
  a.ModNeg();
  r.x.ModAdd(&a,&_p);    // rx = pow2(s) + neg(2*p.x);

  a.ModSub(&r.x,&p.x);

  _p.ModMulK1(&a,&_s);
  r.y.ModAdd(&_p,&p.y);
  r.y.ModNeg();           // ry = neg(p.y + s*(ret.x+neg(p.x)));
  return r;
}

Point Secp256K1::Double(Point &p) {
  /*
  if (Y == 0)
    return POINT_AT_INFINITY
    W = a * Z ^ 2 + 3 * X ^ 2
    S = Y * Z
    B = X * Y*S
    H = W ^ 2 - 8 * B
    X' = 2*H*S
    Y' = W*(4*B - H) - 8*Y^2*S^2
    Z' = 8*S^3
    return (X', Y', Z')
  */
  Int z2;
  Int x2;
  Int _3x2;
  Int w;
  Int s;
  Int s2;
  Int b;
  Int _8b;
  Int _8y2s2;
  Int y2;
  Int h;
  Point r;
  z2.ModSquareK1(&p.z);
  z2.SetInt32(0); // a=0
  x2.ModSquareK1(&p.x);
  _3x2.ModAdd(&x2,&x2);
  _3x2.ModAdd(&x2);
  w.ModAdd(&z2,&_3x2);
  s.ModMulK1(&p.y,&p.z);
  b.ModMulK1(&p.y,&s);
  b.ModMulK1(&p.x);
  h.ModSquareK1(&w);
  _8b.ModAdd(&b,&b);
  _8b.ModDouble();
  _8b.ModDouble();
  h.ModSub(&_8b);
  r.x.ModMulK1(&h,&s);
  r.x.ModAdd(&r.x);
  s2.ModSquareK1(&s);
  y2.ModSquareK1(&p.y);
  _8y2s2.ModMulK1(&y2,&s2);
  _8y2s2.ModDouble();
  _8y2s2.ModDouble();
  _8y2s2.ModDouble();
  r.y.ModAdd(&b,&b);
  r.y.ModAdd(&r.y,&r.y);
  r.y.ModSub(&h);
  r.y.ModMulK1(&w);
  r.y.ModSub(&_8y2s2);
  r.z.ModMulK1(&s2,&s);
  r.z.ModDouble();
  r.z.ModDouble();
  r.z.ModDouble();
  return r;
}

Int Secp256K1::GetY(Int x,bool isEven) {
  Int _s;
  Int _p;
  _s.ModSquareK1(&x);
  _p.ModMulK1(&_s,&x);
  _p.ModAdd(7);
  _p.ModSqrt();
  if(!_p.IsEven() && isEven) {
    _p.ModNeg();
  }
  else if(_p.IsEven() && !isEven) {
    _p.ModNeg();
  }
  return _p;
}

bool Secp256K1::EC(Point &p) {
  Int _s;
  Int _p;
  _s.ModSquareK1(&p.x);
  _p.ModMulK1(&_s,&p.x);
  _p.ModAdd(7);
  _s.ModMulK1(&p.y,&p.y);
  _s.ModSub(&_p);
  return _s.IsZero(); // ( ((pow2(y) - (pow3(x) + 7)) % P) == 0 );
}

Point Secp256K1::ScalarMultiplication(Point &P,Int *scalar)	{
	Point R,Q,T;
	int  no_of_bits, loop;
	no_of_bits = scalar->GetBitLength();
	R.Clear();
	R.z.SetInt32(1);
	if(!scalar->IsZero())	{
		Q.Set(P);
		if(scalar->GetBit(0) == 1)	{
			R.Set(P);
		}
		for(loop = 1; loop < no_of_bits; loop++) {
			T = Double(Q);
			Q.Set(T);
			T.Set(R);
			if(scalar->GetBit(loop)){
				R = Add(T,Q);
			}
		}
	}
	R.Reduce();
	return R;
}

#define KEYBUFFCOMP(buff,p) \
(buff)[0] = ((p).x.bits[7] >> 8) | ((uint32_t)(0x2 + (p).y.IsOdd()) << 24); \
(buff)[1] = ((p).x.bits[6] >> 8) | ((p).x.bits[7] <<24); \
(buff)[2] = ((p).x.bits[5] >> 8) | ((p).x.bits[6] <<24); \
(buff)[3] = ((p).x.bits[4] >> 8) | ((p).x.bits[5] <<24); \
(buff)[4] = ((p).x.bits[3] >> 8) | ((p).x.bits[4] <<24); \
(buff)[5] = ((p).x.bits[2] >> 8) | ((p).x.bits[3] <<24); \
(buff)[6] = ((p).x.bits[1] >> 8) | ((p).x.bits[2] <<24); \
(buff)[7] = ((p).x.bits[0] >> 8) | ((p).x.bits[1] <<24); \
(buff)[8] = 0x00800000 | ((p).x.bits[0] <<24); \
(buff)[9] = 0; \
(buff)[10] = 0; \
(buff)[11] = 0; \
(buff)[12] = 0; \
(buff)[13] = 0; \
(buff)[14] = 0; \
(buff)[15] = 0x108;

#define KEYBUFFUNCOMP(buff,p) \
(buff)[0] = ((p).x.bits[7] >> 8) | 0x04000000; \
(buff)[1] = ((p).x.bits[6] >> 8) | ((p).x.bits[7] <<24); \
(buff)[2] = ((p).x.bits[5] >> 8) | ((p).x.bits[6] <<24); \
(buff)[3] = ((p).x.bits[4] >> 8) | ((p).x.bits[5] <<24); \
(buff)[4] = ((p).x.bits[3] >> 8) | ((p).x.bits[4] <<24); \
(buff)[5] = ((p).x.bits[2] >> 8) | ((p).x.bits[3] <<24); \
(buff)[6] = ((p).x.bits[1] >> 8) | ((p).x.bits[2] <<24); \
(buff)[7] = ((p).x.bits[0] >> 8) | ((p).x.bits[1] <<24); \
(buff)[8] = ((p).y.bits[7] >> 8) | ((p).x.bits[0] <<24); \
(buff)[9] = ((p).y.bits[6] >> 8) | ((p).y.bits[7] <<24); \
(buff)[10] = ((p).y.bits[5] >> 8) | ((p).y.bits[6] <<24); \
(buff)[11] = ((p).y.bits[4] >> 8) | ((p).y.bits[5] <<24); \
(buff)[12] = ((p).y.bits[3] >> 8) | ((p).y.bits[4] <<24); \
(buff)[13] = ((p).y.bits[2] >> 8) | ((p).y.bits[3] <<24); \
(buff)[14] = ((p).y.bits[1] >> 8) | ((p).y.bits[2] <<24); \
(buff)[15] = ((p).y.bits[0] >> 8) | ((p).y.bits[1] <<24); \
(buff)[16] = 0x00800000 | ((p).y.bits[0] <<24); \
(buff)[17] = 0; \
(buff)[18] = 0; \
(buff)[19] = 0; \
(buff)[20] = 0; \
(buff)[21] = 0; \
(buff)[22] = 0; \
(buff)[23] = 0; \
(buff)[24] = 0; \
(buff)[25] = 0; \
(buff)[26] = 0; \
(buff)[27] = 0; \
(buff)[28] = 0; \
(buff)[29] = 0; \
(buff)[30] = 0; \
(buff)[31] = 0x208;

#define KEYBUFFSCRIPT(buff,h) \
(buff)[0] = 0x00140000 | (uint32_t)h[0] << 8 | (uint32_t)h[1]; \
(buff)[1] = (uint32_t)h[2] << 24 | (uint32_t)h[3] << 16 | (uint32_t)h[4] << 8 | (uint32_t)h[5];\
(buff)[2] = (uint32_t)h[6] << 24 | (uint32_t)h[7] << 16 | (uint32_t)h[8] << 8 | (uint32_t)h[9];\
(buff)[3] = (uint32_t)h[10] << 24 | (uint32_t)h[11] << 16 | (uint32_t)h[12] << 8 | (uint32_t)h[13];\
(buff)[4] = (uint32_t)h[14] << 24 | (uint32_t)h[15] << 16 | (uint32_t)h[16] << 8 | (uint32_t)h[17];\
(buff)[5] = (uint32_t)h[18] << 24 | (uint32_t)h[19] << 16 | 0x8000; \
(buff)[6] = 0; \
(buff)[7] = 0; \
(buff)[8] = 0; \
(buff)[9] = 0; \
(buff)[10] = 0; \
(buff)[11] = 0; \
(buff)[12] = 0; \
(buff)[13] = 0; \
(buff)[14] = 0; \
(buff)[15] = 0xB0;


void Secp256K1::GetHash160(int type,bool compressed,
  Point &k0,Point &k1,Point &k2,Point &k3,
  uint8_t *h0,uint8_t *h1,uint8_t *h2,uint8_t *h3) {

#ifdef WIN64
  __declspec(align(16)) unsigned char sh0[64];
  __declspec(align(16)) unsigned char sh1[64];
  __declspec(align(16)) unsigned char sh2[64];
  __declspec(align(16)) unsigned char sh3[64];
#else
  unsigned char sh0[64] __attribute__((aligned(16)));
  unsigned char sh1[64] __attribute__((aligned(16)));
  unsigned char sh2[64] __attribute__((aligned(16)));
  unsigned char sh3[64] __attribute__((aligned(16)));
#endif

  switch (type) {

  case P2PKH:
  case BECH32:
  {

    if (!compressed) {

      uint32_t b0[32];
      uint32_t b1[32];
      uint32_t b2[32];
      uint32_t b3[32];

      KEYBUFFUNCOMP(b0, k0);
      KEYBUFFUNCOMP(b1, k1);
      KEYBUFFUNCOMP(b2, k2);
      KEYBUFFUNCOMP(b3, k3);

      sha256sse_2B(b0, b1, b2, b3, sh0, sh1, sh2, sh3);
      ripemd160sse_32(sh0, sh1, sh2, sh3, h0, h1, h2, h3);

    } else {

      uint32_t b0[16];
      uint32_t b1[16];
      uint32_t b2[16];
      uint32_t b3[16];

      KEYBUFFCOMP(b0, k0);
      KEYBUFFCOMP(b1, k1);
      KEYBUFFCOMP(b2, k2);
      KEYBUFFCOMP(b3, k3);

      sha256sse_1B(b0, b1, b2, b3, sh0, sh1, sh2, sh3);
      ripemd160sse_32(sh0, sh1, sh2, sh3, h0, h1, h2, h3);

    }

  }
  break;

  case P2SH:
  {

    unsigned char kh0[20];
    unsigned char kh1[20];
    unsigned char kh2[20];
    unsigned char kh3[20];

    GetHash160(P2PKH,compressed,k0,k1,k2,k3,kh0,kh1,kh2,kh3);

    // Redeem Script (1 to 1 P2SH)
    uint32_t b0[16];
    uint32_t b1[16];
    uint32_t b2[16];
    uint32_t b3[16];

    KEYBUFFSCRIPT(b0, kh0);
    KEYBUFFSCRIPT(b1, kh1);
    KEYBUFFSCRIPT(b2, kh2);
    KEYBUFFSCRIPT(b3, kh3);

    sha256sse_1B(b0, b1, b2, b3, sh0, sh1, sh2, sh3);
    ripemd160sse_32(sh0, sh1, sh2, sh3, h0, h1, h2, h3);

  }
  break;

  }
}



void Secp256K1::GetHash160(int type, bool compressed, Point &pubKey, unsigned char *hash) {

  unsigned char shapk[64];

  switch (type) {

  case P2PKH:
  case BECH32:
  {
    unsigned char publicKeyBytes[128];

    if (!compressed) {

      // Full public key
      publicKeyBytes[0] = 0x4;
      pubKey.x.Get32Bytes(publicKeyBytes + 1);
      pubKey.y.Get32Bytes(publicKeyBytes + 33);
      sha256_65(publicKeyBytes, shapk);

    } else {

      // Compressed public key
      publicKeyBytes[0] = pubKey.y.IsEven() ? 0x2 : 0x3;
      pubKey.x.Get32Bytes(publicKeyBytes + 1);
      sha256_33(publicKeyBytes, shapk);

    }

    ripemd160_32(shapk, hash);
  }
  break;

  case P2SH:
  {

    // Redeem Script (1 to 1 P2SH)
    unsigned char script[64];

    script[0] = 0x00;  // OP_0
    script[1] = 0x14;  // PUSH 20 bytes
    GetHash160(P2PKH, compressed, pubKey, script + 2);

    sha256(script, 22, shapk);
    ripemd160_32(shapk, hash);

  }
  break;

  }

}


#define KEYBUFFPREFIX(buff,k,fix) \
(buff)[0] = (k->bits[7] >> 8) | ((uint32_t)(fix) << 24); \
(buff)[1] = (k->bits[6] >> 8) | (k->bits[7] <<24); \
(buff)[2] = (k->bits[5] >> 8) | (k->bits[6] <<24); \
(buff)[3] = (k->bits[4] >> 8) | (k->bits[5] <<24); \
(buff)[4] = (k->bits[3] >> 8) | (k->bits[4] <<24); \
(buff)[5] = (k->bits[2] >> 8) | (k->bits[3] <<24); \
(buff)[6] = (k->bits[1] >> 8) | (k->bits[2] <<24); \
(buff)[7] = (k->bits[0] >> 8) | (k->bits[1] <<24); \
(buff)[8] = 0x00800000 | (k->bits[0] <<24); \
(buff)[9] = 0; \
(buff)[10] = 0; \
(buff)[11] = 0; \
(buff)[12] = 0; \
(buff)[13] = 0; \
(buff)[14] = 0; \
(buff)[15] = 0x108;



void Secp256K1::GetHash160_fromX(int type,unsigned char prefix,
  Int *k0,Int *k1,Int *k2,Int *k3,
  uint8_t *h0,uint8_t *h1,uint8_t *h2,uint8_t *h3) {

#ifdef WIN64
  __declspec(align(16)) unsigned char sh0[64];
  __declspec(align(16)) unsigned char sh1[64];
  __declspec(align(16)) unsigned char sh2[64];
  __declspec(align(16)) unsigned char sh3[64];
#else
  unsigned char sh0[64] __attribute__((aligned(16)));
  unsigned char sh1[64] __attribute__((aligned(16)));
  unsigned char sh2[64] __attribute__((aligned(16)));
  unsigned char sh3[64] __attribute__((aligned(16)));
#endif

  switch (type) {

  case P2PKH:
  {
      uint32_t b0[16];
      uint32_t b1[16];
      uint32_t b2[16];
      uint32_t b3[16];

      KEYBUFFPREFIX(b0, k0, prefix);
      KEYBUFFPREFIX(b1, k1, prefix);
      KEYBUFFPREFIX(b2, k2, prefix);
      KEYBUFFPREFIX(b3, k3, prefix);

      sha256sse_1B(b0, b1, b2, b3, sh0, sh1, sh2, sh3);
      ripemd160sse_32(sh0, sh1, sh2, sh3, h0, h1, h2, h3);
  }
  break;

  case P2SH:
  {
	fprintf(stderr,"[E] Fixme unsopported case");
	exit(0);
  }
  break;

  }
}

void Secp256K1::GetHash160_fromX_02_03(int type,
  Int *k0, Int *k1, Int *k2, Int *k3,
  uint8_t *h02_0, uint8_t *h02_1, uint8_t *h02_2, uint8_t *h02_3,
  uint8_t *h03_0, uint8_t *h03_1, uint8_t *h03_2, uint8_t *h03_3) {

#ifdef WIN64
  __declspec(align(16)) unsigned char sh0[64];
  __declspec(align(16)) unsigned char sh1[64];
  __declspec(align(16)) unsigned char sh2[64];
  __declspec(align(16)) unsigned char sh3[64];
#else
  unsigned char sh0[64] __attribute__((aligned(16)));
  unsigned char sh1[64] __attribute__((aligned(16)));
  unsigned char sh2[64] __attribute__((aligned(16)));
  unsigned char sh3[64] __attribute__((aligned(16)));
#endif

  switch (type) {
  case P2PKH:
  {
    uint32_t b0[16];
    uint32_t b1[16];
    uint32_t b2[16];
    uint32_t b3[16];

    KEYBUFFPREFIX(b0, k0, 0x02);
    KEYBUFFPREFIX(b1, k1, 0x02);
    KEYBUFFPREFIX(b2, k2, 0x02);
    KEYBUFFPREFIX(b3, k3, 0x02);

    sha256sse_1B(b0, b1, b2, b3, sh0, sh1, sh2, sh3);
    ripemd160sse_32(sh0, sh1, sh2, sh3, h02_0, h02_1, h02_2, h02_3);

    // Flip compressed prefix from 0x02 to 0x03 (top byte of word 0).
    b0[0] ^= 0x01000000u;
    b1[0] ^= 0x01000000u;
    b2[0] ^= 0x01000000u;
    b3[0] ^= 0x01000000u;

    sha256sse_1B(b0, b1, b2, b3, sh0, sh1, sh2, sh3);
    ripemd160sse_32(sh0, sh1, sh2, sh3, h03_0, h03_1, h03_2, h03_3);
  }
  break;

  case P2SH:
  {
    fprintf(stderr,"[E] Fixme unsopported case");
    exit(0);
  }
  break;
  }
}

// ====================================================================
// AVX2 OPTIMIZED FUNCTIONS (8-way parallel processing)
// ====================================================================

void Secp256K1::GetHash160_AVX2(int type,bool compressed,
  Point &k0, Point &k1, Point &k2, Point &k3,
  Point &k4, Point &k5, Point &k6, Point &k7,
  uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3,
  uint8_t *h4, uint8_t *h5, uint8_t *h6, uint8_t *h7) {

#ifdef WIN64
  __declspec(align(32)) unsigned char sh0[64];
  __declspec(align(32)) unsigned char sh1[64];
  __declspec(align(32)) unsigned char sh2[64];
  __declspec(align(32)) unsigned char sh3[64];
  __declspec(align(32)) unsigned char sh4[64];
  __declspec(align(32)) unsigned char sh5[64];
  __declspec(align(32)) unsigned char sh6[64];
  __declspec(align(32)) unsigned char sh7[64];
#else
  unsigned char sh0[64] __attribute__((aligned(32)));
  unsigned char sh1[64] __attribute__((aligned(32)));
  unsigned char sh2[64] __attribute__((aligned(32)));
  unsigned char sh3[64] __attribute__((aligned(32)));
  unsigned char sh4[64] __attribute__((aligned(32)));
  unsigned char sh5[64] __attribute__((aligned(32)));
  unsigned char sh6[64] __attribute__((aligned(32)));
  unsigned char sh7[64] __attribute__((aligned(32)));
#endif

  switch (type) {

  case P2PKH:
  case BECH32:
  {

    if (!compressed) {
      // Process uncompressed keys (65 bytes each)
      uint32_t b0[32];
      uint32_t b1[32];
      uint32_t b2[32];
      uint32_t b3[32];
      uint32_t b4[32];
      uint32_t b5[32];
      uint32_t b6[32];
      uint32_t b7[32];

      KEYBUFFUNCOMP(b0, k0);
      KEYBUFFUNCOMP(b1, k1);
      KEYBUFFUNCOMP(b2, k2);
      KEYBUFFUNCOMP(b3, k3);
      KEYBUFFUNCOMP(b4, k4);
      KEYBUFFUNCOMP(b5, k5);
      KEYBUFFUNCOMP(b6, k6);
      KEYBUFFUNCOMP(b7, k7);

      // AVX2 8-way parallel SHA256 (2x faster than SSE2)
      sha256avx2_2B(b0, b1, b2, b3, b4, b5, b6, b7,
                    sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7);

      ripemd160avx2_32(sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7,
                       h0, h1, h2, h3, h4, h5, h6, h7);

    } else {
      // Process compressed keys (33 bytes each)
      uint32_t b0[16];
      uint32_t b1[16];
      uint32_t b2[16];
      uint32_t b3[16];
      uint32_t b4[16];
      uint32_t b5[16];
      uint32_t b6[16];
      uint32_t b7[16];

      KEYBUFFCOMP(b0, k0);
      KEYBUFFCOMP(b1, k1);
      KEYBUFFCOMP(b2, k2);
      KEYBUFFCOMP(b3, k3);
      KEYBUFFCOMP(b4, k4);
      KEYBUFFCOMP(b5, k5);
      KEYBUFFCOMP(b6, k6);
      KEYBUFFCOMP(b7, k7);

      // AVX2 8-way parallel SHA256 (2x faster than SSE2)
      sha256avx2_1B(b0, b1, b2, b3, b4, b5, b6, b7,
                    sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7);

      ripemd160avx2_32(sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7,
                       h0, h1, h2, h3, h4, h5, h6, h7);
    }

  }
  break;

  case P2SH:
  {
    // Recursively compute P2PKH first, then hash the script
    unsigned char kh0[20], kh1[20], kh2[20], kh3[20];
    unsigned char kh4[20], kh5[20], kh6[20], kh7[20];

    GetHash160_AVX2(P2PKH,compressed,k0,k1,k2,k3,k4,k5,k6,k7,
                    kh0,kh1,kh2,kh3,kh4,kh5,kh6,kh7);

    // Redeem Script (1 to 1 P2SH)
    uint32_t b0[16];
    uint32_t b1[16];
    uint32_t b2[16];
    uint32_t b3[16];
    uint32_t b4[16];
    uint32_t b5[16];
    uint32_t b6[16];
    uint32_t b7[16];

    KEYBUFFSCRIPT(b0, kh0);
    KEYBUFFSCRIPT(b1, kh1);
    KEYBUFFSCRIPT(b2, kh2);
    KEYBUFFSCRIPT(b3, kh3);
    KEYBUFFSCRIPT(b4, kh4);
    KEYBUFFSCRIPT(b5, kh5);
    KEYBUFFSCRIPT(b6, kh6);
    KEYBUFFSCRIPT(b7, kh7);

    // AVX2 8-way parallel SHA256 (2x faster than SSE2)
    sha256avx2_1B(b0, b1, b2, b3, b4, b5, b6, b7,
                  sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7);

    ripemd160avx2_32(sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7,
                     h0, h1, h2, h3, h4, h5, h6, h7);
  }
  break;

  }
}

void Secp256K1::GetHash160_fromX_AVX2(int type,unsigned char prefix,
  Int *k0,Int *k1,Int *k2,Int *k3,
  Int *k4,Int *k5,Int *k6,Int *k7,
  uint8_t *h0,uint8_t *h1,uint8_t *h2,uint8_t *h3,
  uint8_t *h4,uint8_t *h5,uint8_t *h6,uint8_t *h7) {

#ifdef WIN64
  __declspec(align(32)) unsigned char sh0[64];
  __declspec(align(32)) unsigned char sh1[64];
  __declspec(align(32)) unsigned char sh2[64];
  __declspec(align(32)) unsigned char sh3[64];
  __declspec(align(32)) unsigned char sh4[64];
  __declspec(align(32)) unsigned char sh5[64];
  __declspec(align(32)) unsigned char sh6[64];
  __declspec(align(32)) unsigned char sh7[64];
#else
  unsigned char sh0[64] __attribute__((aligned(32)));
  unsigned char sh1[64] __attribute__((aligned(32)));
  unsigned char sh2[64] __attribute__((aligned(32)));
  unsigned char sh3[64] __attribute__((aligned(32)));
  unsigned char sh4[64] __attribute__((aligned(32)));
  unsigned char sh5[64] __attribute__((aligned(32)));
  unsigned char sh6[64] __attribute__((aligned(32)));
  unsigned char sh7[64] __attribute__((aligned(32)));
#endif

  switch (type) {

  case P2PKH:
  {
      uint32_t b0[16];
      uint32_t b1[16];
      uint32_t b2[16];
      uint32_t b3[16];
      uint32_t b4[16];
      uint32_t b5[16];
      uint32_t b6[16];
      uint32_t b7[16];

      KEYBUFFPREFIX(b0, k0, prefix);
      KEYBUFFPREFIX(b1, k1, prefix);
      KEYBUFFPREFIX(b2, k2, prefix);
      KEYBUFFPREFIX(b3, k3, prefix);
      KEYBUFFPREFIX(b4, k4, prefix);
      KEYBUFFPREFIX(b5, k5, prefix);
      KEYBUFFPREFIX(b6, k6, prefix);
      KEYBUFFPREFIX(b7, k7, prefix);

      // AVX2 8-way parallel SHA256 (2x faster than SSE2)
      sha256avx2_1B(b0, b1, b2, b3, b4, b5, b6, b7,
                    sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7);

      ripemd160avx2_32(sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7,
                       h0, h1, h2, h3, h4, h5, h6, h7);
  }
  break;

  case P2SH:
  {
    fprintf(stderr,"[E] Fixme unsupported case\n");
    exit(0);
  }
  break;

  }
}

void Secp256K1::GetHash160_fromX_02_03_AVX2(int type,
  Int *k0, Int *k1, Int *k2, Int *k3,
  Int *k4, Int *k5, Int *k6, Int *k7,
  uint8_t *h02_0, uint8_t *h02_1, uint8_t *h02_2, uint8_t *h02_3,
  uint8_t *h02_4, uint8_t *h02_5, uint8_t *h02_6, uint8_t *h02_7,
  uint8_t *h03_0, uint8_t *h03_1, uint8_t *h03_2, uint8_t *h03_3,
  uint8_t *h03_4, uint8_t *h03_5, uint8_t *h03_6, uint8_t *h03_7) {

#ifdef WIN64
  __declspec(align(32)) unsigned char sh0[64];
  __declspec(align(32)) unsigned char sh1[64];
  __declspec(align(32)) unsigned char sh2[64];
  __declspec(align(32)) unsigned char sh3[64];
  __declspec(align(32)) unsigned char sh4[64];
  __declspec(align(32)) unsigned char sh5[64];
  __declspec(align(32)) unsigned char sh6[64];
  __declspec(align(32)) unsigned char sh7[64];
#else
  unsigned char sh0[64] __attribute__((aligned(32)));
  unsigned char sh1[64] __attribute__((aligned(32)));
  unsigned char sh2[64] __attribute__((aligned(32)));
  unsigned char sh3[64] __attribute__((aligned(32)));
  unsigned char sh4[64] __attribute__((aligned(32)));
  unsigned char sh5[64] __attribute__((aligned(32)));
  unsigned char sh6[64] __attribute__((aligned(32)));
  unsigned char sh7[64] __attribute__((aligned(32)));
#endif

  switch (type) {
  case P2PKH:
  {
    uint32_t b0[16];
    uint32_t b1[16];
    uint32_t b2[16];
    uint32_t b3[16];
    uint32_t b4[16];
    uint32_t b5[16];
    uint32_t b6[16];
    uint32_t b7[16];

    KEYBUFFPREFIX(b0, k0, 0x02);
    KEYBUFFPREFIX(b1, k1, 0x02);
    KEYBUFFPREFIX(b2, k2, 0x02);
    KEYBUFFPREFIX(b3, k3, 0x02);
    KEYBUFFPREFIX(b4, k4, 0x02);
    KEYBUFFPREFIX(b5, k5, 0x02);
    KEYBUFFPREFIX(b6, k6, 0x02);
    KEYBUFFPREFIX(b7, k7, 0x02);

    sha256avx2_1B(b0, b1, b2, b3, b4, b5, b6, b7,
                  sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7);
    ripemd160avx2_32(sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7,
                     h02_0, h02_1, h02_2, h02_3, h02_4, h02_5, h02_6, h02_7);

    b0[0] ^= 0x01000000u;
    b1[0] ^= 0x01000000u;
    b2[0] ^= 0x01000000u;
    b3[0] ^= 0x01000000u;
    b4[0] ^= 0x01000000u;
    b5[0] ^= 0x01000000u;
    b6[0] ^= 0x01000000u;
    b7[0] ^= 0x01000000u;

    sha256avx2_1B(b0, b1, b2, b3, b4, b5, b6, b7,
                  sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7);
    ripemd160avx2_32(sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7,
                     h03_0, h03_1, h03_2, h03_3, h03_4, h03_5, h03_6, h03_7);
  }
  break;

  case P2SH:
  {
    fprintf(stderr,"[E] Fixme unsupported case\n");
    exit(0);
  }
  break;
  }
}

// AVX-512 optimized 16-way parallel GetHash160 from X coordinate
void Secp256K1::GetHash160_fromX_AVX512(int type, unsigned char prefix,
  Int *k0, Int *k1, Int *k2, Int *k3,
  Int *k4, Int *k5, Int *k6, Int *k7,
  Int *k8, Int *k9, Int *k10, Int *k11,
  Int *k12, Int *k13, Int *k14, Int *k15,
  uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3,
  uint8_t *h4, uint8_t *h5, uint8_t *h6, uint8_t *h7,
  uint8_t *h8, uint8_t *h9, uint8_t *h10, uint8_t *h11,
  uint8_t *h12, uint8_t *h13, uint8_t *h14, uint8_t *h15) {

  // Aligned buffers for SHA256 output
  alignas(64) unsigned char sh0[64], sh1[64], sh2[64], sh3[64];
  alignas(64) unsigned char sh4[64], sh5[64], sh6[64], sh7[64];
  alignas(64) unsigned char sh8[64], sh9[64], sh10[64], sh11[64];
  alignas(64) unsigned char sh12[64], sh13[64], sh14[64], sh15[64];

  switch (type) {
  case P2PKH:
  {
    uint32_t b0[16], b1[16], b2[16], b3[16];
    uint32_t b4[16], b5[16], b6[16], b7[16];
    uint32_t b8[16], b9[16], b10[16], b11[16];
    uint32_t b12[16], b13[16], b14[16], b15[16];

    // Prepare key buffers with prefix
    KEYBUFFPREFIX(b0, k0, prefix);
    KEYBUFFPREFIX(b1, k1, prefix);
    KEYBUFFPREFIX(b2, k2, prefix);
    KEYBUFFPREFIX(b3, k3, prefix);
    KEYBUFFPREFIX(b4, k4, prefix);
    KEYBUFFPREFIX(b5, k5, prefix);
    KEYBUFFPREFIX(b6, k6, prefix);
    KEYBUFFPREFIX(b7, k7, prefix);
    KEYBUFFPREFIX(b8, k8, prefix);
    KEYBUFFPREFIX(b9, k9, prefix);
    KEYBUFFPREFIX(b10, k10, prefix);
    KEYBUFFPREFIX(b11, k11, prefix);
    KEYBUFFPREFIX(b12, k12, prefix);
    KEYBUFFPREFIX(b13, k13, prefix);
    KEYBUFFPREFIX(b14, k14, prefix);
    KEYBUFFPREFIX(b15, k15, prefix);

    // AVX2 SHA256 for first 8, then next 8
    // (SHA-NI 2-way interleaved would be faster but requires different setup)
    sha256avx2_1B(b0, b1, b2, b3, b4, b5, b6, b7,
                  sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7);
    sha256avx2_1B(b8, b9, b10, b11, b12, b13, b14, b15,
                  sh8, sh9, sh10, sh11, sh12, sh13, sh14, sh15);

    // AVX-512 16-way RIPEMD160
    ripemd160avx512_32(sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7,
                       sh8, sh9, sh10, sh11, sh12, sh13, sh14, sh15,
                       h0, h1, h2, h3, h4, h5, h6, h7,
                       h8, h9, h10, h11, h12, h13, h14, h15);
  }
  break;

  case P2SH:
  {
    fprintf(stderr, "[E] Fixme unsupported case\n");
    exit(0);
  }
  break;
  }
}

void Secp256K1::GetHash160_fromX_02_03_AVX512(int type,
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
  uint8_t *h03_12, uint8_t *h03_13, uint8_t *h03_14, uint8_t *h03_15) {

  alignas(64) unsigned char sh0[64], sh1[64], sh2[64], sh3[64];
  alignas(64) unsigned char sh4[64], sh5[64], sh6[64], sh7[64];
  alignas(64) unsigned char sh8[64], sh9[64], sh10[64], sh11[64];
  alignas(64) unsigned char sh12[64], sh13[64], sh14[64], sh15[64];

  switch (type) {
  case P2PKH:
  {
    uint32_t b0[16], b1[16], b2[16], b3[16];
    uint32_t b4[16], b5[16], b6[16], b7[16];
    uint32_t b8[16], b9[16], b10[16], b11[16];
    uint32_t b12[16], b13[16], b14[16], b15[16];

    KEYBUFFPREFIX(b0, k0, 0x02);
    KEYBUFFPREFIX(b1, k1, 0x02);
    KEYBUFFPREFIX(b2, k2, 0x02);
    KEYBUFFPREFIX(b3, k3, 0x02);
    KEYBUFFPREFIX(b4, k4, 0x02);
    KEYBUFFPREFIX(b5, k5, 0x02);
    KEYBUFFPREFIX(b6, k6, 0x02);
    KEYBUFFPREFIX(b7, k7, 0x02);
    KEYBUFFPREFIX(b8, k8, 0x02);
    KEYBUFFPREFIX(b9, k9, 0x02);
    KEYBUFFPREFIX(b10, k10, 0x02);
    KEYBUFFPREFIX(b11, k11, 0x02);
    KEYBUFFPREFIX(b12, k12, 0x02);
    KEYBUFFPREFIX(b13, k13, 0x02);
    KEYBUFFPREFIX(b14, k14, 0x02);
    KEYBUFFPREFIX(b15, k15, 0x02);

    sha256avx2_1B(b0, b1, b2, b3, b4, b5, b6, b7,
                  sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7);
    sha256avx2_1B(b8, b9, b10, b11, b12, b13, b14, b15,
                  sh8, sh9, sh10, sh11, sh12, sh13, sh14, sh15);
    ripemd160avx512_32(sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7,
                       sh8, sh9, sh10, sh11, sh12, sh13, sh14, sh15,
                       h02_0, h02_1, h02_2, h02_3, h02_4, h02_5, h02_6, h02_7,
                       h02_8, h02_9, h02_10, h02_11, h02_12, h02_13, h02_14, h02_15);

    b0[0] ^= 0x01000000u;  b1[0] ^= 0x01000000u;  b2[0] ^= 0x01000000u;  b3[0] ^= 0x01000000u;
    b4[0] ^= 0x01000000u;  b5[0] ^= 0x01000000u;  b6[0] ^= 0x01000000u;  b7[0] ^= 0x01000000u;
    b8[0] ^= 0x01000000u;  b9[0] ^= 0x01000000u;  b10[0] ^= 0x01000000u; b11[0] ^= 0x01000000u;
    b12[0] ^= 0x01000000u; b13[0] ^= 0x01000000u; b14[0] ^= 0x01000000u; b15[0] ^= 0x01000000u;

    sha256avx2_1B(b0, b1, b2, b3, b4, b5, b6, b7,
                  sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7);
    sha256avx2_1B(b8, b9, b10, b11, b12, b13, b14, b15,
                  sh8, sh9, sh10, sh11, sh12, sh13, sh14, sh15);
    ripemd160avx512_32(sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7,
                       sh8, sh9, sh10, sh11, sh12, sh13, sh14, sh15,
                       h03_0, h03_1, h03_2, h03_3, h03_4, h03_5, h03_6, h03_7,
                       h03_8, h03_9, h03_10, h03_11, h03_12, h03_13, h03_14, h03_15);
  }
  break;

  case P2SH:
  {
    fprintf(stderr, "[E] Fixme unsupported case\n");
    exit(0);
  }
  break;
  }
}

// AVX-512 optimized 16-way parallel GetHash160 for full points
void Secp256K1::GetHash160_AVX512(int type, bool compressed,
  Point &k0, Point &k1, Point &k2, Point &k3,
  Point &k4, Point &k5, Point &k6, Point &k7,
  Point &k8, Point &k9, Point &k10, Point &k11,
  Point &k12, Point &k13, Point &k14, Point &k15,
  uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3,
  uint8_t *h4, uint8_t *h5, uint8_t *h6, uint8_t *h7,
  uint8_t *h8, uint8_t *h9, uint8_t *h10, uint8_t *h11,
  uint8_t *h12, uint8_t *h13, uint8_t *h14, uint8_t *h15) {

  alignas(64) unsigned char sh0_buf[64], sh1_buf[64], sh2_buf[64], sh3_buf[64];
  alignas(64) unsigned char sh4_buf[64], sh5_buf[64], sh6_buf[64], sh7_buf[64];
  alignas(64) unsigned char sh8_buf[64], sh9_buf[64], sh10_buf[64], sh11_buf[64];
  alignas(64) unsigned char sh12_buf[64], sh13_buf[64], sh14_buf[64], sh15_buf[64];

  switch (type) {
  case P2PKH:
  {
    if (compressed) {
      // Use KEYBUFFCOMP macro for compressed points
      uint32_t b0[16], b1[16], b2[16], b3[16];
      uint32_t b4[16], b5[16], b6[16], b7[16];
      uint32_t b8[16], b9[16], b10[16], b11[16];
      uint32_t b12[16], b13[16], b14[16], b15[16];

      KEYBUFFCOMP(b0, k0);
      KEYBUFFCOMP(b1, k1);
      KEYBUFFCOMP(b2, k2);
      KEYBUFFCOMP(b3, k3);
      KEYBUFFCOMP(b4, k4);
      KEYBUFFCOMP(b5, k5);
      KEYBUFFCOMP(b6, k6);
      KEYBUFFCOMP(b7, k7);
      KEYBUFFCOMP(b8, k8);
      KEYBUFFCOMP(b9, k9);
      KEYBUFFCOMP(b10, k10);
      KEYBUFFCOMP(b11, k11);
      KEYBUFFCOMP(b12, k12);
      KEYBUFFCOMP(b13, k13);
      KEYBUFFCOMP(b14, k14);
      KEYBUFFCOMP(b15, k15);

      sha256avx2_1B(b0, b1, b2, b3, b4, b5, b6, b7,
                    sh0_buf, sh1_buf, sh2_buf, sh3_buf, sh4_buf, sh5_buf, sh6_buf, sh7_buf);
      sha256avx2_1B(b8, b9, b10, b11, b12, b13, b14, b15,
                    sh8_buf, sh9_buf, sh10_buf, sh11_buf, sh12_buf, sh13_buf, sh14_buf, sh15_buf);

      ripemd160avx512_32(sh0_buf, sh1_buf, sh2_buf, sh3_buf, sh4_buf, sh5_buf, sh6_buf, sh7_buf,
                         sh8_buf, sh9_buf, sh10_buf, sh11_buf, sh12_buf, sh13_buf, sh14_buf, sh15_buf,
                         h0, h1, h2, h3, h4, h5, h6, h7,
                         h8, h9, h10, h11, h12, h13, h14, h15);
    } else {
      // Uncompressed: 65-byte keys - use KEYBUFFUNCOMP macro
      uint32_t b0[32], b1[32], b2[32], b3[32];
      uint32_t b4[32], b5[32], b6[32], b7[32];
      uint32_t b8[32], b9[32], b10[32], b11[32];
      uint32_t b12[32], b13[32], b14[32], b15[32];

      KEYBUFFUNCOMP(b0, k0);
      KEYBUFFUNCOMP(b1, k1);
      KEYBUFFUNCOMP(b2, k2);
      KEYBUFFUNCOMP(b3, k3);
      KEYBUFFUNCOMP(b4, k4);
      KEYBUFFUNCOMP(b5, k5);
      KEYBUFFUNCOMP(b6, k6);
      KEYBUFFUNCOMP(b7, k7);
      KEYBUFFUNCOMP(b8, k8);
      KEYBUFFUNCOMP(b9, k9);
      KEYBUFFUNCOMP(b10, k10);
      KEYBUFFUNCOMP(b11, k11);
      KEYBUFFUNCOMP(b12, k12);
      KEYBUFFUNCOMP(b13, k13);
      KEYBUFFUNCOMP(b14, k14);
      KEYBUFFUNCOMP(b15, k15);

      sha256avx2_2B(b0, b1, b2, b3, b4, b5, b6, b7,
                    sh0_buf, sh1_buf, sh2_buf, sh3_buf, sh4_buf, sh5_buf, sh6_buf, sh7_buf);
      sha256avx2_2B(b8, b9, b10, b11, b12, b13, b14, b15,
                    sh8_buf, sh9_buf, sh10_buf, sh11_buf, sh12_buf, sh13_buf, sh14_buf, sh15_buf);

      ripemd160avx512_32(sh0_buf, sh1_buf, sh2_buf, sh3_buf, sh4_buf, sh5_buf, sh6_buf, sh7_buf,
                         sh8_buf, sh9_buf, sh10_buf, sh11_buf, sh12_buf, sh13_buf, sh14_buf, sh15_buf,
                         h0, h1, h2, h3, h4, h5, h6, h7,
                         h8, h9, h10, h11, h12, h13, h14, h15);
    }
  }
  break;

  case P2SH:
  case BECH32:
  {
    fprintf(stderr, "[E] Fixme unsupported case\n");
    exit(0);
  }
  break;
  }
}
