/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2016, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file     TEncSearch.cpp
 \brief    encoder search class
 */

#include "TLibCommon/CommonDef.h"
#include "TLibCommon/TComRom.h"
#include "TLibCommon/TComMotionInfo.h"
#include "TEncSearch.h"
#include "TLibCommon/TComTU.h"
#include "TLibCommon/Debug.h"
#include <math.h>
#include <limits>
#include <fstream>
#include <iostream>
#include <algorithm>


// EMI: Parameters declaration

signed short MVX_HALF, MVX_QRTER, MVY_HALF, MVY_QRTER = 0;
float  C, H1, H2, V1, V2, U1, U2, U3, U4;
long int array_e[100000];

float IN[17], X1[22], X2[20], OUT[49] = {};
int N, NN_out, index_ref, counter_i, PUHeight, PUWidth;

/*
The next group of variables are all 1D and 2D arrays. The reason why I declared them as std::array is because that
way they can be assigned in a cleaner manner. These arrays are assigned values in TEncSearch::init() function depending
on the chosen Quantization Parameter
https://stackoverflow.com/questions/16059781/2d-array-value-assign-after-declaration-in-c for more info on 2D array assignment
*/
std::array<std::array<float,4>,8> embs0, embs1;
std::array<std::array<float,17>,22> in_h1;
std::array<std::array<float,22>,20> h1_h2;
std::array<std::array<float,20>,49> h2_out;
std::array<float ,22> b1, BN_gamma_1, BN_beta_1;
std::array<float ,20> b2, BN_gamma_2, BN_beta_2;
std::array<float ,49> bout;
std::array<float ,9> BN_gamma_in, mean, stdev;

// Helper Functions

float relu(float x){
	if (x>0)	{	return x; }
	else { return 0; }
}

float sigmoid(float x){
  return (1 / (1 + std::exp(-x)));
}

void NN_pred(){
  
  // Reset all values of arrays

  memset(IN, 0, sizeof(IN));
  memset(X1, 0, sizeof(X1));
  memset(X2, 0, sizeof(X2));
  memset(OUT, 0, sizeof(OUT));
  memset(array_e, 0, sizeof(array_e));
  N = 0; NN_out = 0; counter_i = 0; index_ref = 0;


  // Normalize input values using the computed mean and standard deviations

  IN[8] = (U1 - mean[0]) / stdev[0];
  IN[9] = (V1 - mean[1]) / stdev[1];
  IN[10] = (U2 - mean[2]) / stdev[2];
  IN[11] = (H1 - mean[3]) / stdev[3];
  IN[12] = (C - mean[4]) / stdev[4];
  IN[13] = (H2 - mean[5]) / stdev[5];
  IN[14] = (U3 - mean[6]) / stdev[6];
  IN[15] = (V2 - mean[7]) / stdev[7];
  IN[16] = (U4 - mean[8]) / stdev[8];

  // Input layer also consists of categorical variables, in which we will use embedding matrices depending on block Height and Width

  switch (PUHeight) {
    case 4:   IN[0] = embs0[1][0];  IN[1] = embs0[1][1];   IN[2] = embs0[1][2];   IN[3] = embs0[1][3];		break;
    case 8:   IN[0] = embs0[2][0];  IN[1] = embs0[2][1];   IN[2] = embs0[2][2];   IN[3] = embs0[2][3];		break;
    case 12:  IN[0] = embs0[3][0];  IN[1] = embs0[3][1];   IN[2] = embs0[3][2];   IN[3] = embs0[3][3];	  break;
    case 16:  IN[0] = embs0[4][0];  IN[1] = embs0[4][1];   IN[2] = embs0[4][2];   IN[3] = embs0[4][3];		break;
    case 24:  IN[0] = embs0[5][0];  IN[1] = embs0[5][1];   IN[2] = embs0[5][2];   IN[3] = embs0[5][3];	  break;
    case 32:  IN[0] = embs0[6][0];  IN[1] = embs0[6][1];   IN[2] = embs0[6][2];   IN[3] = embs0[6][3];		break;
    case 64:  IN[0] = embs0[7][0];  IN[1] = embs0[7][1];   IN[2] = embs0[7][2];   IN[3] = embs0[7][3];		break;
    default:  IN[0] = embs0[0][0];  IN[1] = embs0[0][1];   IN[2] = embs0[0][2];   IN[3] = embs0[0][3];		break;
  }

  switch (PUWidth) {
    case 4:   IN[4] = embs1[1][0];  IN[5] = embs1[1][1];   IN[6] = embs1[1][2];   IN[7] = embs1[1][3];		break;
    case 8:   IN[4] = embs1[2][0];  IN[5] = embs1[2][1];   IN[6] = embs1[2][2];   IN[7] = embs1[2][3];		break;
    case 12:  IN[4] = embs1[3][0];  IN[5] = embs1[3][1];   IN[6] = embs1[3][2];   IN[7] = embs1[3][3];	  break;
    case 16:  IN[4] = embs1[4][0];  IN[5] = embs1[4][1];   IN[6] = embs1[4][2];   IN[7] = embs1[4][3];		break;
    case 24:  IN[4] = embs1[5][0];  IN[5] = embs1[5][1];   IN[6] = embs1[5][2];   IN[7] = embs1[5][3];	  break;
    case 32:  IN[4] = embs1[6][0];  IN[5] = embs1[6][1];   IN[6] = embs1[6][2];   IN[7] = embs1[6][3];		break;
    case 64:  IN[4] = embs1[7][0];  IN[5] = embs1[7][1];   IN[6] = embs1[7][2];   IN[7] = embs1[7][3];		break;
    default:  IN[4] = embs1[0][0];  IN[5] = embs1[0][1];   IN[6] = embs1[0][2];   IN[7] = embs1[0][3];		break;
  }

  // Input Layer
  for(int i=0;i<9;i++){
    IN[i+8] = (IN[i+8] * BN_gamma_in[i]);	  
  }

  
  // First Hidden Layer
  for (int i = 0; i < 22; i++) {
    for (int j = 0; j < 17; j++) {
      X1[i] += (in_h1[i][j] * IN[j]);
    }
    X1[i] += b1[i];
    X1[i] = (relu(X1[i]) * BN_gamma_1[i]) + BN_beta_1[i];
  }

  // Second Hidden Layer
  for (int i = 0; i < 20; i++) {
    for (int j = 0; j < 22; j++) {
      X2[i] += (h1_h2[i][j] * X1[j]);
    }
    X2[i] += b2[i];
    X2[i] = (relu(X2[i]) * BN_gamma_2[i]) + BN_beta_2[i];
  }

  // OUTPUT LAYER
  for (int i = 0; i < 49; i++) {
    for (int j = 0; j < 20; j++) {
      OUT[i] += (h2_out[i][j] * X2[j]);
    }
    OUT[i] += bout[i];
  }
  
  // Decision: NN_out holds the index of the maximum element

  N = sizeof(OUT) / sizeof(float); // Size of OUT[] array, used in next step
  NN_out = std::distance(OUT, std::max_element(OUT, OUT + N));
  
  switch (NN_out) {
    case 0: MVX_HALF = -1;  MVX_QRTER = -1;		MVY_HALF = -1;  MVY_QRTER = -1;		break;
    case 1: MVX_HALF = -1;  MVX_QRTER = 0;		MVY_HALF = -1;  MVY_QRTER = -1;		break;
    case 2: MVX_HALF = 0;   MVX_QRTER = -1;		MVY_HALF = -1;  MVY_QRTER = -1;		break;
    case 3: MVX_HALF = 0;   MVX_QRTER = 0;	  MVY_HALF = -1;  MVY_QRTER = -1;	  break;
    case 4: MVX_HALF = 0;   MVX_QRTER = 1;		MVY_HALF = -1;  MVY_QRTER = -1;		break;
    case 5: MVX_HALF = 1;   MVX_QRTER = 0;		MVY_HALF = -1;  MVY_QRTER = -1;	  break;
    case 6: MVX_HALF = 1;   MVX_QRTER = 1;		MVY_HALF = -1;  MVY_QRTER = -1;		break;

    case 7: MVX_HALF = -1;  MVX_QRTER = -1;		MVY_HALF = -1;  MVY_QRTER = 0;		break;
    case 8: MVX_HALF = -1;  MVX_QRTER = 0;		MVY_HALF = -1;  MVY_QRTER = 0;		break;
    case 9: MVX_HALF = 0;   MVX_QRTER = -1;		MVY_HALF = -1;  MVY_QRTER = 0;		break;
    case 10: MVX_HALF = 0;  MVX_QRTER = 0;	  MVY_HALF = -1;  MVY_QRTER = 0;		break;
    case 11: MVX_HALF = 0;  MVX_QRTER = 1;		MVY_HALF = -1;  MVY_QRTER = 0;		break;
    case 12: MVX_HALF = 1;  MVX_QRTER = 0;		MVY_HALF = -1;  MVY_QRTER = 0;		break;
    case 13: MVX_HALF = 1;  MVX_QRTER = 1;		MVY_HALF = -1;  MVY_QRTER = 0;		break;

    case 14: MVX_HALF = -1; MVX_QRTER = -1;	  MVY_HALF = 0;   MVY_QRTER = -1;		break;
    case 15: MVX_HALF = -1; MVX_QRTER = 0;		MVY_HALF = 0;   MVY_QRTER = -1;		break;
    case 16: MVX_HALF = 0;  MVX_QRTER = -1;   MVY_HALF = 0;   MVY_QRTER = -1;		break;
    case 17: MVX_HALF = 0;  MVX_QRTER = 0;	  MVY_HALF = 0;   MVY_QRTER = -1;		break;
    case 18: MVX_HALF = 0;  MVX_QRTER = 1;		MVY_HALF = 0;   MVY_QRTER = -1;		break;
    case 19: MVX_HALF = 1;  MVX_QRTER = 0;		MVY_HALF = 0;   MVY_QRTER = -1;		break;
    case 20: MVX_HALF = 1;  MVX_QRTER = 1;		MVY_HALF = 0;   MVY_QRTER = -1;		break;

    case 21: MVX_HALF = -1; MVX_QRTER = -1;	  MVY_HALF = 0;   MVY_QRTER = 0;		break;
    case 22: MVX_HALF = -1; MVX_QRTER = 0;		MVY_HALF = 0;   MVY_QRTER = 0;		break;
    case 23: MVX_HALF = 0;  MVX_QRTER = -1;	  MVY_HALF = 0;   MVY_QRTER = 0;		break;
    case 24: MVX_HALF = 0;  MVX_QRTER = 0;	  MVY_HALF = 0;   MVY_QRTER = 0;		break;
    case 25: MVX_HALF = 0;  MVX_QRTER = 1;		MVY_HALF = 0;   MVY_QRTER = 0;		break;
    case 26: MVX_HALF = 1;  MVX_QRTER = 0;		MVY_HALF = 0;   MVY_QRTER = 0;		break;
    case 27: MVX_HALF = 1;  MVX_QRTER = 1;		MVY_HALF = 0;   MVY_QRTER = 0;		break;

    case 28: MVX_HALF = -1; MVX_QRTER = -1;	  MVY_HALF = 0;   MVY_QRTER = 1;		break;
    case 29: MVX_HALF = -1; MVX_QRTER = 0;		MVY_HALF = 0;   MVY_QRTER = 1;		break;
    case 30: MVX_HALF = 0;  MVX_QRTER = -1;	  MVY_HALF = 0;   MVY_QRTER = 1;		break;
    case 31: MVX_HALF = 0;  MVX_QRTER = 0;	  MVY_HALF = 0;   MVY_QRTER = 1;		break;
    case 32: MVX_HALF = 0;  MVX_QRTER = 1;		MVY_HALF = 0;   MVY_QRTER = 1;		break;
    case 33: MVX_HALF = 1;  MVX_QRTER = 0;		MVY_HALF = 0;   MVY_QRTER = 1;		break;
    case 34: MVX_HALF = 1;  MVX_QRTER = 1;		MVY_HALF = 0;   MVY_QRTER = 1;		break;

    case 35: MVX_HALF = -1; MVX_QRTER = -1;   MVY_HALF = 1;   MVY_QRTER = 0;	  break;
    case 36: MVX_HALF = -1; MVX_QRTER = 0;		MVY_HALF = 1;   MVY_QRTER = 0;	  break;
    case 37: MVX_HALF = 0;  MVX_QRTER = -1;	  MVY_HALF = 1;   MVY_QRTER = 0;	  break;
    case 38: MVX_HALF = 0;  MVX_QRTER = 0;	  MVY_HALF = 1;   MVY_QRTER = 0;		break;
    case 39: MVX_HALF = 0;  MVX_QRTER = 1;		MVY_HALF = 1;   MVY_QRTER = 0;	  break;
    case 40: MVX_HALF = 1;  MVX_QRTER = 0;		MVY_HALF = 1;   MVY_QRTER = 0;	  break;
    case 41: MVX_HALF = 1;  MVX_QRTER = 1;		MVY_HALF = 1;   MVY_QRTER = 0;	  break;

    case 42: MVX_HALF = -1; MVX_QRTER = -1;	  MVY_HALF = 1;   MVY_QRTER = 1;		break;
    case 43: MVX_HALF = -1; MVX_QRTER = 0;		MVY_HALF = 1;   MVY_QRTER = 1;		break;
    case 44: MVX_HALF = 0;  MVX_QRTER = -1;	  MVY_HALF = 1;   MVY_QRTER = 1;		break;
    case 45: MVX_HALF = 0;  MVX_QRTER = 0;	  MVY_HALF = 1;   MVY_QRTER = 1;	  break;
    case 46: MVX_HALF = 0;  MVX_QRTER = 1;		MVY_HALF = 1;   MVY_QRTER = 1;		break;
    case 47: MVX_HALF = 1;  MVX_QRTER = 0;		MVY_HALF = 1;   MVY_QRTER = 1;		break;
    case 48: MVX_HALF = 1;  MVX_QRTER = 1;		MVY_HALF = 1;   MVY_QRTER = 1;		break;
    default: MVX_HALF = 0;  MVX_QRTER = 0;		MVY_HALF = 0;   MVY_QRTER = 0;		break;
  }
}

//end of modification


//! \ingroup TLibEncoder
//! \{

static const TComMv s_acMvRefineH[9] =
{
  TComMv(  0,  0 ), // 0
  TComMv(  0, -1 ), // 1
  TComMv(  0,  1 ), // 2
  TComMv( -1,  0 ), // 3
  TComMv(  1,  0 ), // 4
  TComMv( -1, -1 ), // 5
  TComMv(  1, -1 ), // 6
  TComMv( -1,  1 ), // 7
  TComMv(  1,  1 )  // 8
};

static const TComMv s_acMvRefineQ[9] =
{
  TComMv(  0,  0 ), // 0
  TComMv(  0, -1 ), // 1
  TComMv(  0,  1 ), // 2
  TComMv( -1, -1 ), // 5
  TComMv(  1, -1 ), // 6
  TComMv( -1,  0 ), // 3
  TComMv(  1,  0 ), // 4
  TComMv( -1,  1 ), // 7
  TComMv(  1,  1 )  // 8
};

static Void offsetSubTUCBFs(TComTU &rTu, const ComponentID compID)
{
        TComDataCU *pcCU              = rTu.getCU();
  const UInt        uiTrDepth         = rTu.GetTransformDepthRel();
  const UInt        uiAbsPartIdx      = rTu.GetAbsPartIdxTU(compID);
  const UInt        partIdxesPerSubTU = rTu.GetAbsPartIdxNumParts(compID) >> 1;

  //move the CBFs down a level and set the parent CBF

  UChar subTUCBF[2];
  UChar combinedSubTUCBF = 0;

  for (UInt subTU = 0; subTU < 2; subTU++)
  {
    const UInt subTUAbsPartIdx = uiAbsPartIdx + (subTU * partIdxesPerSubTU);

    subTUCBF[subTU]   = pcCU->getCbf(subTUAbsPartIdx, compID, uiTrDepth);
    combinedSubTUCBF |= subTUCBF[subTU];
  }

  for (UInt subTU = 0; subTU < 2; subTU++)
  {
    const UInt subTUAbsPartIdx = uiAbsPartIdx + (subTU * partIdxesPerSubTU);
    const UChar compositeCBF = (subTUCBF[subTU] << 1) | combinedSubTUCBF;

    pcCU->setCbfPartRange((compositeCBF << uiTrDepth), compID, subTUAbsPartIdx, partIdxesPerSubTU);
  }
}


TEncSearch::TEncSearch()
: m_puhQTTempTrIdx(NULL)
, m_pcQTTempTComYuv(NULL)
, m_pcEncCfg (NULL)
, m_pcTrQuant (NULL)
, m_pcRdCost (NULL)
, m_pcEntropyCoder (NULL)
, m_iSearchRange (0)
, m_bipredSearchRange (0)
, m_motionEstimationSearchMethod (MESEARCH_FULL)
, m_pppcRDSbacCoder (NULL)
, m_pcRDGoOnSbacCoder (NULL)
, m_pTempPel (NULL)
, m_isInitialized (false)
{
  for (UInt ch=0; ch<MAX_NUM_COMPONENT; ch++)
  {
    m_ppcQTTempCoeff[ch]                           = NULL;
#if ADAPTIVE_QP_SELECTION
    m_ppcQTTempArlCoeff[ch]                        = NULL;
#endif
    m_puhQTTempCbf[ch]                             = NULL;
    m_phQTTempCrossComponentPredictionAlpha[ch]    = NULL;
    m_pSharedPredTransformSkip[ch]                 = NULL;
    m_pcQTTempTUCoeff[ch]                          = NULL;
#if ADAPTIVE_QP_SELECTION
    m_ppcQTTempTUArlCoeff[ch]                      = NULL;
#endif
    m_puhQTTempTransformSkipFlag[ch]               = NULL;
  }

  for (Int i=0; i<MAX_NUM_REF_LIST_ADAPT_SR; i++)
  {
    memset (m_aaiAdaptSR[i], 0, MAX_IDX_ADAPT_SR * sizeof (Int));
  }
  for (Int i=0; i<AMVP_MAX_NUM_CANDS+1; i++)
  {
    memset (m_auiMVPIdxCost[i], 0, (AMVP_MAX_NUM_CANDS+1) * sizeof (UInt) );
  }

  setWpScalingDistParam( NULL, -1, REF_PIC_LIST_X );
}


Void TEncSearch::destroy()
{
  assert (m_isInitialized);
  if ( m_pTempPel )
  {
    delete [] m_pTempPel;
    m_pTempPel = NULL;
  }

  if ( m_pcEncCfg )
  {
    const UInt uiNumLayersAllocated = m_pcEncCfg->getQuadtreeTULog2MaxSize()-m_pcEncCfg->getQuadtreeTULog2MinSize()+1;

    for (UInt ch=0; ch<MAX_NUM_COMPONENT; ch++)
    {
      for (UInt layer = 0; layer < uiNumLayersAllocated; layer++)
      {
        delete[] m_ppcQTTempCoeff[ch][layer];
#if ADAPTIVE_QP_SELECTION
        delete[] m_ppcQTTempArlCoeff[ch][layer];
#endif
      }
      delete[] m_ppcQTTempCoeff[ch];
      delete[] m_puhQTTempCbf[ch];
#if ADAPTIVE_QP_SELECTION
      delete[] m_ppcQTTempArlCoeff[ch];
#endif
    }

    for( UInt layer = 0; layer < uiNumLayersAllocated; layer++ )
    {
      m_pcQTTempTComYuv[layer].destroy();
    }
  }

  delete[] m_puhQTTempTrIdx;
  delete[] m_pcQTTempTComYuv;

  for (UInt ch=0; ch<MAX_NUM_COMPONENT; ch++)
  {
    delete[] m_pSharedPredTransformSkip[ch];
    delete[] m_pcQTTempTUCoeff[ch];
#if ADAPTIVE_QP_SELECTION
    delete[] m_ppcQTTempTUArlCoeff[ch];
#endif
    delete[] m_phQTTempCrossComponentPredictionAlpha[ch];
    delete[] m_puhQTTempTransformSkipFlag[ch];
  }
  m_pcQTTempTransformSkipTComYuv.destroy();

  m_tmpYuvPred.destroy();
  m_isInitialized = false;
}

TEncSearch::~TEncSearch()
{
  if (m_isInitialized)
  {
    destroy();
  }
}




Void TEncSearch::init(TEncCfg*       pcEncCfg,
                      TComTrQuant*   pcTrQuant,
                      Int            iSearchRange,
                      Int            bipredSearchRange,
                      MESearchMethod motionEstimationSearchMethod,
                      const UInt     maxCUWidth,
                      const UInt     maxCUHeight,
                      const UInt     maxTotalCUDepth,
                      TEncEntropy*   pcEntropyCoder,
                      TComRdCost*    pcRdCost,
                      TEncSbac***    pppcRDSbacCoder,
                      TEncSbac*      pcRDGoOnSbacCoder
                      )
{
  assert (!m_isInitialized);
  m_pcEncCfg                     = pcEncCfg;
  m_pcTrQuant                    = pcTrQuant;
  m_iSearchRange                 = iSearchRange;
  m_bipredSearchRange            = bipredSearchRange;
  m_motionEstimationSearchMethod = motionEstimationSearchMethod;
  m_pcEntropyCoder               = pcEntropyCoder;
  m_pcRdCost                     = pcRdCost;

  m_pppcRDSbacCoder              = pppcRDSbacCoder;
  m_pcRDGoOnSbacCoder            = pcRDGoOnSbacCoder;
  
  for (UInt iDir = 0; iDir < MAX_NUM_REF_LIST_ADAPT_SR; iDir++)
  {
    for (UInt iRefIdx = 0; iRefIdx < MAX_IDX_ADAPT_SR; iRefIdx++)
    {
      m_aaiAdaptSR[iDir][iRefIdx] = iSearchRange;
    }
  }

  // initialize motion cost
  for( Int iNum = 0; iNum < AMVP_MAX_NUM_CANDS+1; iNum++)
  {
    for( Int iIdx = 0; iIdx < AMVP_MAX_NUM_CANDS; iIdx++)
    {
      if (iIdx < iNum)
      {
        m_auiMVPIdxCost[iIdx][iNum] = xGetMvpIdxBits(iIdx, iNum);
      }
      else
      {
        m_auiMVPIdxCost[iIdx][iNum] = MAX_INT;
      }
    }
  }

  const ChromaFormat cform=pcEncCfg->getChromaFormatIdc();
  initTempBuff(cform);

  m_pTempPel = new Pel[maxCUWidth*maxCUHeight];

  const UInt uiNumLayersToAllocate = pcEncCfg->getQuadtreeTULog2MaxSize()-pcEncCfg->getQuadtreeTULog2MinSize()+1;
  const UInt uiNumPartitions = 1<<(maxTotalCUDepth<<1);
  for (UInt ch=0; ch<MAX_NUM_COMPONENT; ch++)
  {
    const UInt csx=::getComponentScaleX(ComponentID(ch), cform);
    const UInt csy=::getComponentScaleY(ComponentID(ch), cform);
    m_ppcQTTempCoeff[ch] = new TCoeff* [uiNumLayersToAllocate];
#if ADAPTIVE_QP_SELECTION
    m_ppcQTTempArlCoeff[ch]  = new TCoeff*[uiNumLayersToAllocate];
#endif
    m_puhQTTempCbf[ch] = new UChar  [uiNumPartitions];

    for (UInt layer = 0; layer < uiNumLayersToAllocate; layer++)
    {
      m_ppcQTTempCoeff[ch][layer] = new TCoeff[(maxCUWidth*maxCUHeight)>>(csx+csy)];
#if ADAPTIVE_QP_SELECTION
      m_ppcQTTempArlCoeff[ch][layer]  = new TCoeff[(maxCUWidth*maxCUHeight)>>(csx+csy) ];
#endif
    }

    m_phQTTempCrossComponentPredictionAlpha[ch]    = new SChar  [uiNumPartitions];
    m_pSharedPredTransformSkip[ch]                 = new Pel   [MAX_CU_SIZE*MAX_CU_SIZE];
    m_pcQTTempTUCoeff[ch]                          = new TCoeff[MAX_CU_SIZE*MAX_CU_SIZE];
#if ADAPTIVE_QP_SELECTION
    m_ppcQTTempTUArlCoeff[ch]                      = new TCoeff[MAX_CU_SIZE*MAX_CU_SIZE];
#endif
    m_puhQTTempTransformSkipFlag[ch]               = new UChar [uiNumPartitions];
  }
  m_puhQTTempTrIdx   = new UChar  [uiNumPartitions];
  m_pcQTTempTComYuv  = new TComYuv[uiNumLayersToAllocate];
  for( UInt ui = 0; ui < uiNumLayersToAllocate; ++ui )
  {
    m_pcQTTempTComYuv[ui].create( maxCUWidth, maxCUHeight, pcEncCfg->getChromaFormatIdc() );
  }
  m_pcQTTempTransformSkipTComYuv.create( maxCUWidth, maxCUHeight, pcEncCfg->getChromaFormatIdc() );
  m_tmpYuvPred.create(MAX_CU_SIZE, MAX_CU_SIZE, pcEncCfg->getChromaFormatIdc());
  m_isInitialized = true;

  // EMI: Weights and Bias Initialization based on QP

  if(m_pcEncCfg->getQP() == 27){
    
    embs0 = {{
			{{0.26888916,0.33337578,0.26942757,0.3971767}},
			{{0.0048518013,0.043297287,-0.008824279,0.0021500925}},
			{{-0.0075342553,-0.014839634,0.006728149,-0.0018673245}},
			{{0.020745702,-0.07163908,-0.03310908,0.029965091}},
			{{0.021908993,-0.08273415,0.008476719,0.00074990944}},
			{{-0.008009936,-0.2331889,-0.2164679,0.052489445}},
			{{-0.012068137,-0.22542445,-0.15615678,0.016278626}},
			{{0.22077987,-0.60080224,-1.099746,-0.95901775}}
    }};

    embs1 = {{
			{{-0.18688016,0.0007355213,-0.35365492,0.12459138}},
			{{0.020424744,-0.007628322,0.03671953,-0.00021638475}},
			{{-0.0027425757,0.0068448805,-0.021018915,0.00016698838}},
			{{-0.109797366,0.0015004467,0.06415523,-0.014492594}},
			{{-0.019343065,-0.01409941,-0.09645954,0.0035052167}},
			{{-0.11857826,0.0033721866,0.07289964,0.1086809}},
			{{-0.035313357,-0.22693714,-0.24610956,0.051204808}},
			{{-0.4047911,-0.5407213,0.026340954,-1.3581167}}
    }};

    in_h1 = {{
			{{-0.06205056,-0.35290056,-0.17085886,-0.03093076,0.14076932,-0.0027149946,-0.37947422,0.14230068,0.33937028,0.84634465,0.5179456,-0.015585538,-9.091468,-0.28714353,0.35315046,1.7618704,9.276261}},
			{{0.106475,-0.038478512,-0.038874738,0.14463015,0.24839367,-0.011581389,-0.10276053,0.004481586,-0.77762854,2.408172,3.4319596,2.2488587,-0.53393686,-0.81117415,-2.3617988,-7.1781826,9.203312}},
			{{0.010929881,-0.032975428,-0.04102234,-0.009746591,-0.01582309,-0.057727892,-0.013366925,-0.07726804,2.901721,-0.9644129,-6.4703283,1.0182652,1.3778181,3.298214,-2.5790217,-1.8984816,9.976436}},
			{{0.036058843,0.039748468,0.050374635,0.074876726,0.1738276,0.043435615,0.038132843,0.032399498,1.6107361,-3.7441838,1.5366392,0.30319127,0.1167481,-0.9786074,-0.27042404,7.336364,14.390563}},
			{{0.14166173,-0.1381795,-0.019242147,-0.04911218,0.14151356,-0.109705314,-0.14721693,-0.09792631,1.2913625,-0.32723206,1.0107722,-4.8773675,-0.6276112,4.9407687,-0.117250964,0.22388439,-3.1512997}},
			{{0.028258547,-0.06493587,0.051529072,-0.051958356,-0.015319186,0.050087526,-0.053480648,0.009935036,0.57836014,0.27297753,2.189681,-0.5382368,1.4118688,-0.75655,-6.6051335,2.851554,14.506139}},
			{{0.024909379,-0.07826331,-0.05164261,0.031078111,0.013927204,0.0023033412,-0.1207748,-0.0076333554,-0.35257983,1.3250577,-9.426376,0.18225591,0.7959614,0.4455838,0.9224792,1.2213817,1.4526724}},
			{{-0.034434684,-0.036904573,-0.036984455,0.0044503305,0.0022352843,0.02447542,-0.05466938,-0.052179776,-8.6051,1.4200547,-0.06964812,-1.1123106,0.44600558,0.7085214,0.23790993,1.9818953,-12.046127}},
			{{0.05612522,-0.053258315,-0.049601946,-0.082962066,0.20616135,-0.035906706,-0.094146594,-0.21764475,0.37478,0.4535718,-0.63387877,6.473826,-0.4954997,-3.847885,0.5448075,-1.1453211,-4.0227966}},
			{{-0.0034042317,-0.007310502,-0.0024796273,0.017100245,0.011897236,0.007181073,-0.0025964999,-0.007471835,-3.7994056,0.16058457,0.09644091,0.3959259,0.091042444,-0.35414875,-0.07290709,-0.34743732,32.707943}},
			{{-0.037098546,-0.02184826,0.09622031,-0.019358614,0.2773329,0.04349763,-0.051807966,-0.022291442,-0.7937578,0.34028953,3.3398802,1.2353382,0.43074632,-6.4747047,-0.21276964,4.3569326,9.554955}},
			{{0.086764835,-0.09399598,0.030442169,0.015489392,0.1474069,0.10181992,-0.25395727,-0.055025198,0.084465295,1.6414195,3.3661375,-0.9266418,-3.9580152,-1.0733072,0.7427766,6.7843914,16.321978}},
			{{0.038944904,-0.1165917,-0.040001918,0.029891953,-0.16265483,-0.045573883,-0.07998126,0.024584796,1.1195836,0.21934424,-0.2359118,-6.9565787,1.5117892,1.8226818,-0.4790659,1.0555794,-3.4993136}},
			{{0.18147786,-0.79711163,-0.027062135,0.071451694,-0.36080667,-0.15596116,-0.55688643,-0.23473834,-0.99446654,1.9915721,-0.87775594,1.5315607,-7.375366,1.6138308,0.34931263,3.474257,10.917939}},
			{{-0.11416159,0.05675029,-0.084050104,0.031858806,0.26031992,0.032140642,-0.028479006,-0.04986942,6.095314,0.68255955,-4.590063,0.14352731,-1.5620162,2.5416958,-3.3501284,3.634533,4.956368}},
			{{-0.007302549,0.0122601995,-0.011264465,0.011372025,0.10683094,0.013605445,-0.011882047,-0.00807854,0.87629265,-0.10972005,0.26869184,-0.79082423,-0.30955082,1.0720605,0.61201364,1.2442973,-31.37301}},
			{{0.30989602,3.8671997,-2.3637662,0.07695089,1.4588475,-1.7068859,3.2868009,0.017731521,2.2054026,0.9699397,1.2383561,1.9953256,-5.2647686,0.9153282,-1.3367555,8.4430275,7.2875338}},
			{{-0.0127452025,-0.06539125,0.00012233038,0.005950654,-0.08679785,-0.0035518114,-0.037985794,0.02170182,-0.29264957,0.85816336,0.2798071,0.47960874,2.1470857,0.28790626,-0.40317458,-10.321914,-5.884294}},
			{{-0.007813763,-0.13459231,-0.0066805524,0.027717425,-0.10702333,-0.013101047,-0.10787466,-0.0068500806,0.21397544,-6.458878,-0.27586344,0.20158952,3.2919679,0.35352588,0.050470274,3.236864,-3.6190698}},
			{{0.004155157,-0.025466671,0.0070963968,0.0057676947,-0.019397598,-0.0014343722,-0.009436511,0.0062604994,-8.454919,-1.3703815,2.0976155,2.4382021,0.90462697,-0.17364484,0.73064977,0.48918065,-13.785964}},
			{{0.04697719,-0.10657512,-0.052549705,0.03133773,-0.060816657,-0.041547704,-0.0850572,0.021462012,-1.1553674,0.68908817,-0.2240088,1.0087705,2.2158256,-6.145701,-0.4696225,1.960309,-13.21202}},
			{{0.006502851,0.03906795,0.082761705,-0.015540242,0.14226197,0.047210503,0.052887995,-0.0056152917,2.0708876,1.1520886,-1.9235245,-3.2856722,1.011641,0.4664508,2.9268582,-5.6797056,20.014566}}
    }};

    h1_h2 = {{
			{{0.35094085,-2.2402449,-0.1899432,2.12071,-2.3197274,-1.5581638,0.54768217,-4.3301105,0.10796151,-0.8270882,-0.32651496,0.03813263,-2.3699894,2.6702452,1.0795506,-3.2972543,-1.7485937,-3.2245114,0.08564647,-0.5345541,0.55380297,-0.92347175}},
			{{-0.13892595,-1.1600838,-0.71871233,-0.56147826,1.4583054,-0.63852006,-4.5078773,0.28163454,-17.80308,-0.85855585,-0.5010922,0.8611811,0.7203713,1.469543,-0.26100937,-1.3725061,-1.5335901,0.20998645,1.0077381,-0.41116214,-2.1185532,-0.27265194}},
			{{0.3079511,1.0750291,0.5049704,0.8935327,-0.51759446,-1.1526767,-1.6026238,-1.4446186,0.3119105,-0.7113408,-3.206323,2.12646,-2.1710691,2.4642508,-0.8686417,-0.5530051,-1.5335767,0.46945602,0.5356811,0.1500205,-2.8925562,-0.79467994}},
			{{-0.1371943,0.01638802,-0.04297027,-0.3389799,1.2207017,-0.3633783,-0.55255103,-0.93430597,-1.9512007,-0.906236,-1.4181786,1.0477778,-0.24240734,1.5967987,2.1774058,-1.0066862,-1.7927074,0.21652652,0.35408664,-0.40967813,-1.5735074,-0.6679476}},
			{{2.4858496,-0.76165456,-0.3517925,-0.94289786,0.37053475,-1.4792296,0.13063738,0.14434257,0.48796114,-0.2882178,0.09113615,2.5568302,0.96494746,1.6070769,0.29534578,-0.5776445,-1.617194,-2.1622865,-2.1062133,-0.80959475,1.5881277,-1.1188457}},
			{{0.2699315,0.06951124,-12.836189,1.0374107,-1.2934433,-2.5519533,-4.727695,-0.46610394,0.10499331,-1.3989019,1.580358,-1.7766451,-3.696533,2.2707334,-3.898118,0.025809456,-1.0685409,-1.0034182,0.47761568,0.3056113,1.5203408,-8.1998625}},
			{{0.33145577,1.4343811,-1.2619538,-4.2770047,-0.21141861,-0.55467916,-3.0694506,0.22544861,-1.5746648,-2.2796106,-0.26662084,-0.2744821,1.7530524,2.611429,-0.321195,-0.9298099,-1.6583027,-0.6345922,-4.1202707,-1.236761,-0.05696857,1.4528233}},
			{{0.6649986,-3.2696273,-0.97046334,2.992011,1.0387673,-2.819785,-2.7294092,0.37235475,-1.6290104,-0.18846889,-0.07836999,-0.19418149,1.8009968,2.8629882,-1.501163,-1.0391941,-2.858956,-5.1101813,-0.18903583,-0.052934773,1.0198922,-2.2331572}},
			{{-0.2090871,-0.4863,-0.3434963,-0.15692429,-14.244794,-3.507891,-0.327892,-4.7131414,1.0962831,-1.3126769,0.5506627,0.26604748,-1.0746812,2.1443985,-1.0832554,-3.2378063,-1.5466508,1.0739665,0.78134483,-0.5170236,0.33557218,-0.6794828}},
			{{3.5604343,0.3686872,2.8006117,-0.07721485,1.243771,-0.1532469,0.29830387,-0.07404779,0.5372764,-0.57433385,-3.5204198,-2.4241369,-0.86414254,0.6292585,-3.0412486,-3.2954564,-2.2026162,0.70872927,0.96881056,2.8033655,-4.0779014,-1.4233943}},
			{{1.1375115,-0.98163444,-2.7333426,-2.420467,-0.049400855,-0.8016565,-1.0057743,0.46772525,0.06104554,1.4204303,-0.8316258,0.48675457,-2.2376308,2.7042665,-3.5708475,1.9274199,-2.084789,-3.028697,-1.78003,0.23304671,-1.444798,-3.7727928}},
			{{0.2787427,-3.8977058,-1.6968523,2.1526449,0.26585564,-0.5777819,1.0322582,-5.1364098,-0.8909205,-1.7032422,-0.6330761,-3.6418428,2.8178172,3.1043227,2.5000916,-1.1715995,-2.3278046,-2.4642208,-0.89261997,-3.7265446,-1.7824707,-1.2786335}},
			{{0.1468516,2.2869775,-1.2811421,-0.66315,-2.175888,-1.4759085,0.8499608,-0.89643914,-1.2093097,0.49633095,1.9841406,-3.5020623,-2.4993303,3.585398,1.3752866,-8.656708,-1.892992,-0.10636012,-2.101777,-6.399404,0.18612,-0.5168197}},
			{{0.3187965,0.8776294,-0.03385597,-2.157772,0.27649468,-0.31644908,0.63999206,-2.2176418,0.67728674,0.060577005,-0.6455512,1.2257292,-0.031750217,1.6234127,0.9185435,-4.135025,-1.7360418,0.18702967,-2.8012435,-3.063499,-1.7824427,-0.56632155}},
			{{8.336277,0.23362431,-0.3214503,0.22865617,1.1477137,-0.6676485,-0.20343372,0.10704715,0.5659633,-0.0055566705,-1.2266669,-3.577075,0.8880544,-1.3492509,0.15086433,-0.03396892,-2.279213,0.83596766,0.7386306,-0.25862324,0.39894703,-1.2986002}},
			{{0.094671555,1.5624152,-2.5905702,-1.3493106,-1.9703066,-1.5833949,-1.0344691,-0.78521127,0.48159406,-14.881109,0.93155867,-1.241062,-0.77729714,2.2357914,-2.0795553,2.5020876,-1.8124553,0.27522323,0.26213428,-0.3391898,0.8248072,-0.9162607}},
			{{0.6346298,-1.0644522,-12.560466,-4.602836,0.99542594,-5.6723986,-0.688924,0.36092505,0.40686184,0.9942901,-0.7643233,-3.294334,0.4974624,2.586748,-2.7569902,3.6963434,-1.4336312,1.0329549,-1.8438996,-0.5574699,-1.0923316,-0.59785414}},
			{{2.4036782,1.6507438,-0.71953446,1.9461348,-0.38766816,-2.2943404,-2.2732177,-2.0516312,-0.8682256,-1.5863603,-2.6366065,-1.9373174,-2.469638,5.1838274,-0.68299,-0.7010495,-1.8912034,0.40655643,0.40518835,-0.2576297,-2.84953,-0.48186648}},
			{{0.79366726,2.8408353,-1.3921822,-0.92032325,0.45316005,0.5040676,-1.2483644,-2.6600072,-3.4523458,-0.7358735,-0.49137264,-3.8292105,-0.6162937,2.0696054,3.5619512,-2.952356,-2.7494411,-1.4761109,0.10497004,-11.780169,-0.016464291,-4.367115}},
			{{1.9874853,-2.898325,-1.1393987,-2.0905585,1.562599,0.7442111,-1.2687485,-1.0680461,0.38125464,-1.678275,1.2116766,1.7678914,0.46113765,2.7411454,1.0333128,-2.5381014,-2.5871656,-4.540075,-3.7753105,-0.6407381,0.16413058,-1.3504753}}
    }};

    h2_out = {{
			{{-3.066898,0.33597252,-4.3193107,-5.9171557,-1.4519075,0.1585446,-1.307392,0.26427296,-2.0777688,0.72498894,0.9315726,-1.7895601,-0.18311319,-5.3318124,0.47089276,-2.657698,0.94375306,-5.579117,-6.7812314,-3.3494408}},
			{{-3.8382218,0.8723686,-0.8508142,-1.8907461,-4.017043,0.05642419,-3.286129,1.3037126,0.18308787,1.2933713,1.0631888,-3.1651897,-3.1137052,-6.6733828,0.9947935,-1.9616302,0.65434134,-2.965401,-4.335585,-4.7650056}},
			{{-1.3334192,0.17848012,0.20237091,-0.6668556,-6.296603,0.89948446,-4.5818176,1.1195525,-0.2715365,1.131891,-0.3903994,-2.1460447,-3.657837,-9.043678,0.20865734,-1.8548945,-0.7890347,0.80833966,-1.8982964,-6.455065}},
			{{0.25737804,-0.8342683,0.28947586,-0.20062467,-5.7464275,0.31966275,-6.7188654,0.41323593,-0.9750862,0.510417,-2.6556249,-0.25093982,-2.7764225,-4.642855,-1.0192517,-0.5433152,-1.4663838,1.1000988,-0.30659217,-5.212606}},
			{{1.3635522,-1.3905246,0.4155117,0.3156958,-5.1820226,-1.0285122,-6.480496,-0.2273342,0.23929483,0.9564057,-5.936832,1.3524238,-1.6392542,-2.4697106,0.31916192,-0.92729574,-3.9550247,0.5047273,-0.38671678,-4.6871114}},
			{{1.8220454,-1.5600618,-0.83586097,-0.24619363,-3.1651537,-1.5776894,-4.3183527,-2.232596,0.93256444,0.87008345,-6.4551415,1.6517231,-0.4721319,0.019785374,0.9320186,-0.88896513,-5.7873363,-3.248359,-0.20928088,-4.693013}},
			{{1.2193208,-3.5418696,-6.419055,-0.62468106,-2.2244475,-2.4114418,-3.6295211,-1.3947701,-0.20606567,0.6099411,-4.315041,1.2826613,0.37429148,0.03990931,0.62928414,-1.163504,-5.003934,-6.729938,0.21595964,-2.4657507}},
			{{-2.2376385,1.3145744,-5.1392307,-9.28425,-0.29371154,-0.89478725,-0.2890302,1.5292064,-4.2114506,-0.9162807,1.1116388,-2.229063,-1.3736753,-4.9795732,1.0150046,-1.8020424,0.7387924,-10.033238,-3.8260446,-1.2612056}},
			{{-2.5452945,1.3313079,-0.9764796,-1.5793886,-0.9829576,-0.61390364,-2.2224698,1.7981511,-1.4229046,-0.42024714,1.0602186,-1.1185387,-3.4425783,-5.8023376,1.0033271,-2.7365422,0.80811685,-4.6861796,-2.9851909,-3.1755347}},
			{{-0.4755881,0.47714654,0.7258598,-0.12189527,-1.9181178,0.52068126,-3.137563,1.6696932,-1.0549392,-0.60526407,0.615335,0.21175581,-4.3982015,-6.1673527,0.7833304,-2.514501,-0.9871368,0.28132817,-1.6935302,-3.3175735}},
			{{0.7111771,-0.93454254,0.77698946,0.6218478,-1.9828802,0.98585284,-4.007147,1.297756,-0.8039556,0.2812075,-1.5475285,1.0714691,-3.8891544,-4.896421,-0.36554307,-0.94877124,-3.101908,0.7842019,-0.5285244,-2.9719925}},
			{{1.532712,-1.7115608,0.33408797,0.12741388,-1.7534162,0.6465887,-3.802126,0.72971314,0.52892923,-0.72826326,-4.106918,1.533637,-1.7484101,-1.2558686,0.7459857,-1.1483244,-4.3613462,0.2574915,-0.77873456,-2.134241}},
			{{1.77803,-2.359384,-1.7161206,-0.9837191,-0.99470335,0.1875843,-2.9066608,-0.060650334,1.343257,-0.272496,-6.839042,1.4163661,0.05726755,0.39479586,0.5605167,-1.4331193,-5.526842,-5.5432286,-1.2094295,-1.9107502}},
			{{1.6037307,-2.90865,-9.361994,-1.1249639,-0.53895444,-2.166398,-1.0920125,-0.046345837,0.125311,-1.3265362,-7.525629,1.1254019,1.2216245,-0.0883723,0.601887,-1.8534551,-4.2412987,-8.795844,-1.3030221,0.14426555}},
			{{-1.4020936,1.8783013,-6.7523994,-6.1049705,0.32907194,-2.449745,0.62714183,0.955518,-4.4325776,-1.8453784,0.30462673,-1.1657361,-2.65002,-5.1691008,0.45084414,-1.6615989,0.5161384,-10.211124,-2.2577465,0.36013895}},
			{{-2.170781,1.9126688,-0.7984417,-0.6703616,0.68824244,-2.3110282,-0.5281962,0.9738895,-1.6637805,-1.9485301,0.19450593,0.57711375,-3.6335447,-5.14275,0.8531755,-2.6455173,0.9918387,-4.986569,-2.1581345,-0.35603845}},
			{{-0.7985861,0.23418935,0.59653986,0.84627545,0.46060294,-1.022204,-1.592849,0.8024595,-1.1363257,-0.8493021,0.633168,0.93304783,-4.676906,-4.5239882,0.84121126,-1.5705754,0.3085501,0.3597514,-0.6534184,0.09124972}},
			{{0.45091185,-1.061355,0.89863145,0.62177604,0.22753738,0.31893027,-2.0340052,0.5562612,-0.8663485,-0.09152598,0.12321731,0.52001154,-2.6921961,-2.2764153,-0.55414414,-0.7042064,-2.1222668,0.7032,-0.47533277,0.25435078}},
			{{0.8856428,-1.5220866,0.5108761,-0.41985595,0.38129228,1.2943358,-1.9225384,0.12525812,0.33291212,-0.4782512,-1.72376,0.07782341,-0.51151925,-0.22873364,0.48234576,-0.53630596,-4.588667,0.102802515,-1.3113252,0.33338684}},
			{{1.0830115,-1.5328617,-2.1400847,-2.1691468,0.2847174,1.0263671,-2.0228631,-0.13029212,1.7165205,-1.9579799,-3.3428383,-0.5013263,0.8596006,0.44840267,0.762657,-0.48737705,-5.0746436,-4.9462543,-1.5131612,0.48755726}},
			{{0.9162966,-2.9620821,-10.2780285,-1.1686447,-0.34274387,-0.7210792,-0.9803669,-0.081343554,0.91863644,-3.4837945,-5.131263,-0.8731665,1.6097764,-0.7511817,0.07968486,-1.3703616,-3.8215487,-6.1191225,-1.6473702,1.5227057}},
			{{-0.4937812,1.6350044,-5.752102,-2.1065938,-0.12759317,-3.5973747,0.7381057,-0.09112318,-3.6400151,-3.354544,-1.987856,0.07687807,-2.7193139,-3.4447966,-1.3548948,-1.2812862,-0.4427916,-6.1202593,-1.0882374,0.916395}},
			{{-1.7774154,1.7100859,-1.4481148,0.41708422,0.63411653,-4.7994804,0.58793485,-0.107667446,-1.6882156,-2.5073094,-1.4795358,0.9909748,-3.568137,-3.2109563,-0.5885503,-1.5473729,0.81577194,-4.4396195,-1.0074238,0.6196923}},
			{{-0.7025387,0.5488594,0.32630196,0.8476636,0.7457396,-2.6700466,-0.041643098,-0.52725047,-1.2148969,-1.8033645,0.2436905,0.49941403,-2.9298956,-2.0252852,-0.6812175,-1.1452378,0.613915,0.63106275,-0.01848799,0.7994851}},
			{{-0.01595192,-1.2572938,0.68198705,0.26124382,0.641662,-0.7948483,-0.6343308,-0.57397574,-0.96203816,-0.74104136,0.62164676,-0.7436562,-1.0457451,-0.60850054,-1.8617029,-0.2999391,-0.37847218,0.6678042,-0.41183636,0.7086558}},
			{{-0.17956927,-1.467107,0.36408314,-1.2503222,0.8174676,0.78850794,-0.71645314,-0.6577471,0.7404123,-1.7387198,0.26613167,-1.6384454,0.4425627,0.14847042,-0.78489476,0.01347125,-2.0864162,0.21990535,-1.1012096,0.8184835}},
			{{-0.57023335,-1.391772,-2.1475918,-4.3651133,0.62581533,1.5164584,-1.0217779,-0.5318343,1.822747,-3.0066934,-1.2873352,-2.9052937,1.0600275,0.2630437,-0.53226715,0.51169133,-3.7908056,-5.2637315,-1.6727047,1.0903986}},
			{{-0.52767795,-2.6107392,-8.27047,-3.9067993,0.05821217,0.45179346,-0.42848146,-0.2175615,1.0498726,-3.6186957,-1.8428042,-2.4806764,0.1552082,-1.6762105,-0.9500483,0.095106274,-2.897252,-6.2431726,-1.648091,1.3366858}},
			{{-0.6426221,1.0529166,-8.678345,-0.6726559,-0.97339517,-4.553662,1.1803737,-0.9103235,-3.6852348,-2.732604,-5.687733,1.1820695,-2.0410857,-1.8510122,-0.047169846,-2.3725867,-1.426912,-8.337407,0.3348643,1.930513}},
			{{-1.3143848,1.4457488,-1.7633926,1.0154039,-0.44482473,-4.691282,1.2386998,-1.5315621,-1.6591095,-1.9965184,-4.064885,0.64440095,-2.7810142,-1.421254,0.75789475,-1.9414196,0.043090105,-4.1736703,0.75955534,0.8975732}},
			{{-0.19275106,0.20562771,0.26215583,0.9093468,0.25794888,-4.1661944,0.9412055,-1.8827757,-1.284355,-0.7927206,-1.7959061,-0.9382245,-1.3369777,-0.7341001,0.5091127,-0.98117954,1.2115258,0.46854162,0.7045803,0.42777827}},
			{{-0.798324,-0.9508906,0.6754302,-0.31972557,0.449288,-2.0962148,0.62119126,-1.7672268,-0.51591223,-0.1547734,-0.07168338,-2.546306,0.5057787,0.081473716,-0.76439923,0.038688723,0.81409514,0.82605094,-0.5663353,0.0931513}},
			{{-2.0547943,-1.0319499,0.41059464,-3.0558786,0.6486855,-0.15036665,0.14065933,-1.3586587,0.8275868,-0.6525431,0.36955187,-3.6261907,1.0326712,0.2659703,0.6673085,0.7835342,-0.18074036,0.20892648,-1.6415908,0.31100494}},
			{{-2.3758476,-1.0790728,-2.1287808,-7.037675,0.739997,1.3696635,-0.1560831,-1.2352136,1.6891729,-1.7250944,-0.007370657,-3.2659287,0.36849734,0.14300235,0.9623716,1.8196745,-1.2016879,-5.4825087,-2.274796,0.18472293}},
			{{-1.8095647,-2.0080717,-10.158643,-10.557332,0.40853894,1.4257499,0.22408535,-0.46138996,0.2877154,-2.6438196,-0.06870861,-2.989226,-1.262832,-1.9730976,0.57505566,2.0180683,-0.6549924,-10.22548,-1.7852268,0.6364764}},
			{{0.4461632,0.035924073,-8.933114,-0.09234405,-3.104706,-3.8404527,1.5087941,-2.6001623,-2.5813222,-1.7315909,-4.4373326,0.22585319,-1.8311411,-0.9743249,0.743812,-2.558144,-3.380929,-10.405035,2.2260115,1.9071939}},
			{{0.02887681,0.86866695,-2.048456,1.036821,-3.4686303,-3.6429226,1.4390725,-3.0782397,-1.528878,-0.6544497,-5.3817587,-0.88780946,-1.462845,0.06698537,0.7134323,-2.0425618,-1.6483307,-4.2938547,1.8612311,0.30221236}},
			{{-1.353149,0.19976316,0.12863132,0.688166,-1.9919474,-3.238766,1.297858,-3.2674317,-1.1637446,-1.1785905,-4.2000723,-2.230186,0.53818476,0.40496272,0.7842975,-0.93631274,0.43051586,0.52497816,1.1827406,-2.1583009}},
			{{-2.2870598,-0.9260336,0.63505286,-0.8714629,-1.9665816,-2.520462,1.27416,-3.4174604,-0.25524274,0.21522152,-1.9934747,-3.3176212,1.0344235,0.18277681,-0.47040686,0.15749346,1.3876754,0.98268235,-0.93098015,-3.157776}},
			{{-3.0142355,-1.0701418,0.503319,-2.765654,-1.3761343,-0.5833733,0.9854207,-2.278635,0.75932634,-1.0285566,-0.22319059,-3.2748365,0.25221038,0.06519047,0.5812951,1.4983788,1.229669,-0.10668402,-2.0521734,-3.7265184}},
			{{-3.4297566,-1.0007738,-2.0691917,-4.9278097,-0.5295277,0.7313919,0.48950118,-1.3534662,1.3243787,-0.83828425,0.38064462,-3.9052289,-0.96967643,-0.62908936,0.9287864,2.7324648,0.5805016,-4.119846,-2.381443,-2.7677054}},
			{{-2.9156199,-0.81683433,-5.7033615,-10.749707,-0.3134151,0.9188053,0.8333489,-0.7849528,-0.3210946,-2.3502462,0.74896735,-2.9670324,-1.4278603,-2.7352204,0.99415934,2.7581046,0.16869605,-7.9184847,-2.2087672,-1.0049262}},
			{{0.23007649,-0.84519726,-5.815947,0.053247098,-4.449718,-3.6603453,-0.3012601,-3.8331566,-2.7845886,0.23953818,-2.6958504,-0.51387656,-1.1872458,-0.18816285,0.7680459,-1.7141556,-3.9536788,-6.466744,2.5406759,-0.2937594}},
			{{0.4044079,0.49627456,-1.013548,0.87338257,-4.6598396,-3.9216983,0.040972915,-4.397419,-0.6225199,0.33603317,-5.70017,-1.9232651,-0.7010463,0.40177432,1.005409,-2.1844537,-3.5444944,-2.7960732,2.4837375,-2.8121498}},
			{{-1.2155232,-0.19252086,0.29376194,0.5111527,-5.03778,-2.6554613,0.004648141,-6.176399,-0.6982954,0.24920438,-5.3030076,-1.9827203,0.809459,0.31056398,0.21699119,-1.6209613,-1.1472052,0.8443454,1.1571301,-6.9383564}},
			{{-2.0595171,-0.96969587,0.35174164,-1.1054661,-4.9201584,-1.1765217,-0.019884787,-5.362471,-0.38249588,0.46863303,-3.305183,-3.6134408,-0.07649077,-0.6402233,-0.7983068,-0.18159266,0.8280754,1.1670134,-0.5752589,-4.8521304}},
			{{-3.9415202,-1.0034428,0.14742163,-2.450942,-5.236689,-0.4546354,0.08204179,-3.8641174,0.8691916,0.24460061,-0.86552376,-3.8773582,-0.67738736,-3.4745033,0.24794574,1.4960473,1.8535928,0.52248293,-2.1050828,-6.7286286}},
			{{-3.966066,-1.0987251,-1.2574024,-2.3734457,-3.5381832,0.3199425,-0.59736323,-1.4395517,1.3117973,0.17840424,0.23714986,-4.839118,-1.1276605,-4.32524,1.0083866,2.660343,1.6395502,-2.8541398,-3.0613978,-4.668307}},
			{{-1.6701401,-0.63114583,-4.3917913,-6.6908655,-1.6874294,0.7860625,-0.5538958,-1.5358143,-1.1143105,-0.6799871,0.47498232,-3.7217052,-0.7955002,-3.7695148,0.76843154,2.4424388,0.9572536,-4.1430864,-3.9546835,-3.3034892}}
    }};

    b1 = {
			-0.60210836, 0.18945721, -0.26552498, 0.2640773, 0.2180244, -0.4247985, -0.42111692, -0.4025271, 0.33799252, -0.019199293, 0.009905055, 0.724221, -0.49563923, 0.3963077, 0.26111564, 0.05002394, 0.28496352, -0.31952268, -0.50277406, -0.26096177, -0.69376665, 0.17874445
    };

    b2 = {
			0.74696624, 0.3846849, 1.5476271, 0.9965938, 2.132027, -0.5797378, 0.5872936, 0.55615634, 0.7494177, -0.0038235558, -0.18315186, -1.094765, -0.83366734, 0.430824, 0.020939147, 0.33440065, -1.2245744, -0.41185778, -0.84126973, 0.41897535
    };

    bout = {
			-2.407016, -1.8684318, -1.453138, -0.7228578, -0.9929487, -1.3253256, -2.1858861, -2.312111, -0.82392985, -0.23452261, 0.66780365, 0.09397568, -0.5964343, -2.452249, -1.836157, -0.39621726, 1.1300654, 1.6598724, 1.3922262, -0.07923506, -2.118753, -1.2336315, 0.3074725, 1.5711837, 2.5573614, 1.5986004, 0.3353753, -1.6927739, -2.1762505, -0.40862727, 1.2885319, 1.7262784, 1.236852, -0.2640607, -2.1991682, -2.550665, -0.6349711, -0.21275407, 0.4729668, -0.14078999, -0.49896806, -2.0212617, -2.2288725, -1.4638003, -1.5718899, -0.99863434, -1.472633, -1.4711082, -1.7023442
    };

    BN_gamma_in = {
			0.33374506, 0.88557863, 0.44883358, 0.6509423, 0.9652897, 0.7312501, 0.6207009, 0.34989476, 0.03389716
    };

    BN_gamma_1 = {
			5.6786466, 3.4703245, 7.2192545, 3.6617916, 2.8335927, 8.479494, 7.4399743, 8.978987, 2.6377435, 12.0498295, 3.0441427, 2.3028302, 8.49144, 2.6803298, 3.0796738, 9.467199, 1.8655304, 11.495024, 8.181942, 9.062074, 10.924568, 5.1439013
    };

    BN_gamma_2 = {
			0.17893454, 0.1971101, 0.11917643, 0.1379635, 0.12006492, 0.22108577, 0.18737441, 0.1874615, 0.1936909, 0.19751711, 0.19040631, 0.2782673, 0.29314083, 0.1359517, -0.28042123, 0.22073679, 0.25191358, 0.16657636, 0.313996, 0.15935662
    };

    BN_beta_1 = {
			-0.43478298, -0.40837958, -0.017061608, -0.5742905, -0.13781892, -0.099713504, -0.1672824, -0.1082751, -0.14699878, -0.08220062, -0.502702, -0.44958055, -0.20099999, -0.5442624, -0.2989939, -0.23611149, -0.011625829, -0.20057993, -0.28376234, -0.035505958, -0.23717202, -0.48265147
    };

    BN_beta_2 = {
			-0.25091317, -0.28539738, -0.12927878, -0.092178956, -0.18537998, -0.10818178, -0.12726577, -0.15941255, -0.21386056, -0.23076798, -0.0786912, -0.0819933, -0.09125569, -0.08367135, 0.299151, -0.2578983, -0.09530835, -0.04725937, -0.14871296, -0.08736679
    };
    
    mean = {
      57479.15596430949,35502.85275136948,53428.18770928572,46543.56181859651,17589.15456102924,46240.91567667166,53692.6507076018,35119.45035165905,57276.29801354078
    };

    stdev = {
      207677.34728660225,154962.88030363916,191576.07600115417,181696.56921362053,127069.28025293918,180191.77552818554,192388.33261921056,154020.73044885873,205901.42822234138
    };
  }

  else if(m_pcEncCfg->getQP() == 32){
    
    embs0 = {{
			{{0.27178106,0.1562536,0.29687002,0.19387302}},
			{{0.0005826703,0.04503933,-0.012307142,0.0020174638}},
			{{-0.0009466682,-0.00960623,0.011385387,-0.0021233724}},
			{{0.07860421,-0.050415576,-0.021897053,-0.10868027}},
			{{0.0068658455,-0.07425601,-0.0014656468,0.009458856}},
			{{-0.13870968,-0.18110885,-0.12789325,-0.22385864}},
			{{-0.026851362,-0.1893832,-0.09772639,-0.014543245}},
			{{0.14630552,-0.5311645,-0.7479692,0.11717225}}
    }};

    embs1 = {{
			{{-0.24382272,-0.02293396,-0.081736326,-0.308432}},
			{{-0.008461359,0.01221801,-0.040905356,-0.002273267}},
			{{0.007605914,0.002354036,0.015264896,0.003054202}},
			{{0.011686197,-0.2013675,-0.051249437,-0.057847563}},
			{{-0.0045802975,-0.010979273,0.08206608,-0.010402694}},
			{{-0.015234189,-0.26718876,-0.010512383,0.19019148}},
			{{-0.13180132,-0.052574083,0.20649542,-0.06308843}},
			{{-0.63776934,-0.26613146,0.4637781,-0.4099232}}
    }};

    in_h1 = {{
			{{-0.0007222936,-0.16729552,-0.084687114,0.0103214355,-0.025339566,-0.09943552,0.09472984,-0.16694811,-0.34150082,-10.190184,4.5201325,0.4631142,2.7785094,-1.5521905,1.767668,1.421891,0.5205178}},
			{{-0.03343856,-0.1283778,-0.0131712975,-0.0034226263,-0.0062364326,-0.09917375,0.10278422,0.012169279,-0.4293848,0.5047209,0.6589794,-9.607029,2.6884062,3.246944,-0.5600475,0.01772281,0.2585595}},
			{{-0.010130377,0.079061486,0.04699638,-0.07449548,0.052406017,-0.35339037,-0.025647739,0.12171437,1.4016248,-1.8974005,-2.8385973,5.1811395,-0.3313903,-9.074126,-11.952666,0.65552396,1.2825204}},
			{{0.012098921,-0.11555442,-0.010076737,0.007689509,-0.011739936,-0.4001231,0.026372138,0.14192547,1.3723336,-6.8596897,17.743591,-0.59488523,-1.2755471,0.5277019,-5.690724,3.2699444,-0.40980852}},
			{{0.003955381,-0.011323103,-0.010288339,0.0044367197,-0.008879286,-0.012994118,0.0054098517,0.013618643,0.32071397,0.49671078,-31.124008,-0.057790007,-0.013990506,2.0024776,5.149352,0.050921395,0.0873149}},
			{{-0.10146686,1.7724854,-0.9862231,0.056964666,-0.52665144,-0.26059234,-1.652385,-0.5145191,-0.70732236,2.1951363,-8.800853,3.2464542,-4.8417273,4.750741,-16.88138,2.5581748,-1.5810698}},
			{{0.021718698,-0.400763,-0.0062290244,0.24771564,0.12885864,0.03902137,0.23085143,-0.41254598,2.8093712,2.8800006,-8.70334,-2.2556064,-2.9603388,-6.6868806,-6.3275757,1.0831944,4.2791963}},
			{{-0.012122147,-0.09093992,-0.049011182,-0.025537329,-0.0676107,-0.11308133,0.063605316,0.052529424,0.4139118,-0.15221612,1.0963707,1.8587861,1.6831055,-12.7516775,1.8157268,0.13249719,-0.070772596}},
			{{-0.01025639,0.053482298,0.095963,0.010544924,0.0036512953,0.03790084,-0.0050996756,-0.015966693,-5.6376657,-1.710763,-13.909494,-1.4102304,4.1660333,-2.6015239,-12.0245285,-0.64730847,-4.4021273}},
			{{0.04843131,-0.3295965,-0.08245097,-0.007570107,-0.07049912,-0.89569366,0.08228426,0.23854816,-0.15401743,5.971915,2.9002252,0.5896565,3.0523846,-0.94868654,1.0847206,-5.965654,0.6110476}},
			{{0.0036841854,-0.79190207,-0.19270067,-0.014087026,-0.31594685,0.3466838,0.9202676,0.07184742,-1.5765762,1.2057122,5.380285,-4.2357626,-10.102044,-0.73568827,2.1889038,1.1567936,0.75898826}},
			{{-0.03505506,-0.16561766,0.012925229,0.08180655,-0.09259458,0.13518721,0.24068047,0.05570491,-2.6303434,-2.0732386,5.315644,2.6399543,2.0142345,8.132688,4.052682,-1.2937635,-4.8726416}},
			{{0.046944473,0.84348124,-0.2965688,-0.04258306,-0.32035396,0.12232241,-0.8285533,-0.08964593,3.2413516,-3.024646,19.912468,-2.1461077,0.45365998,0.60463434,21.316551,-1.6615728,1.7984512}},
			{{-0.030184686,-0.08089985,-0.02805699,0.040920258,-0.037074752,0.060537625,0.13086022,-0.031024417,-0.26306146,-1.8276106,-22.04345,1.9356097,1.559581,3.941801,-17.816648,-0.57147104,-0.06507938}},
			{{-0.016717996,0.016888928,0.014745781,-0.029568031,-0.018817203,-0.15261616,0.018475315,0.06585316,2.1849084,0.2987483,-8.278772,-6.3332415,-0.42156404,7.9931474,9.481775,0.39207855,-1.1558123}},
			{{0.022971887,-0.052988295,-0.046114657,0.038577326,-0.00093727943,-0.028171137,-0.030185271,-0.0679245,5.106966,-1.6262915,3.5399375,-1.5276268,0.6593049,1.5460359,9.697568,0.9474646,-6.2484307}},
			{{0.008496091,-0.010261549,-0.016958926,-0.0034088388,0.0010616628,0.023558544,0.0072159255,-0.01734435,2.7616343,-0.60846037,-5.0357113,-2.5235395,-1.4013602,8.646088,17.485699,-1.0206221,0.23682547}},
			{{-0.020039653,-0.081539415,-0.0081595285,0.009911343,0.05658702,-0.041935336,0.14773782,-0.107275225,-1.6523757,4.208569,18.970922,2.8962831,-2.5874503,-4.0757113,10.526065,-0.506362,0.0070581576}},
			{{-0.01619768,-0.0043124887,-0.022691846,0.016156964,0.015731832,-0.049525384,-0.010858982,-0.014762219,-7.078418,2.3212147,8.895474,1.0891938,-0.2804251,1.075912,15.847625,-0.22913104,0.20572418}},
			{{-0.012124357,-0.009979954,-0.02627301,-0.017955258,-0.016156718,-0.07271752,0.0054838867,0.016186016,0.40325722,0.6086555,4.633067,1.3196052,-0.29583824,-0.14050202,-27.535728,-0.052492503,0.34495702}},
			{{0.0047819563,-0.8823443,-0.40813714,0.050036695,-0.1836177,-1.0841756,0.5213021,-0.25813788,-0.11442537,2.817391,-2.9369898,2.1360528,-8.096101,4.321172,-6.8684907,1.8101056,-0.82485896}},
			{{0.084378056,0.16556393,-0.12137975,0.018480495,0.08509416,-0.8410589,-0.37786156,-0.18971847,-0.0014161459,0.12527682,-12.02242,-0.39602813,8.065005,2.0153701,-7.1658363,0.43440783,-0.8108726}}
    }};

    h1_h2 = {{
			{{0.46733418,-0.16096526,-1.3779025,-0.5296222,1.9656878,-2.678903,-0.8880296,-1.216787,1.4795922,-0.84599626,1.2665863,-0.34892502,-2.2388778,-1.0417763,2.1684644,-0.98697627,-1.8831486,-2.9729075,-7.588645,6.154331,1.8518957,-0.010169139}},
			{{-2.239726,-0.25071865,-0.75759727,-3.3061647,2.160107,-1.9541756,-3.476842,-0.58239156,0.9342924,0.22045998,-0.5249128,1.5794525,-1.3143777,-2.0263789,2.122949,1.1670951,0.19015893,1.3319883,-0.17510694,-4.9378767,2.8849232,-0.1608168}},
			{{-0.32668984,0.12981117,1.9893602,1.6028223,1.8229938,-1.9418769,0.22062477,1.2011495,-0.7327636,-1.8713267,0.1929405,-2.1528292,-1.1566775,-3.8592072,1.5171715,-1.392965,-0.015816906,-0.22044297,-1.0548717,-1.2520225,2.0698447,-0.27751744}},
			{{-0.39088312,-0.48896208,-2.8215268,-0.2341731,9.330177,-1.8825034,-1.3981965,-3.6152613,0.29165912,1.2681972,-0.6619432,-0.45701802,-2.3732085,-0.82476234,2.6876252,-5.483071,2.36215,-0.7234621,-0.6154284,-2.0054624,2.3300169,-0.44491145}},
			{{-1.2019414,-0.5693807,1.6550012,-4.528651,0.54617167,-1.9706175,1.2085274,1.5202929,-0.051495034,1.9063284,-0.571999,-4.360171,-0.9610814,1.7622883,-2.3884022,-2.808616,-0.45676467,-0.34714893,-2.0186343,-0.6157769,1.3386554,-1.0139021}},
			{{-4.5395913,0.8045448,-0.6106717,-3.6995637,3.6082418,-2.7155437,0.28509328,1.0844837,-0.42438748,0.20828837,-1.2932807,-1.4109381,-0.5461097,-2.3675847,0.89196956,-0.6727995,0.16249199,1.9317616,-1.5922046,-0.24382037,2.661238,-0.10319002}},
			{{0.5946189,-2.45921,-2.5728724,1.1818364,2.832401,-0.8874107,-0.52956796,-3.595096,-0.7241307,-0.08719586,0.9028002,0.039364092,-2.6604578,-1.216092,-0.9768696,-1.6624749,3.5694716,0.03578491,-0.43828747,1.698664,1.9840034,0.000570275}},
			{{-2.6304762,-0.5986014,-0.067243114,-0.9504156,0.32507783,-2.625632,0.13826767,-0.06997978,-1.1130337,-2.3771577,0.57608837,-1.3951561,-0.12350817,-0.88147324,0.48606938,-0.09164605,-0.923805,1.0993143,0.18785557,-0.14280967,4.138186,-0.2954275}},
			{{0.468315,-5.5819473,2.0000236,0.709903,3.4740512,-1.7059572,-1.9701988,0.7828689,-0.3682883,0.23618741,-0.15971734,1.9255381,-2.4380789,-1.1607496,-1.9258211,-1.2928586,0.99923146,2.6608217,-1.8541918,-2.712652,2.2900672,-0.10430681}},
			{{-0.72085524,-0.47975028,-1.5110831,2.4008346,5.8745685,-2.2218533,-4.238405,-0.51177734,1.5123538,-0.8808029,-0.024984503,0.6322753,-0.9533706,-2.0387564,-2.0170333,0.93927705,-3.4955702,3.8707309,-2.0564547,-0.8357152,2.1013367,-1.175157}},
			{{1.0637561,1.4857764,0.4274521,-0.25225174,-0.24372214,-0.6795046,-0.558656,0.91459143,0.63136506,0.6964916,-1.7033913,-0.7409801,-1.7115083,0.17429624,0.6747576,-0.17688109,0.13333942,-1.0635579,-0.28811434,-0.27161604,1.8960378,-14.912447}},
			{{-0.6282318,0.17943966,-1.0135455,2.5266778,3.1865728,-2.456607,-1.9518396,0.70890766,-0.1802982,-2.302862,-1.6265254,-0.22565912,-0.60230225,-1.0321451,-0.43637928,-4.6203,-0.3200667,-1.3377157,1.5921478,-4.4041443,3.2524745,-0.6245389}},
			{{0.593798,1.6201082,2.955274,0.72917324,-1.4906026,-2.4125473,-2.8606424,-0.5187431,-1.1542772,-1.0645046,-3.3801193,-3.0067158,-2.383004,1.792679,3.5507627,-0.35295236,1.9726,-3.4192345,-2.127963,-2.690809,2.0494382,-0.33464167}},
			{{-2.0947018,-0.64074415,1.5952574,0.6163578,-1.093383,-1.9596831,0.35330474,0.4930704,-0.7001369,-2.9672925,-1.081584,-1.1129534,-1.3214351,2.0102057,0.57424164,-1.3298684,1.1295261,-4.017822,-2.7188542,-1.6284165,3.4039228,-0.4144481}},
			{{-0.20164087,-3.709945,3.714549,-0.29140973,-2.525115,-1.843098,-0.7825483,-0.8830477,0.43374303,-0.44646263,-0.3217031,0.331924,-0.9780278,-2.3408463,-2.6435578,-1.1067016,-1.7348807,-1.7755988,-5.2977476,-3.128419,3.3178632,-0.71449393}},
			{{0.8924063,0.08004779,1.189749,0.11464974,1.9032722,-1.2627355,-1.731855,-0.83153474,-0.5415733,0.4851316,-1.7439804,-1.0998136,-1.953987,-0.46047154,-1.332169,-0.47011265,4.109115,-0.50828683,-1.2920228,-2.0471134,3.2486646,-9.758817}},
			{{-1.7788799,-1.9000838,0.364542,-2.4473166,0.7196706,-3.5619438,-0.4612579,-1.0581598,1.7788649,-0.52018183,0.09662012,-2.316609,-1.2880237,2.8518803,-1.9575485,-0.19621162,1.6758258,-2.4917734,-1.2927473,3.0174143,3.3786001,-0.22503905}},
			{{0.261765,-1.7993783,1.0077285,1.0989226,0.47731355,-0.85956806,-1.4211967,-2.8411431,1.0587494,0.46575153,-0.13725445,-0.05905216,-3.4399655,-0.5315417,-2.4100509,-0.92937803,4.749442,1.61849,-1.0853202,-1.5577716,1.650838,-0.46848843}},
			{{-2.9607034,0.76106095,0.76675797,-2.0978444,-0.9090318,-1.1492908,1.4126669,-0.9591291,1.3960541,-0.99186033,-1.4717724,1.7352757,-1.48312,-0.4730525,0.016230034,-1.5283372,2.0264761,-0.7180739,-1.46674,1.1250124,1.9371375,-0.014461352}},
			{{-1.7776676,0.11604641,0.030551005,0.5888615,0.6556674,-1.4705982,1.0219507,0.26519695,3.413277,-1.1236167,1.4105083,-1.5013844,-1.4753498,-1.8126218,1.3437761,-0.13049941,0.94600517,2.3235593,-0.2902499,-1.2380612,2.0045154,-0.3000127}}
    }};

    h2_out = {{
			{{-4.64132,1.1369852,-1.8325193,0.22273482,-1.5715749,-2.7404761,-0.6771207,0.1955358,-1.6337425,0.5705972,0.13055302,-2.3612452,-0.74507326,-4.416316,-4.0692616,-7.883145,-4.627503,-4.4562755,-5.6485543,-2.8381772}},
			{{-2.835911,0.356258,-3.018914,1.6872808,-1.7443286,-5.1842885,-0.008840262,-1.6014357,-0.7327274,0.8978315,-0.3534597,-2.43241,0.5164936,-5.4714007,-1.7563887,-3.7592566,-5.7717543,-0.6426615,-5.51474,-3.3029397}},
			{{-0.77702326,-0.6288999,-1.6689929,1.471405,-1.3559994,-6.092747,0.46012995,-5.1427197,0.39436832,1.2766814,-0.24640596,-1.6136099,-0.38691938,-2.0488937,-2.0321171,0.39514866,-4.2556434,0.45639232,-5.493548,-5.149797}},
			{{-0.28564703,-1.2109267,-0.4107146,-0.15872863,-1.3188443,-5.720401,-0.1030471,-6.925652,0.47896594,0.36411145,0.679513,-0.4337025,-0.23182693,0.7506746,-0.9976604,0.866016,-0.8049578,0.7299026,-2.5790653,-4.943276}},
			{{0.37295803,-4.136207,-0.5368652,-1.6330681,-0.6701518,-5.7497005,-0.5810051,-5.742883,0.72294915,-1.9608617,-0.44035405,1.3717463,1.2805601,0.8644782,0.16421257,0.8488268,-0.44948852,0.95102066,-1.1669225,-4.3795877}},
			{{0.18159942,-4.734642,-0.874949,-1.5010068,0.15886067,-4.9687467,-1.3125973,-2.9839275,0.3662074,-4.61096,-0.46615097,2.9502466,1.0177525,0.5899759,1.6236233,-2.7011774,-0.4012876,-0.40828288,0.4230351,-3.008887}},
			{{0.4249136,-2.8374205,-0.057907797,-3.5415227,0.63056844,-3.0080311,-3.358974,0.13588786,-1.6948344,-7.26294,0.196507,1.3988572,-0.44927812,0.8364068,1.3551265,-6.5319858,-1.1904122,-3.4278612,0.31767645,-3.573261}},
			{{-3.20262,1.1515619,0.106605686,-0.0696857,-2.143054,-1.0059266,-0.8152101,0.64399713,-4.7898874,0.5393334,-0.26513228,-2.2667675,-0.42138067,-3.7716024,-4.3281693,-11.173831,-3.1533742,-5.707086,-7.7671933,-0.7276168}},
			{{-0.96505296,0.76786387,-0.27144077,1.0032752,-2.8704824,-3.3137019,0.62281173,-0.57096124,-2.3059878,0.45603722,-0.5515004,-2.1239612,0.7004048,-3.520968,-2.6558294,-5.37325,-3.381747,-2.0618875,-4.271785,-0.88537985}},
			{{-0.024491716,-0.8082561,0.37619418,0.5844133,-3.1914828,-3.5661187,0.81418395,-2.0678139,-0.7321738,1.2099994,-0.7141873,-1.5353075,0.5553649,-1.4102019,-1.8720819,0.15690924,-2.5009596,0.47259364,-3.94065,-1.544744}},
			{{0.13080105,-1.5704082,0.93120354,-0.9284301,-2.5646334,-3.2431452,0.41421324,-4.484749,0.4014133,1.1336114,0.5306443,-0.7070735,0.5960404,0.6806369,-0.9830494,0.6772413,-0.23751731,0.86487216,-2.594861,-2.225923}},
			{{0.26276353,-3.3253946,1.4166291,-2.3963234,-0.57290524,-3.8031888,-0.9301282,-3.1083431,0.40391427,-0.092046596,-0.7484006,0.30867675,0.24697714,0.8292097,0.5960023,0.54951376,-1.0261487,1.0275848,0.14174825,-1.7687956}},
			{{-0.014214518,-3.204736,1.4928802,-3.052362,0.5092636,-3.5946965,-3.802507,-1.6183009,0.033381283,-2.1178768,-0.48001748,0.9402496,-0.4227204,0.21353345,1.8425212,-4.68835,-0.51338553,-0.8968556,0.67434895,-1.3004578}},
			{{1.1136833,-2.4686944,1.3940722,-5.127874,0.401643,-0.82810426,-6.735117,0.23185687,-2.4052045,-5.7054806,-0.4858316,-0.09340516,-1.0944233,0.6253933,1.8141207,-9.817379,-2.190899,-4.0574512,0.42730343,-1.1965728}},
			{{-2.1304753,0.79050916,0.63705677,1.0855416,-2.9866004,-0.07737951,-1.168241,0.5726624,-6.552535,0.10118653,-0.24485856,-1.030515,-1.3832545,-0.51677495,-3.4782186,-8.441324,-3.194976,-8.999006,-6.2980857,0.5512306}},
			{{-0.3216742,0.721969,0.5211723,1.5658824,-3.807778,-0.8556781,0.3107311,0.057663932,-3.3248494,-0.66333133,-0.7329323,-0.95648324,0.513046,-0.66769266,-2.9360328,-5.3089747,-4.654693,-2.7225497,-3.4781923,0.8805667}},
			{{0.3022813,0.22268821,0.45368358,0.5129111,-3.3539157,-1.7232853,0.90649015,-0.10933901,-1.1010753,0.21063721,-0.65311867,-0.5942722,0.29354903,0.33582997,-2.0247934,0.4716182,-2.0007062,0.13339205,-2.5605597,0.68144625}},
			{{-0.36906597,-1.0952324,0.6913394,-0.6979516,-1.8220023,-1.9299837,0.28820395,-0.5256252,0.24477075,0.9320882,0.7493267,-0.49388933,-0.4677617,1.0287491,-0.8321453,0.5859534,-0.37415695,0.87193674,-0.6906826,0.15669456}},
			{{-0.31492305,-3.2454355,1.2208158,-2.018484,-0.0745525,-2.0994997,-1.6854798,-0.75739986,0.61865133,0.93594724,-0.48570886,-0.09126207,-1.4489064,0.2372152,0.6011615,0.62337714,-0.36740652,0.80930644,0.73821694,0.11147376}},
			{{-0.33357468,-4.3735085,1.2708377,-2.8772058,0.88268256,-1.324318,-5.3645473,-0.31382674,0.7943057,-0.05716373,-0.828104,0.09987043,-2.3405118,-0.53623164,1.6894977,-4.43922,-1.0040262,-1.2505311,0.55017155,0.27421388}},
			{{0.18840425,-3.2807355,1.2855456,-4.2484875,0.9548698,-0.26615205,-6.471484,0.2636379,-0.49968413,-2.504522,-0.5561459,-0.4636302,-3.6124709,0.31923756,1.2674261,-7.036847,-2.0202248,-7.9371986,0.36220703,-0.32917067}},
			{{-0.2002069,-0.42698416,0.62772024,0.7991651,-2.2479608,0.7351414,-1.2428603,0.37224802,-7.2284126,-0.9365154,0.7699978,0.4767024,-1.0627114,0.81742746,-2.4069266,-5.3771715,-7.398682,-8.386695,-1.503167,0.22258642}},
			{{0.8030032,0.59359014,0.45554006,1.6238322,-3.3354154,0.14017653,0.2143236,0.32871497,-4.4874263,-1.7533642,0.20229772,0.55602837,0.52311367,0.11310509,-1.9199468,-4.634042,-4.606863,-2.8053477,-0.7183792,0.9253407}},
			{{0.49217883,0.4099223,-0.035376877,0.9622763,-2.717444,-0.07474351,0.8056257,0.6269057,-1.816134,-0.9844033,0.34420285,0.50950456,-0.49287358,0.61147237,-1.1462232,0.5043969,-1.7730517,0.096030325,-0.48217767,0.8995497}},
			{{-0.39117405,-0.22266501,-0.44885215,-0.34215117,-1.0711782,-0.47753507,0.017176688,0.7635422,0.035682276,0.12473259,1.6635188,0.13697407,-2.0706623,0.31504968,-0.69255257,0.5617428,-0.30998263,0.74585265,0.38133913,0.8264223}},
			{{-0.65823174,-1.5957261,0.16531724,-1.6667714,0.28205204,-0.38203084,-1.6290336,0.7066675,0.77146775,0.9065797,0.269699,0.0978968,-3.1291394,-1.2404009,0.61261356,0.558066,-0.66780555,0.55381846,0.70195806,0.69949263}},
			{{-0.6845914,-2.7018592,0.5239354,-2.2859054,1.1847544,-0.20152661,-5.884024,0.44348428,1.1486132,0.98703927,0.009692357,0.14693715,-3.6623175,-1.9735426,1.0627953,-4.079725,-1.9405618,-1.4829185,-0.48284125,0.71969485}},
			{{-1.7185258,-2.1409085,0.566894,-3.4455626,0.4333808,0.5149673,-6.0579987,0.5614159,0.35945842,0.5762021,0.8017133,0.035020746,-3.902038,-0.41177422,-0.14347878,-6.114112,-3.4711282,-7.3934565,-1.1563864,-0.17911072}},
			{{2.2024329,-0.8684066,1.1265314,0.1591946,-1.0576082,1.0049274,-1.039158,0.41683656,-7.3319087,-4.7724743,-0.5466509,2.0316374,-0.24073361,0.5836809,-1.0526205,-6.9159327,-5.937476,-9.447503,0.00058949686,-0.1958307}},
			{{1.5409209,0.046562366,0.20290743,1.4213653,-1.9755388,1.0763596,0.7848079,-0.3799206,-4.3609877,-3.5062225,-0.73385257,2.4783144,0.42880553,0.16852313,-1.2742313,-4.5850673,-2.6424017,-2.8879156,0.5784127,0.39164877}},
			{{0.32741335,0.8600117,-0.5062592,0.9541519,-1.1656697,0.7654224,0.8345538,-0.10066152,-1.9564823,-2.6362734,-0.26486558,2.2016635,-0.8146398,0.08398038,-1.014033,0.44910437,-0.18892173,0.014212807,0.69276613,0.56045336}},
			{{-0.98694485,0.84602356,-1.2553651,0.090550534,0.28123662,0.68173116,-0.05728483,0.26483548,-0.081579685,-0.89052314,1.0331206,0.87749076,-2.0207214,-0.8829474,-0.54151034,0.6212593,0.18841141,0.8894941,0.66395026,0.48751888}},
			{{-1.7773354,0.38519704,-1.0566558,-1.4467659,1.178564,0.2667244,-1.1529499,0.5398566,0.85549265,0.42299768,-0.40053135,0.7543808,-2.0618038,-3.1319366,0.013173699,0.5404707,-0.85574657,0.6371012,-0.1826202,0.8710421}},
			{{-2.0214014,-0.35287148,-0.5061023,-2.512596,0.94201016,0.65701306,-4.547462,0.7052512,1.3152117,1.2545813,-0.77839303,0.7643634,-2.543935,-3.8999975,0.2684163,-3.5838888,-3.2187757,-0.93540174,-2.5777404,0.62497157}},
			{{-3.001203,-0.3800996,0.14656454,-3.9397473,0.31234506,1.0240198,-6.03669,0.817886,-0.027606437,1.8285713,-0.111747995,0.015730528,-2.4930966,-3.4357817,-0.7560588,-5.376081,-5.3669987,-7.542088,-3.2542405,0.29031017}},
			{{2.8661633,-2.0995734,0.2961033,-0.35960883,-0.60084224,1.7249774,-0.34808192,-0.86449194,-6.5743346,-4.865807,-1.1659902,2.505329,-0.8701213,0.59281486,0.6767596,-7.8682237,-2.771971,-9.651418,0.284777,-1.2205895}},
			{{1.9628311,-1.7790947,-1.7991489,1.3223401,-1.0194936,1.5197997,0.9200435,-1.8336002,-3.7685378,-2.9190552,-0.92058843,3.7989974,0.2997831,0.19843826,-0.1255937,-4.410626,-0.66212714,-2.7914145,0.858458,-0.64308316}},
			{{0.50760657,-0.033835445,-2.7883754,1.1671575,0.06425556,1.3830824,0.8381196,-1.6747828,-1.7643387,-3.0055106,-0.7605719,3.2569358,-0.7437273,-1.0571007,-0.7735097,0.16502881,0.24831669,0.08906485,0.8380041,-0.61743295}},
			{{-1.2900958,1.3156971,-2.9144948,0.33339435,0.8925835,1.2279394,-0.18545695,-2.8052592,-0.19278644,-1.7353884,0.70554316,1.7888185,-1.3182406,-2.028158,-0.69508106,0.6389352,0.0534643,0.97046757,-0.015323054,-0.40964276}},
			{{-1.7870722,1.5231268,-3.5348957,-0.73939884,0.94907975,1.0221761,-0.6792453,-0.91001654,0.82560056,-0.19222037,-0.84078664,1.9314995,-1.522413,-3.5925133,-0.21673977,0.4494155,-2.1782513,0.7774022,-1.8787498,-0.20073593}},
			{{-1.8786932,1.2541797,-2.7938464,-2.0654604,0.5631357,0.7984796,-2.5864823,-0.4267551,1.3021201,1.0814896,-0.833722,1.343655,-1.8610734,-4.3529468,-0.15657738,-3.4316878,-4.059943,-0.7361188,-5.246602,0.3011993}},
			{{-3.496095,1.0482326,-1.2464411,-3.2550714,0.2641977,1.2837005,-4.1500487,0.0860862,-1.0054256,2.2752185,-0.7939252,0.38390014,-2.0972154,-3.9937305,-0.923403,-6.342039,-6.6946,-4.7059073,-6.681561,-0.13032338}},
			{{2.6118412,-3.1181183,-0.97527033,-0.11928093,-0.7006795,0.9228049,-0.11262338,-1.1272131,-4.8631377,-5.13056,-0.7729515,3.6842325,-0.7914046,0.79793584,0.19258364,-4.1799736,-0.85630065,-5.3216987,0.34394827,-5.459711}},
			{{2.0086062,-4.026115,-4.3398905,1.4355531,-0.7581475,0.89429903,0.8872702,-2.0960033,-3.4756486,-3.5886245,-0.9907807,4.6175046,0.72647727,-0.60761726,1.2482634,-2.6044352,0.399852,-1.1000586,0.69595397,-5.8887854}},
			{{-0.026971404,-2.6137385,-3.9270625,1.4830359,0.47112235,0.8943593,0.6538397,-3.8049312,-0.91588384,-2.6597972,-0.24518184,4.5957193,-0.30709636,-1.4543177,-0.9410946,0.32965308,0.7232972,0.47581753,0.05103788,-6.4390063}},
			{{-1.4554136,0.5819419,-3.436186,0.5659132,0.2958416,0.74976975,-0.3401571,-6.2436733,0.2755433,-1.2508069,0.7764188,2.2251587,-1.5458064,-1.3842255,-1.175878,0.81294316,-0.5002313,0.8856272,0.043540806,-3.8578856}},
			{{-2.4795978,2.1078074,-4.2336874,-0.49785936,-0.5912617,0.2935651,-0.5432309,-5.0369854,1.2081741,-0.4390384,-0.50541633,1.3932782,-0.8941555,-4.552408,-0.26790732,0.6349498,-2.857046,0.53916335,0.15488032,-3.0564868}},
			{{-3.0837457,2.2909873,-5.3588963,-1.7100333,-1.170069,-0.26991645,-1.2460984,-1.7901479,1.4242672,0.7754235,-0.8465658,0.9543244,0.29491016,-5.9951854,0.23150688,-2.4694288,-5.2654614,-0.31122708,-3.4823654,-2.4903576}},
			{{-4.8894086,2.1090863,-2.628496,-2.4842184,-1.4630647,-0.074353784,-1.7425954,-1.0901592,-0.47583085,2.005091,-0.57950026,0.9835185,-0.9630858,-3.8942652,-0.9640933,-3.4643373,-6.522356,-3.5642462,-5.324626,-1.9482251}}
    }};

    b1 = {
			-0.21988548, -0.3915779, 0.15806352, 0.18345469, -0.05338209, -0.09581837, 0.40492305, -0.24858864, -1.5188023, -0.24470997, -0.84820986, -0.2927701, 0.11499235, -0.1197592, 0.05637328, -0.14485307, 0.36029956, 0.24858746, -0.10255062, -0.026909785, 0.21854764, 1.182476
    };

    b2 = {
			-0.50385183, -0.26420397, 0.7456199, -0.24981897, -0.33663076, 0.15321241, 0.51657075, -0.030079033, 0.427477, -0.14844337, 0.87332654, -0.16277255, -0.50295633, 0.32799828, -0.3448195, -0.47166607, -0.7966887, 0.24177065, 0.91495615, 0.41965398
    };

    bout = {
			-1.4756167, -1.176304, -0.83746314, -0.2405762, -0.8882552, -1.2144722, -1.3524338, -1.6364152, -0.33822158, -0.082043014, 0.6148163, -0.12118895, -0.42084625, -1.6480861, -1.2325834, -0.2086811, 0.95432854, 1.355636, 0.87519836, -0.33598152, -1.2758226, -0.6940351, 0.5417856, 1.4673846, 2.443634, 1.2836998, 0.3045752, -0.78648305, -1.306887, -0.22562444, 0.9556252, 1.3675799, 0.74045485, -0.5069496, -1.4118581, -1.673896, -0.54476565, -0.2686828, 0.5725887, -0.34062436, -0.74962604, -1.7920586, -1.6616081, -1.4777868, -1.1584407, -0.43348134, -1.0076888, -1.5036361, -1.7044656
    };

    BN_gamma_in = {
			0.35053104, 0.36907583, 0.03588921, 0.3640476, 0.60374045, 0.20899928, 0.039141834, 0.88798696, 0.49755174
    };

    BN_gamma_1 = {
			8.094861, 10.14118, 3.5887334, 4.053101, -12.225097, 3.8598945, 2.1431384, 9.703565, 15.433145, 5.1180778, 3.9629874, 5.1332684, 3.7080405, 7.685786, 5.3347344, 5.917572, 3.5943801, 3.5081847, 7.457506, 10.0471945, 3.3481328, 1.6324234
    };

    BN_gamma_2 = {
			0.32611236, 0.23336765, 0.15358745, 0.20659748, 0.20135191, 0.18418075, 0.13467248, 0.18075173, 0.16075507, 0.2621407, 0.30043578, -0.22415756, 0.1801741, 0.19347832, 0.20926279, 0.1379608, 0.20675619, 0.13937277, 0.11991841, 0.12588546
    };

    BN_beta_1 = {
			-0.17648306, -0.21089317, -0.3293891, -0.35526356, 0.055853844, -0.00519813, -0.3108609, -0.18307582, -0.042325266, -0.2679411, -0.19158739, -0.3925915, -0.029167313, -0.34923583, -0.47308847, -0.15546264, -0.41969118, -0.26217964, -0.087082855, -0.11809182, -0.46828288, -1.0911832
    };

    BN_beta_2 = {
			-0.13759148, -0.12784384, -0.13686973, -0.13765632, -0.099115826, -0.09942085, -0.117987104, -0.078117676, -0.09967579, -0.11933789, -0.35844833, 0.16602351, -0.084899396, -0.12782098, -0.13944024, -0.023381552, -0.0436448, -0.06435467, -0.1278522, -0.09936514
    };
    
    mean = {
      54923.98630026454,34253.43096171213,51066.430216003784,44663.03649485166,19008.86047394739,44379.26173907137,51312.456969054336,33936.88848975141,54689.80229219386
    };

    stdev = {
      203013.1651297521,152801.63182374722,187379.1033813536,178073.72926964832,128008.23973479208,176605.8923592302,188119.38609298086,151866.63960198764,200972.0251179253
    };
  }
  
  else if(m_pcEncCfg->getQP() == 37){
    
    embs0 = {{
			{{-0.3470052,0.042158753,0.06555167,-0.24942112}},
			{{0.0012416508,-0.03624891,-0.014858524,-0.00029238863}},
			{{-0.00027659867,0.010932403,0.011401979,-0.00010721941}},
			{{-0.12817083,0.02549043,-0.014852615,0.2388338}},
			{{-0.0058533926,0.07687753,-0.004683608,0.0073759016}},
			{{0.050861236,0.16400601,-0.08467209,0.06425282}},
			{{0.03247923,0.20594762,-0.11652993,0.005445059}},
			{{-0.51205677,0.49682516,-0.47895297,-0.5936651}}
    }};

    embs1 = {{
			{{0.09159002,-0.2794488,0.13715395,-0.1154376}},
			{{0.011388057,0.0027535467,0.0050375396,-0.037943646}},
			{{-0.010741315,-0.0013891995,-0.0005055049,0.011862188}},
			{{0.02216715,0.10183957,-0.4033617,0.04653914}},
			{{0.018742219,-0.0013493678,-0.003195422,0.07908031}},
			{{-0.0030735182,-0.16288877,-0.47302285,0.061246034}},
			{{0.15841623,-0.0009215101,3.5366975e-05,0.21555556}},
			{{0.64163005,0.043512583,0.07277514,0.5386578}}
    }};

    in_h1 = {{
			{{0.028487116,0.58744574,-0.12733692,0.006922589,0.13352378,0.0456484,0.26455435,0.44636774,-5.2501707,3.9604583,0.8839479,3.133904,2.20829,1.3143345,-0.63779205,-10.249599,-0.36710617}},
			{{0.009585747,-0.0019583306,0.014227383,-0.0060739852,0.0073650093,-0.0114870565,-0.06778173,-0.01214751,10.291096,0.6218175,3.5571086,-0.6588866,1.6871678,-8.18242,-0.096049234,-4.6257796,3.1888103}},
			{{-0.046415113,-0.26087904,-0.10891892,-0.61874014,-0.07918267,-0.16925544,-0.24479899,-0.36110428,0.014904348,0.09155421,-0.46558878,-0.081500374,0.34462786,-0.3541816,-0.023853516,-0.045855336,-0.35737133}},
			{{-0.019205611,0.045933425,0.031324923,-0.0009301365,-0.061523188,-0.15932998,-0.039449837,0.15111642,-2.2377045,-0.08956652,0.94129,-9.415207,1.5198684,9.927203,-0.08748579,-0.0119918175,0.64599913}},
			{{-0.03673015,0.1994141,-0.05867599,0.07977603,0.07193017,-0.043180365,-0.9980351,0.1976254,-17.967457,4.5192986,-2.9329088,4.170119,-4.652027,4.0497,-0.079751804,-3.3641257,-0.30195454}},
			{{0.03747097,0.09668131,-0.07306357,0.04468251,0.05068288,0.04353597,0.06804956,0.069112614,8.368875,1.2510496,-15.533346,0.14558661,-0.85382164,2.761696,0.40586287,-1.3547198,1.8910869}},
			{{0.01926122,0.019252121,-0.044287607,0.013237818,0.031175122,-0.028871244,-0.06467707,0.010172096,-1.0804204,1.9007608,2.4530184,1.5416938,3.1077151,7.4675975,-5.3676744,-2.6097066,-0.119536564}},
			{{0.09117301,1.9000092,-0.088182844,0.4905155,0.38624063,0.1767619,-3.5602224,1.4185988,-2.5119727,1.5099303,-3.2609708,-0.26241827,1.8147396,-8.902644,-2.5974658,5.6137815,-2.2785418}},
			{{-0.26159242,-0.31255746,-0.24432203,-0.25010538,0.22745192,-0.666661,-0.26207027,-0.25490046,0.33535188,-0.07541194,-0.41619214,0.17148615,-0.349629,-0.033802442,-0.051431295,-0.14048253,0.013384609}},
			{{-0.032573663,0.35236022,-0.021458007,0.12589741,0.08076667,0.049469173,-0.96555346,0.2139328,8.808344,-1.4966321,2.1296363,-1.468395,-3.7014341,12.70431,-0.10775348,-0.16213827,0.8102665}},
			{{-0.010154817,0.5059937,-0.0059902435,0.14417644,0.02298734,-0.056181166,-0.85394585,0.4371525,-10.426561,-1.6557211,8.442343,4.6940784,-3.6215365,-6.2604566,1.1979817,1.5039291,0.21712995}},
			{{0.10430359,0.26794562,-0.040148802,-0.010896125,-0.070140876,0.20411465,0.37141994,0.2216136,-3.4903293,1.2269421,3.0753305,-8.798381,-2.5718477,1.2740256,4.1102676,-0.19831038,-0.87333816}},
			{{0.12958668,0.41264552,0.31836137,0.1731022,-0.06743995,-0.35779265,-0.13598801,0.3742843,3.5167255,0.0950021,0.5898496,0.96378464,-12.782195,1.5799283,0.41500384,-0.5230079,0.5968525}},
			{{0.007798082,0.024223221,-0.028990116,-0.0032114845,0.030958481,-0.007862107,-0.031545497,0.023963535,-0.07714095,-1.4448835,-14.6036215,1.258219,2.9940293,4.5572023,0.4281824,0.6807963,-0.41831034}},
			{{-0.05984264,-0.030537937,-0.024150824,-0.038415164,0.18978654,0.13996215,0.07802615,-0.32450363,3.8325648,-2.5272079,11.958755,2.1447852,-0.93584204,1.2197663,0.23129575,-5.231378,4.022215}},
			{{0.022414405,0.0229333,-0.031974986,0.014366598,0.021446439,0.028210908,-0.021067152,-0.004786928,4.7724333,-0.72878337,3.2930527,-7.7491,3.3162503,5.7525573,3.4190712,-1.9653927,-3.5811095}},
			{{-0.23320106,-3.1918018,0.27445933,0.66852194,-0.5803425,-0.052845024,-1.3683163,-2.92117,10.859421,5.2126746,2.1705716,0.5389596,0.46337146,1.7162265,-0.2223555,2.9055443,-0.43810305}},
			{{0.017062383,0.9283131,-0.2860547,0.02280253,0.29778588,0.19638236,-0.2520731,0.7882429,-0.29630134,3.8015418,0.22604842,1.2872896,-11.964098,4.090107,-0.27987155,2.404775,-0.7438043}},
			{{0.071554095,1.0721743,-0.29435992,-0.043731745,0.3214663,0.23345414,-0.38710737,0.9980179,-1.1476817,-7.017208,1.465731,4.8261995,4.6214976,5.888428,-1.9441315,3.019119,-1.8874016}},
			{{-0.006656082,0.01502027,0.0012240972,-0.020596573,0.0232439,-0.017947096,-0.054768775,0.007836323,-1.6310424,2.3213763,1.3708874,5.744501,1.4119906,-0.19403173,-0.06544226,-1.8982857,-6.5750732}},
			{{0.0042947116,0.03532192,-0.041659974,0.018459154,0.04488036,-0.010568099,-0.014390746,0.022625582,-26.280369,0.3717449,2.3602593,1.5708874,-0.510845,0.3132474,0.08039524,0.46968025,0.019097803}},
			{{0.059902143,-0.61530465,-0.60209966,0.060630273,0.52859634,0.12125519,-0.15616192,-0.6996632,-9.569188,1.1843466,-3.1146057,3.7777507,-6.2113295,7.9558496,-0.99439037,4.230964,-1.7831339}}
    }};

    h1_h2 = {{
			{{0.5659353,-0.3473509,-0.0018178677,-2.0095031,1.1754417,1.1146034,-0.35455152,-2.7462964,0.004027741,1.4900099,4.0292897,-2.316443,0.846824,-1.2957547,-2.1435113,-0.31653377,-0.060113356,1.2642117,-0.016790831,0.26233566,-1.3727378,-1.5706018}},
			{{-1.8439622,-0.85611814,0.0022501748,-0.3232995,-1.2769588,-0.564967,-0.4573722,-0.86497283,0.0006209062,4.5132537,-0.4505499,-1.2997379,-0.46356076,-1.3451667,-2.6391742,-1.2325166,-1.3031511,1.8742005,-0.346756,-9.220784,-1.8443484,-1.4090095}},
			{{-1.3127215,-0.18128084,-0.002398593,-3.260809,0.50380063,-2.0524151,-10.92751,-1.0561857,-0.00018922813,-0.20204791,4.679287,-2.3663027,0.37504128,-0.12210816,-2.0667002,-0.36201847,-1.7941772,1.0645295,-0.44914475,-0.6641386,0.6411212,-1.1924679}},
			{{-0.016629891,-0.23857895,-0.0026113952,-0.7322689,-0.35608223,-0.42618227,0.031472284,-2.0212963,0.0040620607,4.6810517,-0.0027015742,-0.49023074,0.7696344,0.24172086,-2.6126487,-0.46554288,-0.0063375104,1.2242632,-0.31066668,-0.6667813,-0.16213854,-1.218195}},
			{{1.4892274,-0.06015345,0.008132835,0.26481903,3.3893514,-3.329973,-0.7332599,-1.7437502,-0.0061530066,4.3154974,-0.7909039,2.1131785,0.37669456,-0.86453897,-2.9953256,-0.9474341,-2.99427,-0.54921234,-2.1533508,-1.0919932,1.7839735,-1.285744}},
			{{-1.2576166,-1.1994818,-0.0030414946,-1.202925,-0.023112858,0.6034879,1.0494972,-0.68230236,0.0027750456,0.02608144,-0.07087468,-3.9092765,-0.6575816,0.89670485,0.15849335,-6.6639266,-0.5045244,3.8966837,-0.9162733,-0.5979901,-3.9488547,-1.4129634}},
			{{1.4958621,0.24212563,0.0011490273,-0.44345513,2.0401196,-0.16408117,0.25208306,-1.6307474,-9.902002e-05,1.6626536,-0.81003153,-5.800068,-0.47538352,0.31507525,-0.88667446,4.6247396,-2.1477695,-0.7263978,0.42460492,-0.063284,-1.043304,-1.2102357}},
			{{-4.6846776,-1.2079033,0.0027933966,0.5052647,-3.7345734,0.2967786,-0.62931395,0.46584418,-0.0090919165,2.0076854,2.5421634,-1.1497388,-0.11689365,-0.2440084,-1.6970775,-0.90601707,0.001896364,2.356122,-2.7823882,-0.22995824,-0.112673916,-1.1492145}},
			{{0.20336665,1.4035715,0.006101443,-3.5534072,2.7215195,0.21430296,-0.6889119,-1.2360868,-0.006861377,-0.37389746,3.7118754,-1.367458,0.087027386,-0.8526156,-2.3631892,-1.6357281,-2.797136,0.56631297,-1.8123754,-0.59458065,-4.5689487,-1.4816822}},
			{{1.1909612,-9.516781,-6.2753505e-05,0.23215707,0.095701724,-0.4386482,2.3917763,1.4349223,-0.00029317368,-1.0468974,-2.232467,0.8351794,-0.3118017,1.3450117,-12.837525,-3.6602128,-1.0761614,-1.4425132,-0.546575,1.3548993,1.3878235,-6.65607}},
			{{-0.15820087,-2.5410507,-0.0054635284,-0.48989925,1.035907,4.9434743,-1.1589592,0.13605414,0.003959364,-0.03785907,1.4567289,-0.052046075,-1.1676222,-1.2301877,-1.1678419,-1.2693535,-2.0805666,0.23102549,-2.2018816,-0.9512046,2.9083843,-1.2200328}},
			{{-0.12720378,-0.313762,-0.01671872,-0.28449157,2.5956838,-1.3364794,-0.6250202,-0.82902735,0.009858945,2.7698245,-0.83094966,-0.26785424,0.088314004,-12.915248,-1.4667435,0.90915644,-0.08854087,1.2778928,-0.31618628,0.341816,-1.5468997,-1.5313972}},
			{{-0.60114217,-3.0106843,-0.00085674337,-1.2072574,0.9373956,-4.0684605,-0.67165655,-1.1346748,0.0019777345,0.902767,0.46654716,0.087745495,-1.2477658,-0.24267818,-0.16472885,-0.7100164,-2.3559208,2.1695929,-1.5534433,1.1582007,3.0888205,-1.0594178}},
			{{0.41648278,0.020635994,0.0001408746,-2.949217,1.269272,-0.4005571,-0.48702407,-2.7975914,-0.0051687425,4.0230184,1.4867262,-0.5299626,-1.7601446,-0.4854966,-1.4244132,-0.1340769,-2.281207,1.0099398,-0.19044183,-0.442792,-0.33990976,-0.67229617}},
			{{-0.08086183,1.0101848,-0.0036841081,0.33665177,0.7217579,-0.948905,-0.5506217,-0.0015539193,0.0025844814,0.11785893,0.14773005,-0.8869999,-0.12010947,-0.81196815,-0.7447828,0.4134706,0.12847324,3.26967,-2.8031163,-0.3534793,-1.0949585,-1.3697509}},
			{{-0.4553555,0.5995594,0.0035571926,-0.41876382,0.1708512,-0.071523614,0.322246,-0.87803537,-0.0010665256,0.09058997,-0.28657869,0.9496607,-1.3522187,0.030881694,-0.32511583,-0.94918746,-5.5579357,0.8342269,-0.21004999,0.21167852,-0.47350532,0.33240685}},
			{{-0.037321754,-1.2563499,0.00052554085,-0.0013172006,1.1199094,-0.55886304,-0.897009,-1.1138209,0.0011422096,0.105956875,0.4574809,0.4864547,-2.3189034,-0.82144195,0.46093637,-0.483153,-0.19652523,1.253555,-0.13238993,-0.18714066,0.75532275,-0.27349362}},
			{{0.25216377,0.19033481,0.0021963327,-0.48061815,0.3607366,1.9872278,0.34027526,-0.5035591,-0.0007134166,-0.10156003,0.39320844,-0.36573347,-1.8289216,0.10909409,0.23273177,-0.3981694,-0.06198096,1.1179167,-0.71849495,-1.1514305,-1.3272032,0.02051182}},
			{{-3.3607883,-1.0383563,-0.00053929066,0.61846435,-1.0220517,0.6588143,-0.21650289,-0.2945861,-0.0030319197,-0.20489651,0.7326975,-0.45024788,-0.09479204,-0.88138956,0.13501821,-0.5591492,-0.00874973,5.3452578,-1.5601301,-0.21300443,-0.2652403,-1.9585372}},
			{{-2.3774788,-1.701845,-0.0031932527,0.00012293053,-0.9612059,2.6650996,-0.6581098,-0.6429335,-0.0004494896,2.815552,2.6501596,0.37344953,-0.61542076,-0.85545903,-1.4965812,-1.647289,-4.591514,-1.5660977,-0.19236022,-0.09125853,2.4113991,-1.2033998}}
    }};

    h2_out = {{
			{{-2.6414363,-0.68368804,-1.1196954,-0.7715738,1.1600193,-6.247414,-1.092166,-1.2768879,-3.0917857,-3.3020318,2.1741793,-0.97340477,1.0695753,-2.214433,-3.0801685,0.1665879,0.9023257,-8.175159,-1.2240009,-0.0193082}},
			{{-0.8106161,-0.3978061,-0.9563585,0.7630383,0.7010286,-5.1357784,-0.16844517,-1.5103955,-2.7265723,-2.1752703,0.81256044,-1.1152962,0.8906716,-0.43499416,-5.29144,-0.6680187,-0.126858,-6.184265,-2.5993528,1.4044918}},
			{{-0.8542198,0.09509764,0.424555,0.3614972,-0.47969818,-3.4970682,-0.5988248,-1.0690109,-2.9539888,-1.5797642,-1.2275337,-0.4240901,0.1441861,0.9681961,-6.365782,-0.4963067,-0.22898637,-1.9763159,-2.9015913,2.0245404}},
			{{-0.085431196,0.26288575,0.12748368,-0.15430246,-1.642774,-0.8402461,-0.62175024,-0.35826248,-1.1727971,-1.1574857,-1.692139,-0.5757525,-0.8892365,0.98309755,-8.0438385,0.5934824,0.25806582,-0.39172086,-1.4503155,1.4123898}},
			{{0.43789864,-0.042639878,0.16536464,0.09029701,-2.8131223,0.7036518,0.10458443,-0.2609097,-0.57263494,-1.6732799,-1.982602,-3.5881941,-3.7358735,0.5393857,-3.7598362,-0.4944821,1.1214577,0.43407938,-2.4446602,2.121863}},
			{{0.5499495,-0.6358282,0.7713348,0.10060869,-2.5821018,1.2933788,0.5038295,-0.37940875,-1.0011652,-2.0893407,1.1837856,-4.0847616,-5.9972105,-0.18829967,-3.343674,-0.24343644,4.389921,0.38108507,-2.065004,0.14197712}},
			{{-0.7515555,-0.0630689,-0.05330817,-0.7951865,-4.2910256,0.59969664,-0.37575597,-1.2335993,-0.6548385,-3.2622569,3.7408154,-3.1080074,-6.7465286,-2.4769356,-1.823174,0.92690045,6.7493095,0.06785023,-1.2592863,-1.143644}},
			{{-4.8403187,-0.88742673,-1.6963612,-1.2022666,1.0588771,-4.3302193,-1.9075447,0.36321682,-2.647714,-1.9401212,1.4053952,-0.6579402,0.79406005,-4.5674133,-0.66112465,-0.096500084,1.7779188e-05,-7.4064775,0.25055572,0.15374136}},
			{{-2.5020869,0.18078426,-1.0129547,0.6580854,0.98739696,-3.3978446,-0.8059387,0.50571895,-2.2641978,0.060126204,0.59634185,-0.9210571,0.7192389,-1.4990485,-3.0084612,-0.5312491,-0.6441128,-5.221673,-0.44178632,0.3047997}},
			{{-1.329589,0.59216577,-0.059746534,0.7508359,-0.371768,-2.5279312,-0.969889,0.92929214,-2.4368012,-0.114238806,-0.46665978,-0.8629417,0.43310866,0.8303751,-3.748695,-0.6002977,-0.5504373,-2.126056,-0.8252146,0.8650701}},
			{{-0.1306426,0.55213356,0.38343894,0.35782456,-2.0280957,-0.6244977,-0.35846046,0.96349627,-1.8028696,0.71130115,-0.9795866,-1.6739936,-0.06661074,0.8268226,-3.8082626,0.08115558,-0.14200687,-0.1410549,-0.96494853,1.2000611}},
			{{0.6403507,-0.23514464,0.61861986,-0.15554775,-2.5146234,0.6080933,-0.5758557,1.0652491,-0.5802971,-0.2958643,-0.1425567,-3.252852,-2.0326664,0.5173975,-2.6022182,-0.58064204,0.8722813,0.5142089,-0.6564273,0.758031}},
			{{0.73922855,-1.0022196,0.9208604,-1.1708444,-3.448816,0.783034,-0.50515723,0.84770095,-0.29092366,0.06890429,1.8320652,-3.279846,-3.7333796,-1.2037051,-1.7348247,-0.38194346,3.3423014,0.5875949,-0.5211399,-0.39279568}},
			{{-0.2508028,-1.1082386,-0.11665612,-2.8697855,-4.4883175,0.3297215,-1.6556901,0.56571496,-0.10405924,-1.936027,2.9528935,-1.9460758,-5.5344157,-5.301009,0.20070727,0.60281175,5.305204,-0.00962971,0.101848304,-0.65710586}},
			{{-5.840837,0.16809368,-2.0994334,-0.9698862,1.18835,-3.1717613,-0.7896768,0.8096134,-2.3382065,-1.5299863,0.8209552,-0.17086369,0.24359772,-6.5922475,0.2115929,-0.021773718,0.27410108,-4.3866687,0.88626146,-0.51034254}},
			{{-3.509294,0.8569552,-1.7780286,0.86590487,1.1273628,-2.563737,-1.0771432,0.6024512,-1.992687,-0.13997461,0.2223389,-0.08335471,0.295928,-2.4970605,-0.97480863,-0.3890083,-0.4177668,-3.5336282,0.7984487,-0.37625003}},
			{{-1.7281775,0.88004756,-0.6670873,0.8018833,0.056408077,-1.3216465,-0.69383156,0.50846493,-2.0843592,0.2918244,-0.28003177,-0.5965295,0.49965644,0.38943473,-1.3875864,-0.44947937,-0.7722167,-1.0103192,0.5756552,-0.09507111}},
			{{-0.13300608,0.29452115,-0.016171364,0.28929898,-1.3963703,-0.010929123,-0.6872744,0.56650084,-1.212424,0.4707357,-0.62810063,-1.3245301,0.6172222,0.6692574,-1.1254299,0.26250836,-0.31871456,0.22825797,0.3601394,0.29577798}},
			{{0.64081347,-0.48800996,0.76481175,-0.6733164,-2.8686943,0.2837987,-0.55783486,0.47329706,-0.03517055,0.20010528,0.17352533,-2.4456584,-0.5199624,0.3582335,-0.7189519,-0.5387684,0.14044416,0.5961689,0.37059525,-0.10489826}},
			{{0.9404116,-1.4357281,0.97898185,-2.736682,-3.630035,0.20071396,-1.0435598,0.61357605,1.0352514,-0.29896486,0.88922095,-2.7991445,-1.7346597,-2.6449685,-0.26493263,-0.6474723,1.5556091,0.45440146,0.7230306,-0.48394263}},
			{{-0.34024188,-1.7400346,0.13564134,-4.4720864,-4.3519287,-0.405774,-1.3314229,0.9099313,1.4746203,-1.6468713,1.3670052,-2.908148,-2.7963135,-7.180831,1.0119694,0.21569806,3.5435245,-0.14672993,0.8967222,-0.5461023}},
			{{-7.4421124,0.71393085,-2.1827214,-0.6084747,1.1785718,-0.98764855,-1.0108408,0.49086538,-1.1351779,-0.73652935,-0.8113413,0.22935672,-0.7839158,-8.155677,0.48634118,1.2982881,1.0632153,-2.5609725,0.9990986,-0.7183124}},
			{{-4.7898397,1.2236979,-2.4929593,0.87443554,1.1781129,-0.7503405,-0.6837426,-0.17845464,-1.2838304,0.9749742,-0.70046514,0.5217483,-0.33600652,-3.2234857,-0.2073127,0.34581318,0.16496447,-1.604959,1.0141919,-0.8259367}},
			{{-1.8919818,0.84083,-1.6597935,0.61497056,0.44128036,-0.1534826,-1.1037604,-0.25757438,-1.0930558,1.0342534,-0.67123544,0.22609463,0.123394355,0.08350433,-0.28590178,0.50773895,-0.62627727,-0.35623467,0.71256113,-0.8217632}},
			{{-0.12758188,-0.288445,-0.65123516,-0.08679927,-0.59223205,0.13463032,-0.92002624,-0.5710538,-0.36043608,2.163244,-0.6556836,-0.6035838,0.67417336,0.5912629,-0.044263303,1.1662861,-0.42817712,0.30839896,0.6565442,-0.51699996}},
			{{0.5659693,-1.3574061,0.51673025,-1.5269829,-2.0367787,0.0094283465,-1.2655697,-0.4265038,0.85629857,0.8441468,-0.3276098,-1.3202579,0.39128903,0.092243865,0.3693389,0.1522486,-0.5163033,0.41296402,0.4438828,-0.60207504}},
			{{0.84977394,-2.0471008,0.9800095,-4.453201,-2.9578164,-0.5009841,-1.1628346,-0.053113423,1.706398,0.8981278,-0.13544862,-2.034675,-0.04425777,-3.4620197,0.6272227,-0.15971303,0.35009587,-0.13454719,0.65209645,-0.6376581}},
			{{-0.626436,-2.3552592,0.8421815,-5.8323016,-2.7458231,-1.5101379,-1.0191592,0.35578957,1.7975199,-0.75131136,-0.2613312,-2.4201567,-0.6626193,-7.9519734,1.0838455,0.9517442,1.026892,-0.72979814,0.8427456,-0.5666886}},
			{{-7.8244376,1.4611768,-4.2202864,-0.001519672,0.67996436,0.6198394,-2.0980585,-0.43496764,-0.105037585,-1.2311276,-0.83813626,0.31190407,-2.941821,-7.7646885,1.0329555,0.5291396,2.2137659,-1.2502499,0.59731317,-0.9860545}},
			{{-4.3632574,1.245896,-3.399062,0.85163546,1.2441603,0.66578656,-1.2039316,-1.3771125,-0.77990144,0.038394395,-1.0335674,0.98607033,-2.0546663,-2.7398858,0.2012889,-0.07252326,1.3014436,-0.40116638,0.6398942,-1.1574873}},
			{{-1.959476,0.5681222,-2.666477,0.37999117,0.745705,0.5669037,-0.19084786,-1.8237191,-0.31085438,0.46853003,-0.89989996,0.86692584,-0.56454754,0.06594826,0.22117086,-0.05327149,0.1295509,0.33374977,0.104050204,-1.0008768}},
			{{-0.07370735,-0.91064143,-1.4292265,-0.7013635,0.10731659,0.08944648,-0.18992613,-1.9842398,0.2659986,0.684632,-0.50878465,0.4491519,0.3729428,0.48906928,0.45533267,0.5504791,-0.21068436,0.5884115,-0.076693244,-0.70337284}},
			{{0.6273839,-2.165834,-0.07479455,-2.4746227,-0.9994559,-0.8621437,-0.59815896,-1.7081401,1.1920787,0.12793988,-0.7565324,-0.037682608,0.6564268,0.06715207,0.5394806,-0.5341502,-0.5861941,0.3421573,0.030740224,-0.5741897}},
			{{0.9510985,-3.068486,0.74570227,-4.9196525,-1.9049222,-1.7422097,-1.670508,-1.1994368,1.6555521,-0.20241092,-0.8741628,-0.55982476,0.6342135,-2.9864945,0.85133785,-0.6971883,-0.06816773,-0.7264575,0.3991819,-0.60645515}},
			{{-0.28973252,-3.4472065,1.1970062,-8.0463085,-1.2872075,-2.8239107,-2.105392,-0.72319037,1.3521246,-1.3358079,-0.7109603,-0.9763768,0.41562194,-7.831822,1.3128842,-0.01648461,0.47690752,-1.5474694,0.8041615,-0.5852979}},
			{{-5.6663795,1.1496835,-4.932216,0.001606401,0.5098662,1.7858369,-2.3421612,-1.4741671,-0.101769954,-1.7451446,-0.5151991,0.30842322,-4.158617,-4.974341,0.9369379,0.72874755,4.9442363,-0.8219764,-0.14973296,-1.1658492}},
			{{-3.0784698,0.93096733,-4.118883,0.75334936,1.072467,1.3816186,-0.39555192,-2.9815454,-0.8922786,0.29570103,-1.0212467,0.82522,-3.0287073,-1.6814327,0.41042298,0.020620646,3.404903,0.09169269,-0.752806,-1.0027943}},
			{{-1.4907626,-0.081361406,-2.9197962,0.25370812,0.8549328,0.7158262,0.2734813,-2.9043784,-0.10285762,-0.20120125,-1.2105886,0.9428641,-2.3193946,0.26274166,0.43118098,-0.1675796,0.97343236,0.41603824,-1.6295123,-0.89578193}},
			{{-0.09450189,-1.4102553,-2.071385,-1.2310426,0.5505449,-0.64650023,0.4698889,-1.8589002,0.57028943,0.47120348,-0.68522733,0.57737464,-0.47801962,0.5958337,0.5170326,0.24634416,-0.07362497,0.40164608,-2.6521153,-0.44387203}},
			{{0.7849286,-3.1305575,-0.8354933,-2.8650515,-0.34808108,-2.291729,-0.2781976,-2.4090505,1.003737,-0.42795232,-1.0806708,0.7767532,0.5279402,0.34997424,0.33806744,-0.6301131,-0.12909903,-0.19006361,-1.7304564,-0.3699316}},
			{{1.0052434,-4.589901,-0.26943037,-3.5273592,-1.366593,-2.9694176,-1.2182146,-2.5451138,1.0908437,-0.18797511,-1.3780117,0.3938984,1.2318377,-1.8265923,0.6286299,-0.5294772,0.41070247,-2.674215,-0.6772121,-0.017203448}},
			{{-0.06606736,-4.8953724,-0.022961317,-6.6995215,-1.4274411,-4.231092,-2.8345547,-1.3088537,0.76085436,-2.2066457,-0.6507047,-0.5099953,1.3110237,-5.0510545,1.7992226,-0.042832304,0.8430777,-4.3980637,-0.20738472,-0.33583054}},
			{{-2.7983022,1.2144014,-4.783736,-0.08512082,0.21577647,2.210153,-0.83132994,-3.1782928,-0.4693693,-3.0640576,-0.9419943,-1.3155601,-4.5758224,-2.5367649,-0.14101045,0.85589963,6.186969,-0.6079314,-1.0875717,-1.2350416}},
			{{-1.7378333,0.15207584,-3.8103297,0.6099594,1.1759906,1.5106922,0.9264759,-4.09099,-0.6925538,-2.042631,-1.4228415,-0.18617123,-5.1996408,-0.4630422,-1.2038814,0.06301904,4.292963,0.1556885,-1.7136455,-0.8283613}},
			{{-1.0701411,0.20460561,-2.971523,-0.008482786,0.6950537,0.047720112,1.5017551,-1.3730263,-0.058139,-2.019644,-0.88088423,0.007273566,-4.0637307,0.55657816,-0.42135996,-0.0017468084,0.8033721,0.44199097,-5.0078726,-1.4193013}},
			{{0.035524946,-1.2510242,-1.7388397,-0.7194827,0.3349424,-0.73409337,0.052335493,-1.8069679,0.45514825,-1.2498721,-1.0219088,-0.07507776,-0.42966717,0.85545266,-0.3410577,0.50450546,-0.06667062,0.050032094,-7.0518875,-0.5614259}},
			{{0.6869871,-3.2802386,-0.47722855,-2.4367123,-0.028475905,-4.080423,1.1009493,-1.9446566,0.6293664,-2.030424,-0.6028131,0.33392546,0.216004,0.730571,-0.8318306,-0.6247563,-0.2756689,-0.7031606,-5.1913366,-0.5277505}},
			{{0.9882152,-5.455882,-0.7416215,-1.8433341,-0.902339,-4.3979034,0.4399991,-3.5909545,0.6711501,-2.3394074,-1.4928046,0.50804925,1.3526456,-0.41807827,-0.91207427,-0.5432237,0.8343793,-4.645868,-2.4967072,0.17624344}},
			{{-0.28488642,-5.6511297,-0.30758846,-2.9442174,-1.0775825,-4.7164903,-1.1327212,-2.867036,0.11273204,-3.5000985,-1.2441338,0.091853395,1.5155939,-2.2824025,0.37044567,0.39493206,1.263753,-7.343756,-1.0397441,-0.0807938}}
    }};

    b1 = {
			-0.11087356, 0.11896728, -0.23086089, -0.08732864, 0.45063308, 0.029116552, -0.33972573, -0.09337084, -0.30379713, 0.79959846, 0.6124893, 0.30832857, -0.3621259, -0.22258614, 0.452873, -0.104422525, 1.1591879, 0.36911687, -0.12929708, -0.33745185, -0.061687067, -0.017868023
    };

    b2 = {
			0.3761198, -0.001633271, -0.36021474, 0.33602, 0.49981514, 0.34358954, 0.2006933, -1.5804679, -0.088692285, -0.66359615, 0.8525075, 0.30044734, 0.696566, -0.05696556, 0.3419808, 2.391765, 1.9333972, 1.7604089, 0.3491689, 0.45975465
    };

    bout = {
			-0.9322295, -0.8319202, -0.33890012, 0.3726229, -0.48463383, -1.0237616, -1.0719318, -1.3250556, -0.4514574, -0.30547464, 0.5619598, -0.37563905, -0.6749312, -1.4217302, -0.7715764, -0.4081692, 0.4713198, 0.9331147, 0.3633525, -0.6286224, -1.0407205, -0.1458977, 0.4464512, 1.0413654, 2.0640643, 0.8008865, 0.15067342, -0.2383123, -1.2180129, -0.5716048, 0.4032022, 0.97420686, 0.2712851, -0.7244596, -1.1537776, -1.597742, -0.74772483, -0.43877637, 0.5781913, -0.46134236, -0.7516122, -1.6579332, -1.2619662, -1.0549468, -0.5167837, 0.029882895, -0.602274, -1.0658728, -1.12746
    };

    BN_gamma_in = {
			0.057639718, 0.67415535, 0.15779614, 0.32943404, 0.38675815, 0.18923873, 0.78430665, 0.4502687, 0.61734027
    };

    BN_gamma_1 = {
			4.0772996, 5.499209, 0.13400486, 4.222234, 3.2444541, 3.8196187, 8.071683, 1.8944999, 0.5058664, 3.4248307, 2.8809288, 2.6133761, -3.6890736, 17.269135, 3.0607805, 5.435639, 0.67227066, 3.114117, 2.7360542, 7.641779, 8.881477, 7.0148892
    };

    BN_gamma_2 = {
			0.15920137, 0.2525782, 0.22945441, 0.15102147, 0.17156608, 0.18297341, 0.18170251, 0.14430925, 0.21034466, 1.4870164, 0.21300974, 0.15632561, 0.16720052, 0.12625639, 0.14418027, 0.26980808, -0.13676198, 0.127032, 0.12907723, 0.21448918
    };

    BN_beta_1 = {
			-0.32388026, -0.058439184, -0.00045901598, -0.1680844, -1.2232924, -0.12045191, -0.022219565, -0.8562941, 0.0005444882, -1.7062914, -1.1490017, -0.5753182, 0.15119809, -0.019047718, -0.0021898844, -0.051947102, -0.1200477, -0.7567054, -0.7230986, -0.023631722, -0.06765934, -0.0015201438
    };

    BN_beta_2 = {
			-0.07691966, -0.074238166, -0.0666633, -0.0718895, -0.089213535, -0.102995574, -0.21600102, -0.063759714, -0.08673896, -0.08091984, -0.1555077, -0.10705561, -0.10734169, -0.057452925, -0.0581775, -0.63441163, 0.22163211, -0.19138467, -0.08486706, -0.27262318
    };
    
    mean = {
      50270.00278812122,32814.100678744784,46891.28361361622,41621.16404294983,21791.178170341536,41337.433591779336,47095.434124997926,32595.08412435691,49923.88279260183
    };

    stdev = {
      192277.57002833168,148902.63299216705,178409.20944839143,170471.77005005843,129693.55635497085,169053.99203804682,178924.76577600988,148103.8200182561,189676.17710192283
    };
  }

  else { // QP=22 and Default
    
    embs0 = {{
			{{0.37904647,-0.01781869,-0.1587019,0.21311721}},
			{{0.011720581,-0.001942264,-0.0044361097,-0.028833676}},
			{{-0.0057522845,0.0040410813,0.004354478,0.0126301935}},
			{{0.036107875,0.051984902,-0.026074553,0.07915544}},
			{{-0.018290307,-0.013712987,-0.010077563,0.06746815}},
			{{0.40660998,0.12210838,-0.026242748,0.40944153}},
			{{0.1296971,0.0011974304,-0.005124573,0.24341783}},
			{{0.8559168,-0.8605395,0.661513,0.6341938}}
    }};

    embs1 = {{
			{{0.3003882,0.3718846,0.2591062,-0.36687276}},
			{{9.258145e-05,0.031978715,0.008993433,-0.0048579206}},
			{{-0.0008670334,-0.018487265,0.002277616,0.003942474}},
			{{0.007830077,0.018933335,-0.10097268,0.00066456245}},
			{{0.00536333,-0.08616341,-0.0043545696,-0.007031202}},
			{{-0.10768657,-0.027285565,-0.086264595,-0.010659516}},
			{{0.014833452,-0.26297078,0.010003578,-0.2543707}},
			{{1.7079602,-0.086003706,0.025469214,-0.06030395}}
    }};

    in_h1 = {{
			{{-0.047067836,0.00060473074,-0.016790433,0.13447326,0.043566607,-0.09936547,0.065153524,0.028157016,-0.41205508,2.2089248,0.1015443,1.3115172,0.6847078,-2.578088,3.6236484,6.662539,-3.990674}},
			{{-0.09410552,-0.00643639,0.003933827,0.34812835,-0.0484243,-0.3247816,0.037682302,0.039762035,0.08506887,2.4209018,0.14719483,-0.22206697,-8.896864,-0.07039002,0.7625629,1.4401935,0.14249536}},
			{{-0.17005706,0.08312598,0.022291558,0.1822236,-0.07249823,-0.06522398,-0.09123081,0.041033093,0.3556423,-8.849278,0.2693465,-0.43656006,0.29828635,0.13283429,0.5081913,6.249631,-0.24362826}},
			{{-0.29640985,-0.061992142,0.0074714185,0.32904395,0.012778041,-0.4408897,-0.0049013942,0.22438881,0.47784498,-6.5559278,2.8117993,-1.161123,3.0098467,-2.1483493,0.84943634,-3.590887,0.51623935}},
			{{-0.09843467,0.0113034295,0.008366183,0.103992835,-0.0116396295,-0.07120454,0.13743411,0.056038443,-0.80095345,-0.12836643,2.6516397,5.2004747,-0.5983463,-4.7725945,-1.2672707,0.9576926,0.88479203}},
			{{-0.01944108,0.015755482,0.0005626979,0.043606583,-0.010251126,-0.03751873,-0.038132932,0.0037871818,-0.01596049,2.5780969,-0.045112126,0.26521328,1.9437144,0.1159501,-0.5576609,-10.22673,-0.027542302}},
			{{0.0005945412,-0.2141972,-0.0015971273,0.41712177,0.24036336,-0.33022204,-0.14122178,0.07227308,0.26531965,-2.668978,0.38536817,1.4834238,-8.350147,1.1300457,2.4050527,0.8674292,-0.41690138}},
			{{0.0028060582,-0.027572751,-0.0762589,-0.021863718,-0.016450526,0.05078308,-0.07769212,-0.032404352,-5.219557,1.4358727,3.583515,2.1540937,0.5984592,0.19081166,2.7801924,0.026102573,0.22025123}},
			{{-0.005548537,-0.046461802,-0.007346251,0.013978998,-0.04349047,0.0005923164,-0.073523805,-0.0029683558,-0.036199074,0.004008398,1.3859227,1.475282,1.7377527,1.5178901,5.595775,-3.7434506,-4.8739486}},
			{{-1.0424988,-0.05691055,-0.05490166,1.4715605,-0.09585042,-1.4114484,-0.6165178,0.53978634,-0.39658585,6.3593674,-1.7007676,2.0151975,-7.2006173,1.1585648,2.7474406,3.6082063,-0.112503216}},
			{{-0.035292894,-0.0033094503,-0.037648343,0.11140201,-0.002534666,-0.08984764,-0.14753486,-0.0016432136,-0.29853514,1.517585,-0.5119544,-7.4160156,2.0814946,1.1399804,-1.4339759,2.3609135,-0.30767873}},
			{{-0.19400766,-0.1367185,0.04873346,0.6396741,0.11106868,-0.4944818,-0.52810913,0.17834036,-4.3585153,6.673869,1.6896118,1.4557662,-4.4497347,-2.67571,-0.3835892,3.62323,-0.42425796}},
			{{-0.062455136,0.018130438,-0.002144724,0.07118806,-0.037874382,0.033065464,-0.03478836,-0.009503744,-0.15576054,7.1378136,-0.26799947,-0.0068563716,0.17010602,-0.29180944,0.8530787,-7.994394,0.43624812}},
			{{-0.13745515,-0.07978959,-0.08618849,0.17286138,0.0030144707,-0.055778097,-0.13135648,0.021297721,1.3240541,7.1711936,1.1566889,-3.70392,-2.8211713,1.1875126,0.3726625,3.2062373,-0.035194937}},
			{{0.03272595,-0.10059332,-0.043937564,-0.05760976,0.03737169,0.07855737,0.037309602,0.027249666,-0.42526823,1.2794775,3.5196824,0.22326079,-2.776337,-2.1140132,6.3767715,8.358113,-0.2066255}},
			{{0.033458456,-0.04939891,-0.035177603,-0.03378419,0.07099395,0.077215984,-0.13243774,-0.022237832,1.6076549,1.5284094,-6.272796,0.8235071,-0.5365892,0.06840741,-7.6184144,1.670421,2.1885822}},
			{{-0.07470828,-0.044603452,0.031823568,0.122129895,-0.0015993085,-0.08920993,0.08138028,0.071204595,0.07254612,0.3592225,2.5577695,1.1194906,0.58646256,-0.995776,-11.929429,3.0237892,0.20490058}},
			{{-0.019562284,-0.008226155,0.0024846855,0.067233816,-0.0042155865,-0.062641405,-0.04365828,0.006231723,-0.34906515,-11.360017,-0.92180884,0.85277057,2.0636344,0.8291485,-0.74351645,1.5457357,-0.24561979}},
			{{0.2680015,-0.074911155,-0.09128242,-0.34778753,0.057509277,0.3760303,0.08317884,-0.2089287,1.5848706,-3.1020153,-2.5726788,4.870754,-1.941553,1.6654176,3.0678308,-6.2325425,0.6887489}},
			{{-0.10193606,-0.06498254,-0.0049233674,0.1632881,0.009981696,-0.124797456,0.020957822,0.086094536,0.4740278,0.5457903,-9.893947,-0.582809,0.9844723,0.8657731,3.7029958,1.808383,-0.3010412}},
			{{-0.006308018,0.017547833,-0.02434307,0.038779646,-0.04225504,-0.03853971,-0.12982863,-0.020716619,0.049217585,-0.19358887,0.17583264,4.147533,0.6758906,-5.90894,1.0894783,-0.9877269,0.18933305}},
			{{-0.132521,0.01266886,0.15184493,0.16404891,-0.018670028,-0.19868827,0.41304147,0.15589541,0.083227836,1.5138909,-2.0765538,-6.2603335,1.1163636,1.4688787,6.0006213,0.70072305,-0.18344855}}
    }};

    h1_h2 = {{
			{{1.484737,0.38221392,-0.5062888,-2.679011,-0.23941754,-1.6472212,1.036841,-1.9824778,-0.27632102,0.71383303,-0.927326,2.4132404,-0.6208786,2.2026436,-2.5686727,1.3267823,-0.20536697,-7.615569,-6.405835,0.88810945,3.4900477,-0.68546414}},
			{{-1.9681876,0.34551227,-1.7073663,-3.273184,1.9146872,0.43054676,-1.0410669,-6.5946903,-1.8131392,2.440154,-1.709575,1.3971931,2.0330534,-3.7438648,-0.6575112,1.897415,-3.2064922,-3.6857514,-4.568186,-2.2347598,-0.1545393,-1.8298346}},
			{{-0.96486074,3.061494,1.2049137,-1.8848773,0.59077924,0.36280346,-7.1795917,-0.15689676,-0.32555094,-1.557607,-0.8107554,-1.5555165,1.8368577,-0.9485171,-2.561821,1.3142035,-2.6163185,1.49888,-4.5849705,1.9815359,1.111992,-0.6691594}},
			{{-0.2502299,-0.60518354,-1.1052732,-0.20387076,2.6350634,0.73995715,-0.77787304,-2.123439,-0.016991654,1.952518,-5.6019354,0.8330055,2.3347607,-1.8912559,-0.7954177,0.15155907,-2.116532,3.4715734,-1.8581854,2.9813378,0.45369846,-5.3030267}},
			{{-2.2226126,-0.31083596,2.4352708,-1.5055947,-1.0116286,-0.1623096,0.26820087,2.9504998,-0.857521,3.1344774,-6.6263256,3.228977,-2.8696673,-6.872879,-2.2711363,1.6031072,-2.526059,-1.5743138,-0.13483533,-0.2926919,0.66295165,-2.4482281}},
			{{0.011231608,-0.576602,1.6823455,-2.9255567,3.9562438,-4.112069,-2.0513706,-0.28038275,0.74142057,0.59021145,-1.179025,1.7035521,-5.091157,2.0769832,-2.5142655,-7.0600247,-0.56572384,0.74781555,-3.3231592,2.4815335,-0.4831128,-2.0350657}},
			{{-3.5417874,-0.7456593,0.810957,-1.3966873,-8.903815,0.4000807,-0.4624034,-0.48383594,-8.343733,1.4270546,-0.86161083,2.5757663,-1.9451711,3.7007701,-3.1980863,-1.1962543,-1.3853072,0.59742993,-0.61946183,3.4206011,-6.7058067,-0.271886}},
			{{-0.09611357,1.0032518,-0.70587313,-3.5631747,-0.98925614,-0.76912045,-0.45007625,0.11525052,0.7534678,3.1032221,-2.869127,0.9212516,0.77732915,-2.5634618,0.723956,-9.355745,-0.74037576,-1.1208711,-1.2382029,0.5418655,-1.739339,-2.8036008}},
			{{-2.0419245,0.5690701,2.7417672,-4.3744473,0.43289164,-10.361759,-0.2926924,0.38188115,-2.6124232,2.431818,0.34788108,2.2814562,-6.039794,1.0651411,-1.5416011,-1.4535667,-2.0237541,-3.2511275,-1.2161912,1.9337753,0.74286747,-0.123284504}},
			{{-1.5413309,0.34796104,0.9285424,-2.3951738,-0.53125185,0.3038007,-4.3759804,-0.55150574,-0.27069688,3.0666168,-3.4503644,-1.4289367,0.52843994,-0.5344137,0.106773645,-0.8556846,-1.9907371,-1.2341933,0.8464393,1.5036677,-1.6155727,-2.8856895}},
			{{-0.956388,5.1413183,-0.39366797,-2.8740208,1.114392,-2.4442236,-0.12178814,-0.80769604,-0.854416,2.027227,0.42834723,-2.3127894,-1.0031779,2.3168697,-0.7311627,0.26582897,-0.8704304,-4.6913795,-0.9905084,0.38520348,0.90272623,-1.0229977}},
			{{-1.4578962,-0.42094398,-1.7704108,-4.0189614,-2.9476695,0.54014164,-0.5669602,0.29221606,-0.11533156,2.9572716,0.21062331,2.7359328,3.07216,-3.627339,0.1686471,-6.7392807,-6.136468,-3.7434783,-1.3014346,-0.12280415,-5.1031837,2.4882}},
			{{1.4872984,0.20254098,-3.2162316,-2.3563945,0.9397974,-1.5807891,-0.8062212,-1.8442636,0.6118058,3.1108813,0.8151553,-0.16170715,2.378546,2.9771633,-2.8425412,-2.084058,-0.5064654,-4.972103,-1.5022393,3.288163,0.4061742,-0.36836988}},
			{{-6.7834406,0.67858946,1.6089835,-1.7136228,-3.9465604,1.0328768,-0.4563865,0.035976738,-1.0606756,2.2417684,-1.4241118,2.4893842,-0.36760372,0.90583235,-0.38766444,-0.30010158,-0.06175555,0.57858783,0.19556835,2.5234084,-14.653156,-0.19098727}},
			{{-0.00889111,0.8159031,0.8608146,-2.2318015,0.5858181,-8.108376,-0.24567316,-1.8700444,-0.98036474,2.6532009,-5.057581,1.0210489,-3.9651036,-1.5602045,-0.11707589,1.5135571,-0.904508,2.4297588,-0.9434564,-0.12710907,0.3124555,-2.938003}},
			{{-5.8975315,1.3944674,-2.283561,-4.2727895,1.6561921,-0.42057684,0.25396958,-1.044249,-4.784784,2.596431,-1.5514351,0.9607635,0.011722805,1.6577495,-4.327609,0.3565597,0.5077285,-2.0572243,-0.56761664,0.1933484,-0.6806322,-2.4344687}},
			{{0.20301668,2.3379052,-1.8430178,-3.1326046,0.41232252,-5.738923,0.015535561,0.23460655,-0.24556603,2.6265337,-0.6285188,1.311232,-2.299737,1.3079187,-0.31090558,-0.9267324,-0.7069888,-7.221933,-0.8863634,0.9967942,0.47556844,0.30082327}},
			{{1.136335,0.36779964,-0.5627881,-2.757499,-0.0530439,-1.9719089,-2.002744,-2.1222181,4.16268,2.3426785,-2.9692926,0.6276512,0.39050078,-1.3919578,-6.3674774,-0.1505902,3.223597,-2.5471432,-2.7002764,2.8818972,-0.96949327,-3.3231225}},
			{{0.0467526,8.697631,0.89344084,-2.1037874,0.17477936,0.48976713,-2.4896479,0.15139104,-0.26392654,-2.6670933,0.86268306,0.028620536,0.28787214,-3.6964252,-2.758169,-0.48441434,-0.031213343,-1.8197383,-1.736067,0.3666933,0.46116233,-0.80428034}},
			{{-1.1952755,-0.10673285,1.352894,-3.2715328,-0.21989071,-3.7475712,-2.7671986,-6.663387,-2.3325548,1.2354503,0.8092663,-1.5950806,-4.112644,3.2199836,-3.6999476,0.37798426,0.059132963,0.74474907,-3.6142745,-0.7757617,-0.07441104,-1.4710759}}
    }};

    h2_out = {{
			{{0.056174293,-12.045879,-2.6106658,-4.126843,1.8362547,-0.27538705,-0.8829228,0.7920943,0.5364229,-7.1068726,-6.4056582,0.2985545,4.3832207,-0.8345635,-3.764038,-2.424068,-0.20158838,-0.670196,-1.1532036,-4.433748}},
			{{-3.265911,-8.894824,-2.7918632,-0.7799587,2.5643477,0.06240793,1.4067178,1.3845798,0.97144914,-2.7223985,-9.708855,-0.021094078,5.0357213,0.32345653,-3.6339412,-6.2145677,-1.724837,0.14534059,-0.9257553,-5.76462}},
			{{-2.8410404,-7.201769,0.87666154,-0.34618315,2.1416209,1.0633771,0.77194,1.1648568,0.24175179,0.11009938,-10.28639,-1.9505023,6.8730125,0.80605793,-0.9107584,-5.7158556,-5.8120756,-1.4069945,-1.2696723,-3.0937123}},
			{{-2.638729,-4.000154,1.6205403,-0.55039483,0.72333115,-0.24722333,-0.77793556,-0.16686407,-0.17252272,0.4630403,-7.564607,-3.3799136,7.8651595,0.33316788,0.6487639,-1.0392842,-8.476447,-1.3694843,-0.61201394,-0.5893737}},
			{{-3.4224715,-1.3581028,0.1341874,-0.22202381,2.193029,-2.4903822,-1.0788896,-3.9255102,-0.60203224,0.6442661,-3.9637923,-4.530304,7.7011285,-0.6990649,1.0009037,-0.17319763,-10.234527,-1.1099981,-1.7521524,1.2172482}},
			{{-2.5121858,1.0743223,-2.668334,-1.212964,2.1363974,-3.6842048,-0.5751989,-6.2646236,-2.3451135,-1.0393914,-1.7954,-3.9754074,6.262212,-2.861231,1.2920262,0.6958199,-6.907253,-0.94036865,-1.8544084,0.80703264}},
			{{-0.36977324,1.8816494,-2.2270463,-3.7112865,0.46632868,-3.4144356,-2.5984182,-8.642271,-2.0053349,-6.572223,-2.044709,-2.0870464,7.230839,-3.1689727,0.84791267,0.8152067,-3.867829,0.13489346,-1.7653171,0.3844747}},
			{{1.3279657,-9.687515,-4.42065,-5.7518206,0.58057123,-0.2409947,-1.0097709,0.17796907,2.5900187,-10.771791,-4.9764266,1.2503164,2.5118253,-0.2656711,-3.3804605,-2.6446762,0.9425155,-3.512097,-1.0773259,-4.686427}},
			{{-1.1005036,-8.44056,-3.5520902,-3.5869145,1.1163208,-0.088647045,1.0984331,0.74108475,2.3359492,-2.8583634,-7.5162363,0.53153425,4.3045344,0.8061447,-3.1403,-4.2695746,0.5391375,-1.6142863,-0.7853418,-2.3970187}},
			{{-2.0862076,-6.1584945,-0.6651949,-2.227565,0.99332595,0.9154777,0.80859476,1.1684397,2.092114,0.2754871,-5.163913,-1.9669263,4.1656966,0.8252563,-0.16512166,-4.417231,-2.009035,-2.2945697,-1.3344773,-0.3160737}},
			{{-1.9792707,-4.0481167,1.1201025,-1.4158849,0.6579474,1.4158856,-0.6847474,-0.021646347,1.6194057,0.8446398,-3.355562,-4.3235145,6.6226625,0.08065286,1.2945071,-1.7445328,-5.0186367,-1.5359815,-0.5508058,1.0412933}},
			{{-1.8868015,-0.09659275,-1.2096007,-0.44513822,0.997296,0.7121916,-2.057683,-3.3523738,1.1922932,0.60907423,-1.1486152,-5.4036937,5.0315547,-1.0789487,1.9442691,-0.638121,-5.3882184,-2.467553,-1.8283632,0.8436236}},
			{{-0.054355994,1.7601675,-3.898357,-0.6794879,0.73615277,-2.1157782,-2.2712014,-5.416665,0.63937694,-2.8601453,0.046092324,-4.6341133,6.27489,-3.7703917,1.8128598,0.1908193,-4.320156,-1.7191961,-1.2104979,0.14288838}},
			{{2.1482036,2.6710916,-4.9505987,-4.441915,-1.1397687,-3.9778352,-3.305083,-7.685963,0.67410207,-9.279302,0.16474666,-5.491497,2.6582603,-4.999809,1.0207818,-0.22563888,-2.031132,-1.7781674,-1.3243572,0.31155407}},
			{{0.63323283,-5.1696124,-6.352472,-4.784384,-2.4276042,-0.27068397,-0.14978208,-1.2324492,1.8080564,-14.911526,-0.7715161,1.6219456,0.7999288,-0.2072237,-2.625104,-3.6192605,1.0830547,-3.2298143,-0.41152656,-2.389487}},
			{{-0.7325489,-5.8856926,-5.801929,-3.7508285,-1.298843,-1.0240148,1.901731,-0.2255336,1.8111926,-3.548293,-1.1441022,1.485085,1.9366766,1.1652986,-3.0749176,-3.875082,0.9693183,-2.4217274,-0.97887766,0.18142393}},
			{{-1.3032577,-4.5405807,-0.76769394,-1.8707016,-0.39479136,-0.3068527,0.9658564,1.114365,1.1478592,0.35050982,-0.19203551,-0.44051978,2.2333848,1.0268918,-0.30561617,-2.8147974,0.39584365,-1.8718121,-0.89614403,1.1571105}},
			{{-1.5517836,-2.4439511,0.6663923,-1.8113185,0.054667946,0.76863116,-0.80522233,0.5827264,0.83721554,0.96063703,0.41895685,-3.0091126,2.644393,-0.04299662,0.9853101,-0.48999834,-0.18417747,-1.1112328,0.08467694,0.64567447}},
			{{-0.32503828,0.22888218,-0.8616331,0.13428943,-0.36369032,1.27473,-1.9184035,-1.4144306,0.695799,0.7849274,0.670435,-5.609431,3.2098336,-1.5752468,1.3772318,0.3859737,-0.33179647,-1.8765577,-1.0588733,-0.5322842}},
			{{2.0081887,1.6505903,-4.257031,0.5716449,-0.5142281,0.39643374,-2.0376148,-5.355885,0.8337093,-3.4504676,0.6643335,-6.1688104,3.5402052,-5.3800273,1.4515312,-0.2039381,-0.30808315,-2.1939576,-1.2027853,-1.8820561}},
			{{3.679994,1.8680689,-3.903444,-1.4294854,-2.6945734,-2.0644329,-3.4578047,-6.532139,0.86890113,-9.168893,0.53129756,-5.563114,2.654754,-6.466425,0.33194292,-0.7770948,-0.3301293,-1.7938551,-0.89608645,-1.1247725}},
			{{1.3730702,-3.4244506,-4.8510623,-5.423151,-3.1352315,-1.9382029,0.185541,-4.2624936,0.55937713,-9.459453,-0.097348794,0.5962521,0.0011578331,-0.13476229,-2.2752972,-2.3046343,0.3803087,-3.639117,1.5222433,-0.6216254}},
			{{-0.064469844,-4.1826196,-5.8680267,-3.1717176,-2.744308,-3.7704425,2.3973806,-1.5864146,0.38010475,-4.7532463,0.21526562,1.4992911,-0.02663818,0.95166105,-1.90025,-2.235197,0.49195874,-3.0290208,1.0869454,1.3595839}},
			{{-0.3686902,-2.7011309,-0.623513,-2.0289931,-1.9257,-2.394856,1.2465676,0.54934025,-0.004547143,0.39777258,0.39667916,0.5221692,0.3651976,0.7560004,0.21171272,-0.39919877,0.6664049,-1.6521171,0.73112965,0.8785593}},
			{{-1.235021,-1.0397296,0.64632255,-1.7100168,-0.8677579,-0.41019937,-1.0733997,0.8113143,-0.5518017,0.9537746,0.4165427,-1.0213146,0.47315755,-0.26249918,0.7729499,0.6024515,0.62209725,-0.91108817,2.0125644,-0.4717065}},
			{{0.31946278,0.5758292,-0.9007507,0.7348167,-1.2840489,0.8779474,-1.7648696,0.2186642,0.063295364,0.79594845,0.71091825,-3.1407442,0.575024,-2.6740265,0.6611374,0.5661339,0.46489385,-1.6205875,1.0116068,-1.7304031}},
			{{2.2202842,1.4279114,-3.9548113,1.1954882,-1.2158417,1.0259936,-1.7187445,-2.228644,0.32426393,-3.9009278,0.46370086,-5.457558,0.7717518,-5.5696507,0.44213915,-1.2586743,0.46607128,-2.0332723,0.8112394,-3.988355}},
			{{2.4551146,-0.10308873,-2.4390337,-0.7063208,-2.0100963,0.10536312,-4.5714846,-3.3320155,0.75474495,-8.321712,0.31705526,-3.1835015,0.531973,-6.0887237,-0.59619975,-2.1137502,0.2273389,-2.476631,0.6871316,-2.80641}},
			{{2.3472104,-1.8240899,-5.4317117,-6.5405855,-3.4396932,-5.300879,-0.00484325,-7.557175,-0.601816,-14.157861,0.22059947,-1.2279885,-0.1575617,0.34974337,-2.4491854,-0.43899477,0.010960414,-1.4281594,0.01691183,1.3566966}},
			{{0.9930919,-3.4876382,-6.205798,-5.71217,-2.0402915,-5.8556476,2.5189908,-4.080702,-1.7270321,-4.688359,0.40090364,0.7514791,-0.82803065,1.026948,-0.8952166,0.15148906,-0.8249712,0.01618131,-0.79785174,1.2650092}},
			{{-0.049704622,-1.8451161,-0.87565047,-1.9764442,-0.75358933,-6.9379067,0.94826597,-0.7896345,-2.0247216,0.33763105,0.63584334,1.3286849,-0.72377574,0.6873906,0.16701348,0.86508673,-0.56627417,0.34699377,-0.6992349,-0.16683125}},
			{{-1.1863906,0.582725,0.6860539,-0.592506,-0.9727642,-3.12218,-1.1836798,0.7423583,-1.628019,1.0067403,0.727155,0.44818407,-0.7146136,-0.39275712,-0.53148395,1.119985,-0.38755983,0.33570907,0.2897246,-2.7170715}},
			{{0.13446058,1.1072283,-0.80521065,1.1929061,-1.3868058,-0.08898149,-1.7320383,0.9704757,-1.1846949,0.8257988,0.42826682,-1.012161,-0.45306846,-2.5888276,-1.0932965,0.03136387,0.22153683,-0.49529144,-0.9654001,-5.197095}},
			{{1.8512207,0.97657377,-4.797819,1.3945332,-1.6269243,1.020353,-0.9537012,-0.00086633605,-1.4870049,-3.1806931,-0.09234601,-3.054287,-0.61470586,-5.7475266,-0.4493677,-3.2296307,0.4094492,-0.7338623,-1.152205,-6.0624537}},
			{{3.5464296,-1.3788482,-3.2715263,-1.4172032,-1.4827391,0.873645,-2.1496673,-0.615161,-0.010239532,-11.631689,-0.32639372,-2.269902,-0.2526173,-8.226397,-1.1665106,-4.3620677,0.525337,-1.3826793,-0.71791583,-5.209315}},
			{{3.1097674,-1.3370203,-5.6053214,-8.156066,-3.4500732,-4.1958957,-0.5718204,-10.272775,-2.5269277,-11.076375,-0.96833986,-2.4878244,-0.6694425,0.4436586,-0.884899,0.8557353,-2.0150776,2.7080154,-1.1324757,0.8624295}},
			{{0.8468632,-1.9403393,-5.1891413,-4.119899,-1.2284577,-6.0429254,2.0226336,-6.569864,-3.8031301,-4.5774198,-0.8243323,-0.9512412,-1.0968301,1.0716931,-0.23443004,1.6382523,-4.680777,2.5827947,-0.9500535,0.29881498}},
			{{-0.57814527,0.049081936,-1.8697964,-0.5106934,-1.8546035,-8.989628,0.8917926,-2.199924,-3.509456,0.24140367,-1.0612414,0.85677516,-1.140332,0.73542106,-0.56885487,1.7393688,-5.4214344,1.9834989,-1.5955552,-2.0787435}},
			{{-0.7715513,0.9325158,0.91201675,0.60826516,-0.43966118,-5.3005137,-1.3285295,0.33879817,-3.0657074,0.90317047,-1.7620269,1.0330578,-1.1659455,-0.09871141,-3.0494897,1.0436352,-4.7362814,0.13970713,-0.5605821,-4.6353965}},
			{{-0.6110594,1.0847586,-1.355237,1.460527,-1.4927077,-2.0227478,-2.1089804,1.2132635,-2.6170177,0.6245699,-2.068849,0.44097134,-1.3534745,-2.0277388,-2.5250006,-1.4792807,-2.3325038,0.6831395,-1.8389235,-7.627123}},
			{{0.9181849,-0.4270907,-3.4131722,1.4777956,-1.3467404,0.22152066,-1.042949,1.2447643,-2.8472638,-3.1110878,-2.6439471,-1.0549382,-1.4125388,-4.58619,-1.9202342,-6.0038176,-0.6863082,0.78502196,-1.5711293,-5.8459826}},
			{{3.3738408,-2.6968186,-3.2281213,-1.1682227,-2.4174304,0.4949599,-1.7577295,0.7348095,-0.73384833,-9.551719,-3.1821978,-1.1755605,-1.3394793,-7.0926304,-1.9063747,-8.148935,0.02472254,0.42806405,-1.4776857,-5.084764}},
			{{-0.15059792,-1.3829587,-3.6736887,-4.53613,-0.8213888,-7.9899707,-0.41610798,-8.470408,-3.0919921,-9.552002,-1.1013334,-1.1419607,1.15458,-0.22643705,-0.38278213,1.2530817,-4.8793516,3.4138117,-1.7169234,0.56362545}},
			{{-1.7273184,-0.79362535,-4.587401,-3.0622346,0.634862,-7.474102,1.654858,-8.394144,-5.4331784,-2.296795,-2.6478288,-2.571844,1.0401967,1.003454,0.101001,2.1035953,-7.2982273,3.8618734,-1.3037063,-1.3394803}},
			{{-2.2000022,0.5388884,-0.42291865,-0.28203717,0.5607106,-9.212281,0.8478916,-4.986123,-6.5904403,0.41649184,-2.916619,-1.1336066,1.0685284,0.6206051,-1.481258,1.9127225,-10.447768,2.2551384,-1.8127209,-2.4592366}},
			{{-1.4049144,-0.5471894,1.1014398,0.32243368,0.31977108,-5.3007617,-1.2461746,0.0782582,-4.643244,0.67173547,-5.1865783,-0.1623298,1.0099143,0.03551785,-2.1837456,0.3693095,-7.148145,-0.034401875,-1.0716182,-5.0181}},
			{{-2.2575276,-0.2129614,0.28835064,1.4189677,0.23163818,-2.6594958,-1.6556611,1.1231576,-3.658961,0.66591704,-7.7770767,0.8404767,0.3265721,-1.3528764,-2.921519,-4.070716,-5.369364,1.6043786,-1.9228659,-8.486501}},
			{{-1.9357734,-1.1790864,-2.3170056,1.5334783,0.42060864,-1.2260188,0.36034623,1.5761831,-2.1574497,-2.3163085,-8.922999,-0.24658273,0.14288864,-3.346407,-2.0899215,-9.801937,-2.1999495,2.1947575,-1.7142032,-9.110893}},
			{{0.1266955,-3.6641712,-1.9493549,-1.5969123,-0.88045174,-0.093549654,-2.689438,1.1090434,-2.1830115,-6.7050533,-6.11444,-0.8683713,-0.3311282,-4.3392005,-0.9450099,-7.2026744,-0.9335661,1.3747277,-1.8988084,-6.575792}}
    }};

    b1 = {
			-0.5171557, -0.48601475, -0.032646928, -0.11818887, 0.13604496, -0.24735592, -0.27433506, -0.24721843, -0.38653556, 0.3106132, -0.636214, -0.97055024, 0.027044347, 1.1913933, 0.5240791, 0.17991245, -0.15646927, -0.349037, 0.6881661, -0.28634188, -0.23023535, -0.09611312
    };

    b2 = {
			-0.3978039, -0.9312913, -1.5729783, 0.43066034, -0.1247284, -1.123745, -1.1466427, 0.29614735, 0.20165916, 1.4201652, 0.7826018, -0.758827, 0.16957867, 1.9610925, 1.2706187, -0.49904224, 0.648363, -1.7251387, -1.5148743, -1.0266409
    };

    bout = {
			-2.9241784, -2.0294023, -1.6380814, -1.4883354, -1.3777269, -1.2736953, -2.7803483, -3.097947, -1.054562, -0.1265513, 0.38080558, -0.01479068, -0.43573195, -2.775016, -2.9805446, -0.7092971, 1.2918588, 1.6118844, 1.4471692, -0.10967212, -2.3242273, -2.682143, -0.18732727, 1.4688009, 2.2533176, 1.4776525, 0.4069004, -2.2158356, -3.3277833, -0.5770095, 1.5901377, 1.6890593, 1.3663079, -0.0612026, -2.2799954, -3.3901672, -0.61002696, 0.043697435, 0.40892792, -0.07185392, -0.58828956, -2.8423986, -2.8150008, -1.3823988, -1.5879071, -1.3423474, -1.5323929, -1.7520448, -2.9110427
    };

    BN_gamma_in = {
			0.85062426, 0.23563486, 0.30903727, 0.5092783, 0.69867074, 0.6043543, 0.21364665, 0.24868739, 0.89496803
    };

    BN_gamma_1 = {
			6.9667377, 8.949154, 8.176527, 4.7854686, 5.2724366, 16.14689, -4.8414803, 6.2855887, 8.2501955, 2.6726978, 13.578046, 5.2012205, 5.9329453, 2.4768038, 3.3853822, 3.639665, 8.403637, 13.823827, 2.3240232, -9.157711, 10.091165, 4.0907645
    };

    BN_gamma_2 = {
			0.19936463, 0.30307928, 0.36566442, 0.12790012, 0.19976652, 0.2759202, 0.30252078, 0.23616277, 0.19052108, 0.10503883, 0.112705864, 0.28840595, -0.15166497, 0.14650807, 0.15448253, 0.1991286, 0.14659642, 0.3057434, 0.46937895, 0.34430093
    };

    BN_beta_1 = {
			-0.08002041, -0.3489749, -0.2212502, -0.4612334, -0.14123337, -0.07219588, 0.38908014, -0.080368206, -0.05386581, -0.3399004, -0.13758463, -0.30541095, -0.18299349, -2.0354266, -0.38951075, -0.10767994, -0.23840709, -0.094649546, -0.21605329, 0.18096037, -0.06483804, -0.45121232
    };

    BN_beta_2 = {
			-0.21719526, -0.08363421, -0.045220397, -0.17606291, -0.25792444, -0.073376104, -0.15678528, -0.09764388, -0.1577957, -0.117590316, -0.09137301, -0.08742409, 0.11877092, -0.21670775, -0.2904981, -0.111151285, -0.10000637, -0.13164085, -0.15238073, -0.072151504
    };
    
    mean = {
      58121.79697777398,35982.63326206248,54126.034579215026,47099.84770860291,16918.041715461684,46786.465602280994,54394.190536420305,35557.42198730395,57916.08224750105
    };

    stdev = {
      209719.99252336434,156429.86517925534,193617.6018444604,183553.46721868264,127286.93783731306,182093.47999310683,194486.93364851017,155514.52171421162,208118.4647262227
    };
  }
  
}


__inline Void TEncSearch::xTZSearchHelp( const TComPattern* const pcPatternKey, IntTZSearchStruct& rcStruct, const Int iSearchX, const Int iSearchY, const UChar ucPointNr, const UInt uiDistance )
{
  Distortion  uiSad = 0;

  const Pel* const  piRefSrch = rcStruct.piRefY + iSearchY * rcStruct.iYStride + iSearchX;

  //-- jclee for using the SAD function pointer
  m_pcRdCost->setDistParam( pcPatternKey, piRefSrch, rcStruct.iYStride,  m_cDistParam );

  setDistParamComp(COMPONENT_Y);

  // distortion
  m_cDistParam.bitDepth = pcPatternKey->getBitDepthY();
  m_cDistParam.m_maximumDistortionForEarlyExit = rcStruct.uiBestSad;

  if((m_pcEncCfg->getRestrictMESampling() == false) && m_pcEncCfg->getMotionEstimationSearchMethod() == MESEARCH_SELECTIVE)
  {
    Int isubShift = 0;
    // motion cost
    Distortion uiBitCost = m_pcRdCost->getCostOfVectorWithPredictor( iSearchX, iSearchY );

    // Skip search if bit cost is already larger than best SAD
    if (uiBitCost < rcStruct.uiBestSad)
    {
      if ( m_cDistParam.iRows > 32 )
      {
        m_cDistParam.iSubShift = 4;
      }
      else if ( m_cDistParam.iRows > 16 )
      {
        m_cDistParam.iSubShift = 3;
      }
      else if ( m_cDistParam.iRows > 8 )
      {
        m_cDistParam.iSubShift = 2;
      }
      else
      {
        m_cDistParam.iSubShift = 1;
      }

      Distortion uiTempSad = m_cDistParam.DistFunc( &m_cDistParam );
      if((uiTempSad + uiBitCost) < rcStruct.uiBestSad)
      {
        uiSad += uiTempSad >>  m_cDistParam.iSubShift;
        while(m_cDistParam.iSubShift > 0)
        {
          isubShift         = m_cDistParam.iSubShift -1;
          m_cDistParam.pOrg = pcPatternKey->getROIY() + (pcPatternKey->getPatternLStride() << isubShift);
          m_cDistParam.pCur = piRefSrch + (rcStruct.iYStride << isubShift);
          uiTempSad = m_cDistParam.DistFunc( &m_cDistParam );
          uiSad += uiTempSad >>  m_cDistParam.iSubShift;
          if(((uiSad << isubShift) + uiBitCost) > rcStruct.uiBestSad)
          {
            break;
          }

          m_cDistParam.iSubShift--;
        }

        if(m_cDistParam.iSubShift == 0)
        {
          uiSad += uiBitCost;
          if( uiSad < rcStruct.uiBestSad )
          {
            rcStruct.uiBestSad      = uiSad;
            rcStruct.iBestX         = iSearchX;
            rcStruct.iBestY         = iSearchY;
            rcStruct.uiBestDistance = uiDistance;
            rcStruct.uiBestRound    = 0;
            rcStruct.ucPointNr      = ucPointNr;
            m_cDistParam.m_maximumDistortionForEarlyExit = uiSad;
          }
        }
      }
    }
  }
  else
  {
    // fast encoder decision: use subsampled SAD when rows > 8 for integer ME
    if ( m_pcEncCfg->getFastInterSearchMode()==FASTINTERSEARCH_MODE1 || m_pcEncCfg->getFastInterSearchMode()==FASTINTERSEARCH_MODE3 )
    {
      if ( m_cDistParam.iRows > 8 )
      {
        m_cDistParam.iSubShift = 1;
      }
    }

    uiSad = m_cDistParam.DistFunc( &m_cDistParam );

    // EMI: Modification "array_e & counter_i"
    array_e[counter_i] = uiSad;
    
    // only add motion cost if uiSad is smaller than best. Otherwise pointless
    // to add motion cost.
    if( uiSad < rcStruct.uiBestSad )
    {
      // motion cost
      uiSad += m_pcRdCost->getCostOfVectorWithPredictor( iSearchX, iSearchY );

      if( uiSad < rcStruct.uiBestSad )
      {
        rcStruct.uiBestSad      = uiSad;
        rcStruct.iBestX         = iSearchX;
        rcStruct.iBestY         = iSearchY;
        rcStruct.uiBestDistance = uiDistance;
        rcStruct.uiBestRound    = 0;
        rcStruct.ucPointNr      = ucPointNr;
        m_cDistParam.m_maximumDistortionForEarlyExit = uiSad;
      }
    }
  }
  counter_i = counter_i + 1;
}

__inline Void TEncSearch::xTZ2PointSearch( const TComPattern* const pcPatternKey, IntTZSearchStruct& rcStruct, const TComMv* const pcMvSrchRngLT, const TComMv* const pcMvSrchRngRB )
{
  Int   iSrchRngHorLeft   = pcMvSrchRngLT->getHor();
  Int   iSrchRngHorRight  = pcMvSrchRngRB->getHor();
  Int   iSrchRngVerTop    = pcMvSrchRngLT->getVer();
  Int   iSrchRngVerBottom = pcMvSrchRngRB->getVer();

  // 2 point search,                   //   1 2 3
  // check only the 2 untested points  //   4 0 5
  // around the start point            //   6 7 8
  Int iStartX = rcStruct.iBestX;
  Int iStartY = rcStruct.iBestY;
  switch( rcStruct.ucPointNr )
  {
    case 1:
    {
      if ( (iStartX - 1) >= iSrchRngHorLeft )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iStartY, 0, 2 );
      }
      if ( (iStartY - 1) >= iSrchRngVerTop )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iStartY - 1, 0, 2 );
      }
    }
      break;
    case 2:
    {
      if ( (iStartY - 1) >= iSrchRngVerTop )
      {
        if ( (iStartX - 1) >= iSrchRngHorLeft )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iStartY - 1, 0, 2 );
        }
        if ( (iStartX + 1) <= iSrchRngHorRight )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iStartY - 1, 0, 2 );
        }
      }
    }
      break;
    case 3:
    {
      if ( (iStartY - 1) >= iSrchRngVerTop )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iStartY - 1, 0, 2 );
      }
      if ( (iStartX + 1) <= iSrchRngHorRight )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iStartY, 0, 2 );
      }
    }
      break;
    case 4:
    {
      if ( (iStartX - 1) >= iSrchRngHorLeft )
      {
        if ( (iStartY + 1) <= iSrchRngVerBottom )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iStartY + 1, 0, 2 );
        }
        if ( (iStartY - 1) >= iSrchRngVerTop )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iStartY - 1, 0, 2 );
        }
      }
    }
      break;
    case 5:
    {
      if ( (iStartX + 1) <= iSrchRngHorRight )
      {
        if ( (iStartY - 1) >= iSrchRngVerTop )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iStartY - 1, 0, 2 );
        }
        if ( (iStartY + 1) <= iSrchRngVerBottom )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iStartY + 1, 0, 2 );
        }
      }
    }
      break;
    case 6:
    {
      if ( (iStartX - 1) >= iSrchRngHorLeft )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iStartY , 0, 2 );
      }
      if ( (iStartY + 1) <= iSrchRngVerBottom )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iStartY + 1, 0, 2 );
      }
    }
      break;
    case 7:
    {
      if ( (iStartY + 1) <= iSrchRngVerBottom )
      {
        if ( (iStartX - 1) >= iSrchRngHorLeft )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iStartY + 1, 0, 2 );
        }
        if ( (iStartX + 1) <= iSrchRngHorRight )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iStartY + 1, 0, 2 );
        }
      }
    }
      break;
    case 8:
    {
      if ( (iStartX + 1) <= iSrchRngHorRight )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iStartY, 0, 2 );
      }
      if ( (iStartY + 1) <= iSrchRngVerBottom )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iStartY + 1, 0, 2 );
      }
    }
      break;
    default:
    {
      assert( false );
    }
      break;
  } // switch( rcStruct.ucPointNr )
}




__inline Void TEncSearch::xTZ8PointSquareSearch( const TComPattern* const pcPatternKey, IntTZSearchStruct& rcStruct, const TComMv* const pcMvSrchRngLT, const TComMv* const pcMvSrchRngRB, const Int iStartX, const Int iStartY, const Int iDist )
{
  const Int   iSrchRngHorLeft   = pcMvSrchRngLT->getHor();
  const Int   iSrchRngHorRight  = pcMvSrchRngRB->getHor();
  const Int   iSrchRngVerTop    = pcMvSrchRngLT->getVer();
  const Int   iSrchRngVerBottom = pcMvSrchRngRB->getVer();

  // 8 point search,                   //   1 2 3
  // search around the start point     //   4 0 5
  // with the required  distance       //   6 7 8
  assert( iDist != 0 );
  const Int iTop        = iStartY - iDist;
  const Int iBottom     = iStartY + iDist;
  const Int iLeft       = iStartX - iDist;
  const Int iRight      = iStartX + iDist;
  rcStruct.uiBestRound += 1;

  if ( iTop >= iSrchRngVerTop ) // check top
  {
    if ( iLeft >= iSrchRngHorLeft ) // check top left
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iTop, 1, iDist );
    }
    // top middle
    xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iTop, 2, iDist );

    if ( iRight <= iSrchRngHorRight ) // check top right
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iRight, iTop, 3, iDist );
    }
  } // check top
  if ( iLeft >= iSrchRngHorLeft ) // check middle left
  {
    xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iStartY, 4, iDist );
  }
  if ( iRight <= iSrchRngHorRight ) // check middle right
  {
    xTZSearchHelp( pcPatternKey, rcStruct, iRight, iStartY, 5, iDist );
  }
  if ( iBottom <= iSrchRngVerBottom ) // check bottom
  {
    if ( iLeft >= iSrchRngHorLeft ) // check bottom left
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iBottom, 6, iDist );
    }
    // check bottom middle
    xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iBottom, 7, iDist );

    if ( iRight <= iSrchRngHorRight ) // check bottom right
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iRight, iBottom, 8, iDist );
    }
  } // check bottom
}


//additing other square search

__inline Void TEncSearch::xTZ8PointSquareSearch2( const TComPattern* const pcPatternKey, IntTZSearchStruct& rcStruct, const TComMv* const pcMvSrchRngLT, const TComMv* const pcMvSrchRngRB, const Int iStartX, const Int iStartY, const Int iDist )
{
  const Int   iSrchRngHorLeft   = pcMvSrchRngLT->getHor();
  const Int   iSrchRngHorRight  = pcMvSrchRngRB->getHor();
  const Int   iSrchRngVerTop    = pcMvSrchRngLT->getVer();
  const Int   iSrchRngVerBottom = pcMvSrchRngRB->getVer();

  // 8 point search,                   //   1 2 3
  // search around the start point     //   4 0 5
  // with the required  distance       //   6 7 8
  assert( iDist != 0 );
  const Int iTop        = iStartY - iDist;
  const Int iBottom     = iStartY + iDist;
  const Int iLeft       = iStartX - iDist;
  const Int iRight      = iStartX + iDist;
  rcStruct.uiBestRound += 1;
// check top
  if ( iTop >= iSrchRngVerTop ) // check top
  {
	 if ( iLeft >= iSrchRngHorLeft ) // check top left
    {
		xTZSearchHelp(pcPatternKey, rcStruct, iLeft, iTop, 9, iDist);
    }
	  
	 if ( iLeft >= iSrchRngHorLeft ) // check top left
    {
		xTZSearchHelp(pcPatternKey, rcStruct, iStartX - 1, iTop, 10, iDist);
    }
    xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iTop, 11, iDist );
	
	if (iRight <= iSrchRngHorRight) // check top left
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iStartX +1, iTop, 12, iDist );
    }
	
    if ( iRight <= iSrchRngHorRight ) // check top right
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iRight, iTop, 13, iDist );
    }
  }

  if ( iLeft >= iSrchRngHorLeft ) // check middle left
  {
    xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iStartY-1, 14, iDist );
  }

  if (iRight <= iSrchRngHorRight) // check middle left
  {
    xTZSearchHelp( pcPatternKey, rcStruct, iRight, iStartY-1, 15, iDist );
  }
  
  
  if ( iLeft >= iSrchRngHorLeft ) // check middle left
  {
    xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iStartY, 16, iDist );
  }
  
  
  if ( iRight <= iSrchRngHorRight ) // check middle right
  {
    xTZSearchHelp( pcPatternKey, rcStruct, iRight, iStartY, 17, iDist );
  }
  
  if ( iLeft >= iSrchRngHorLeft ) // check middle left
  {
    xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iStartY+1, 18, iDist );
  }
  
  if (iRight <= iSrchRngHorRight) // check middle left
  {
    xTZSearchHelp( pcPatternKey, rcStruct, iRight, iStartY+1, 19, iDist );
  }
  
  
  
  if ( iBottom <= iSrchRngVerBottom ) // check bottom
  {
	  
	if ( iLeft >= iSrchRngHorLeft ) // check bottom left
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iBottom, 20, iDist );
    }  
	  
	if ( iLeft >= iSrchRngHorLeft ) // check bottom left
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iBottom, 21, iDist );
    }   
	  
	  
    
    // check bottom middle
    xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iBottom, 22, iDist );

	if ( iRight <= iSrchRngHorRight ) // check bottom right
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iBottom, 23, iDist );
    }
	
    if ( iRight <= iSrchRngHorRight ) // check bottom right
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iRight, iBottom, 24, iDist );
    }
  } 
  
  // check bottom
}











__inline Void TEncSearch::xTZ8PointDiamondSearch( const TComPattern*const  pcPatternKey,
                                                  IntTZSearchStruct& rcStruct,
                                                  const TComMv*const  pcMvSrchRngLT,
                                                  const TComMv*const  pcMvSrchRngRB,
                                                  const Int iStartX,
                                                  const Int iStartY,
                                                  const Int iDist,
                                                  const Bool bCheckCornersAtDist1 )
{
  const Int   iSrchRngHorLeft   = pcMvSrchRngLT->getHor();
  const Int   iSrchRngHorRight  = pcMvSrchRngRB->getHor();
  const Int   iSrchRngVerTop    = pcMvSrchRngLT->getVer();
  const Int   iSrchRngVerBottom = pcMvSrchRngRB->getVer();

  // 8 point search,                   //   1 2 3
  // search around the start point     //   4 0 5
  // with the required  distance       //   6 7 8
  assert ( iDist != 0 );
  const Int iTop        = iStartY - iDist;
  const Int iBottom     = iStartY + iDist;
  const Int iLeft       = iStartX - iDist;
  const Int iRight      = iStartX + iDist;
  rcStruct.uiBestRound += 1;

  if ( iDist == 1 )
  {
    if ( iTop >= iSrchRngVerTop ) // check top
    {
      if (bCheckCornersAtDist1)
      {
        if ( iLeft >= iSrchRngHorLeft) // check top-left
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iTop, 1, iDist );
        }
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iTop, 2, iDist );
        if ( iRight <= iSrchRngHorRight ) // check middle right
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iRight, iTop, 3, iDist );
        }
      }
      else
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iTop, 2, iDist );
      }
    }
    if ( iLeft >= iSrchRngHorLeft ) // check middle left
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iStartY, 4, iDist );
    }
    if ( iRight <= iSrchRngHorRight ) // check middle right
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iRight, iStartY, 5, iDist );
    }
    if ( iBottom <= iSrchRngVerBottom ) // check bottom
    {
      if (bCheckCornersAtDist1)
      {
        if ( iLeft >= iSrchRngHorLeft) // check top-left
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iBottom, 6, iDist );
        }
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iBottom, 7, iDist );
        if ( iRight <= iSrchRngHorRight ) // check middle right
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iRight, iBottom, 8, iDist );
        }
      }
      else
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iBottom, 7, iDist );
      }
    }
  }
  else
  {
    if ( iDist <= 8 )
    {
      const Int iTop_2      = iStartY - (iDist>>1);
      const Int iBottom_2   = iStartY + (iDist>>1);
      const Int iLeft_2     = iStartX - (iDist>>1);
      const Int iRight_2    = iStartX + (iDist>>1);

      if (  iTop >= iSrchRngVerTop && iLeft >= iSrchRngHorLeft &&
          iRight <= iSrchRngHorRight && iBottom <= iSrchRngVerBottom ) // check border
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX,  iTop,      2, iDist    );
        xTZSearchHelp( pcPatternKey, rcStruct, iLeft_2,  iTop_2,    1, iDist>>1 );
        xTZSearchHelp( pcPatternKey, rcStruct, iRight_2, iTop_2,    3, iDist>>1 );
        xTZSearchHelp( pcPatternKey, rcStruct, iLeft,    iStartY,   4, iDist    );
        xTZSearchHelp( pcPatternKey, rcStruct, iRight,   iStartY,   5, iDist    );
        xTZSearchHelp( pcPatternKey, rcStruct, iLeft_2,  iBottom_2, 6, iDist>>1 );
        xTZSearchHelp( pcPatternKey, rcStruct, iRight_2, iBottom_2, 8, iDist>>1 );
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX,  iBottom,   7, iDist    );
      }
      else // check border
      {
        if ( iTop >= iSrchRngVerTop ) // check top
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iTop, 2, iDist );
        }
        if ( iTop_2 >= iSrchRngVerTop ) // check half top
        {
          if ( iLeft_2 >= iSrchRngHorLeft ) // check half left
          {
            xTZSearchHelp( pcPatternKey, rcStruct, iLeft_2, iTop_2, 1, (iDist>>1) );
          }
          if ( iRight_2 <= iSrchRngHorRight ) // check half right
          {
            xTZSearchHelp( pcPatternKey, rcStruct, iRight_2, iTop_2, 3, (iDist>>1) );
          }
        } // check half top
        if ( iLeft >= iSrchRngHorLeft ) // check left
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iStartY, 4, iDist );
        }
        if ( iRight <= iSrchRngHorRight ) // check right
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iRight, iStartY, 5, iDist );
        }
        if ( iBottom_2 <= iSrchRngVerBottom ) // check half bottom
        {
          if ( iLeft_2 >= iSrchRngHorLeft ) // check half left
          {
            xTZSearchHelp( pcPatternKey, rcStruct, iLeft_2, iBottom_2, 6, (iDist>>1) );
          }
          if ( iRight_2 <= iSrchRngHorRight ) // check half right
          {
            xTZSearchHelp( pcPatternKey, rcStruct, iRight_2, iBottom_2, 8, (iDist>>1) );
          }
        } // check half bottom
        if ( iBottom <= iSrchRngVerBottom ) // check bottom
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iBottom, 7, iDist );
        }
      } // check border
    }
    else // iDist > 8
    {
      if ( iTop >= iSrchRngVerTop && iLeft >= iSrchRngHorLeft &&
          iRight <= iSrchRngHorRight && iBottom <= iSrchRngVerBottom ) // check border
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iTop,    0, iDist );
        xTZSearchHelp( pcPatternKey, rcStruct, iLeft,   iStartY, 0, iDist );
        xTZSearchHelp( pcPatternKey, rcStruct, iRight,  iStartY, 0, iDist );
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iBottom, 0, iDist );
        for ( Int index = 1; index < 4; index++ )
        {
          const Int iPosYT = iTop    + ((iDist>>2) * index);
          const Int iPosYB = iBottom - ((iDist>>2) * index);
          const Int iPosXL = iStartX - ((iDist>>2) * index);
          const Int iPosXR = iStartX + ((iDist>>2) * index);
          xTZSearchHelp( pcPatternKey, rcStruct, iPosXL, iPosYT, 0, iDist );
          xTZSearchHelp( pcPatternKey, rcStruct, iPosXR, iPosYT, 0, iDist );
          xTZSearchHelp( pcPatternKey, rcStruct, iPosXL, iPosYB, 0, iDist );
          xTZSearchHelp( pcPatternKey, rcStruct, iPosXR, iPosYB, 0, iDist );
        }
      }
      else // check border
      {
        if ( iTop >= iSrchRngVerTop ) // check top
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iTop, 0, iDist );
        }
        if ( iLeft >= iSrchRngHorLeft ) // check left
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iStartY, 0, iDist );
        }
        if ( iRight <= iSrchRngHorRight ) // check right
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iRight, iStartY, 0, iDist );
        }
        if ( iBottom <= iSrchRngVerBottom ) // check bottom
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iBottom, 0, iDist );
        }
        for ( Int index = 1; index < 4; index++ )
        {
          const Int iPosYT = iTop    + ((iDist>>2) * index);
          const Int iPosYB = iBottom - ((iDist>>2) * index);
          const Int iPosXL = iStartX - ((iDist>>2) * index);
          const Int iPosXR = iStartX + ((iDist>>2) * index);

          if ( iPosYT >= iSrchRngVerTop ) // check top
          {
            if ( iPosXL >= iSrchRngHorLeft ) // check left
            {
              xTZSearchHelp( pcPatternKey, rcStruct, iPosXL, iPosYT, 0, iDist );
            }
            if ( iPosXR <= iSrchRngHorRight ) // check right
            {
              xTZSearchHelp( pcPatternKey, rcStruct, iPosXR, iPosYT, 0, iDist );
            }
          } // check top
          if ( iPosYB <= iSrchRngVerBottom ) // check bottom
          {
            if ( iPosXL >= iSrchRngHorLeft ) // check left
            {
              xTZSearchHelp( pcPatternKey, rcStruct, iPosXL, iPosYB, 0, iDist );
            }
            if ( iPosXR <= iSrchRngHorRight ) // check right
            {
              xTZSearchHelp( pcPatternKey, rcStruct, iPosXR, iPosYB, 0, iDist );
            }
          } // check bottom
        } // for ...
      } // check border
    } // iDist <= 8
  } // iDist == 1
}

Distortion TEncSearch::xPatternRefinement( TComPattern* pcPatternKey,
                                           TComMv baseRefMv,
                                           Int iFrac, TComMv& rcMvFrac,
                                           Bool bAllowUseOfHadamard
                                         )
{
  Distortion  uiDist;
  Distortion  uiDistBest  = std::numeric_limits<Distortion>::max();
  UInt        uiDirecBest = 0;

  Pel*  piRefPos;
  Int iRefStride = m_filteredBlock[0][0].getStride(COMPONENT_Y);

  m_pcRdCost->setDistParam( pcPatternKey, m_filteredBlock[0][0].getAddr(COMPONENT_Y), iRefStride, 1, m_cDistParam, m_pcEncCfg->getUseHADME() && bAllowUseOfHadamard );

  const TComMv* pcMvRefine = (iFrac == 2 ? s_acMvRefineH : s_acMvRefineQ);

  for (UInt i = 0; i < 9; i++)
  {
    TComMv cMvTest = pcMvRefine[i];
    cMvTest += baseRefMv;

    Int horVal = cMvTest.getHor() * iFrac;
    Int verVal = cMvTest.getVer() * iFrac;
    piRefPos = m_filteredBlock[ verVal & 3 ][ horVal & 3 ].getAddr(COMPONENT_Y);
    if ( horVal == 2 && ( verVal & 1 ) == 0 )
    {
      piRefPos += 1;
    }
    if ( ( horVal & 1 ) == 0 && verVal == 2 )
    {
      piRefPos += iRefStride;
    }
    cMvTest = pcMvRefine[i];
    cMvTest += rcMvFrac;

    setDistParamComp(COMPONENT_Y);

    m_cDistParam.pCur = piRefPos;
    m_cDistParam.bitDepth = pcPatternKey->getBitDepthY();
    uiDist = m_cDistParam.DistFunc( &m_cDistParam );
    uiDist += m_pcRdCost->getCostOfVectorWithPredictor( cMvTest.getHor(), cMvTest.getVer() );

    if ( uiDist < uiDistBest )
    {
      uiDistBest  = uiDist;
      uiDirecBest = i;
      m_cDistParam.m_maximumDistortionForEarlyExit = uiDist;
    }
  }

  rcMvFrac = pcMvRefine[uiDirecBest];

  return uiDistBest;
}



Void
TEncSearch::xEncSubdivCbfQT(TComTU      &rTu,
                            Bool         bLuma,
                            Bool         bChroma )
{
  TComDataCU* pcCU=rTu.getCU();
  const UInt uiAbsPartIdx         = rTu.GetAbsPartIdxTU();
  const UInt uiTrDepth            = rTu.GetTransformDepthRel();
  const UInt uiTrMode             = pcCU->getTransformIdx( uiAbsPartIdx );
  const UInt uiSubdiv             = ( uiTrMode > uiTrDepth ? 1 : 0 );
  const UInt uiLog2LumaTrafoSize  = rTu.GetLog2LumaTrSize();

  if( pcCU->isIntra(0) && pcCU->getPartitionSize(0) == SIZE_NxN && uiTrDepth == 0 )
  {
    assert( uiSubdiv );
  }
  else if( uiLog2LumaTrafoSize > pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() )
  {
    assert( uiSubdiv );
  }
  else if( uiLog2LumaTrafoSize == pcCU->getSlice()->getSPS()->getQuadtreeTULog2MinSize() )
  {
    assert( !uiSubdiv );
  }
  else if( uiLog2LumaTrafoSize == pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) )
  {
    assert( !uiSubdiv );
  }
  else
  {
    assert( uiLog2LumaTrafoSize > pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) );
    if( bLuma )
    {
      m_pcEntropyCoder->encodeTransformSubdivFlag( uiSubdiv, 5 - uiLog2LumaTrafoSize );
    }
  }

  if ( bChroma )
  {
    const UInt numberValidComponents = getNumberValidComponents(rTu.GetChromaFormat());
    for (UInt ch=COMPONENT_Cb; ch<numberValidComponents; ch++)
    {
      const ComponentID compID=ComponentID(ch);
      if( rTu.ProcessingAllQuadrants(compID) && (uiTrDepth==0 || pcCU->getCbf( uiAbsPartIdx, compID, uiTrDepth-1 ) ))
      {
        m_pcEntropyCoder->encodeQtCbf(rTu, compID, (uiSubdiv == 0));
      }
    }
  }

  if( uiSubdiv )
  {
    TComTURecurse tuRecurse(rTu, false);
    do
    {
      xEncSubdivCbfQT( tuRecurse, bLuma, bChroma );
    } while (tuRecurse.nextSection(rTu));
  }
  else
  {
    //===== Cbfs =====
    if( bLuma )
    {
      m_pcEntropyCoder->encodeQtCbf( rTu, COMPONENT_Y, true );
    }
  }
}




Void
TEncSearch::xEncCoeffQT(TComTU &rTu,
                        const ComponentID  component,
                        Bool         bRealCoeff )
{
  TComDataCU* pcCU=rTu.getCU();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  const UInt uiTrDepth=rTu.GetTransformDepthRel();

  const UInt  uiTrMode        = pcCU->getTransformIdx( uiAbsPartIdx );
  const UInt  uiSubdiv        = ( uiTrMode > uiTrDepth ? 1 : 0 );

  if( uiSubdiv )
  {
    TComTURecurse tuRecurseChild(rTu, false);
    do
    {
      xEncCoeffQT( tuRecurseChild, component, bRealCoeff );
    } while (tuRecurseChild.nextSection(rTu) );
  }
  else if (rTu.ProcessComponentSection(component))
  {
    //===== coefficients =====
    const UInt  uiLog2TrafoSize = rTu.GetLog2LumaTrSize();
    UInt    uiCoeffOffset   = rTu.getCoefficientOffset(component);
    UInt    uiQTLayer       = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrafoSize;
    TCoeff* pcCoeff         = bRealCoeff ? pcCU->getCoeff(component) : m_ppcQTTempCoeff[component][uiQTLayer];

    if (isChroma(component) && (pcCU->getCbf( rTu.GetAbsPartIdxTU(), COMPONENT_Y, uiTrMode ) != 0) && pcCU->getSlice()->getPPS()->getPpsRangeExtension().getCrossComponentPredictionEnabledFlag() )
    {
      m_pcEntropyCoder->encodeCrossComponentPrediction( rTu, component );
    }

    m_pcEntropyCoder->encodeCoeffNxN( rTu, pcCoeff+uiCoeffOffset, component );
  }
}




Void
TEncSearch::xEncIntraHeader( TComDataCU*  pcCU,
                            UInt         uiTrDepth,
                            UInt         uiAbsPartIdx,
                            Bool         bLuma,
                            Bool         bChroma )
{
  if( bLuma )
  {
    // CU header
    if( uiAbsPartIdx == 0 )
    {
      if( !pcCU->getSlice()->isIntra() )
      {
        if (pcCU->getSlice()->getPPS()->getTransquantBypassEnableFlag())
        {
          m_pcEntropyCoder->encodeCUTransquantBypassFlag( pcCU, 0, true );
        }
        m_pcEntropyCoder->encodeSkipFlag( pcCU, 0, true );
        m_pcEntropyCoder->encodePredMode( pcCU, 0, true );
      }
      m_pcEntropyCoder  ->encodePartSize( pcCU, 0, pcCU->getDepth(0), true );

      if (pcCU->isIntra(0) && pcCU->getPartitionSize(0) == SIZE_2Nx2N )
      {
        m_pcEntropyCoder->encodeIPCMInfo( pcCU, 0, true );

        if ( pcCU->getIPCMFlag (0))
        {
          return;
        }
      }
    }
    // luma prediction mode
    if( pcCU->getPartitionSize(0) == SIZE_2Nx2N )
    {
      if (uiAbsPartIdx==0)
      {
        m_pcEntropyCoder->encodeIntraDirModeLuma ( pcCU, 0 );
      }
    }
    else
    {
      UInt uiQNumParts = pcCU->getTotalNumPart() >> 2;
      if (uiTrDepth>0 && (uiAbsPartIdx%uiQNumParts)==0)
      {
        m_pcEntropyCoder->encodeIntraDirModeLuma ( pcCU, uiAbsPartIdx );
      }
    }
  }

  if( bChroma )
  {
    if( pcCU->getPartitionSize(0) == SIZE_2Nx2N || !enable4ChromaPUsInIntraNxNCU(pcCU->getPic()->getChromaFormat()))
    {
      if(uiAbsPartIdx==0)
      {
         m_pcEntropyCoder->encodeIntraDirModeChroma ( pcCU, uiAbsPartIdx );
      }
    }
    else
    {
      UInt uiQNumParts = pcCU->getTotalNumPart() >> 2;
      assert(uiTrDepth>0);
      if ((uiAbsPartIdx%uiQNumParts)==0)
      {
        m_pcEntropyCoder->encodeIntraDirModeChroma ( pcCU, uiAbsPartIdx );
      }
    }
  }
}




UInt
TEncSearch::xGetIntraBitsQT(TComTU &rTu,
                            Bool         bLuma,
                            Bool         bChroma,
                            Bool         bRealCoeff /* just for test */ )
{
  TComDataCU* pcCU=rTu.getCU();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  const UInt uiTrDepth=rTu.GetTransformDepthRel();
  m_pcEntropyCoder->resetBits();
  xEncIntraHeader ( pcCU, uiTrDepth, uiAbsPartIdx, bLuma, bChroma );
  xEncSubdivCbfQT ( rTu, bLuma, bChroma );

  if( bLuma )
  {
    xEncCoeffQT   ( rTu, COMPONENT_Y,      bRealCoeff );
  }
  if( bChroma )
  {
    xEncCoeffQT   ( rTu, COMPONENT_Cb,  bRealCoeff );
    xEncCoeffQT   ( rTu, COMPONENT_Cr,  bRealCoeff );
  }
  UInt   uiBits = m_pcEntropyCoder->getNumberOfWrittenBits();

  return uiBits;
}

UInt TEncSearch::xGetIntraBitsQTChroma(TComTU &rTu,
                                       ComponentID compID,
                                       Bool         bRealCoeff /* just for test */ )
{
  m_pcEntropyCoder->resetBits();
  xEncCoeffQT   ( rTu, compID,  bRealCoeff );
  UInt   uiBits = m_pcEntropyCoder->getNumberOfWrittenBits();
  return uiBits;
}

Void TEncSearch::xIntraCodingTUBlock(       TComYuv*    pcOrgYuv,
                                            TComYuv*    pcPredYuv,
                                            TComYuv*    pcResiYuv,
                                            Pel         resiLuma[NUMBER_OF_STORED_RESIDUAL_TYPES][MAX_CU_SIZE * MAX_CU_SIZE],
                                      const Bool        checkCrossCPrediction,
                                            Distortion& ruiDist,
                                      const ComponentID compID,
                                            TComTU&     rTu
                                      DEBUG_STRING_FN_DECLARE(sDebug)
                                           ,Int         default0Save1Load2
                                     )
{
  if (!rTu.ProcessComponentSection(compID))
  {
    return;
  }
  const Bool           bIsLuma          = isLuma(compID);
  const TComRectangle &rect             = rTu.getRect(compID);
        TComDataCU    *pcCU             = rTu.getCU();
  const UInt           uiAbsPartIdx     = rTu.GetAbsPartIdxTU();
  const TComSPS       &sps              = *(pcCU->getSlice()->getSPS());

  const UInt           uiTrDepth        = rTu.GetTransformDepthRelAdj(compID);
  const UInt           uiFullDepth      = rTu.GetTransformDepthTotal();
  const UInt           uiLog2TrSize     = rTu.GetLog2LumaTrSize();
  const ChromaFormat   chFmt            = pcOrgYuv->getChromaFormat();
  const ChannelType    chType           = toChannelType(compID);
  const Int            bitDepth         = sps.getBitDepth(chType);

  const UInt           uiWidth          = rect.width;
  const UInt           uiHeight         = rect.height;
  const UInt           uiStride         = pcOrgYuv ->getStride (compID);
        Pel           *piOrg            = pcOrgYuv ->getAddr( compID, uiAbsPartIdx );
        Pel           *piPred           = pcPredYuv->getAddr( compID, uiAbsPartIdx );
        Pel           *piResi           = pcResiYuv->getAddr( compID, uiAbsPartIdx );
        Pel           *piReco           = pcPredYuv->getAddr( compID, uiAbsPartIdx );
  const UInt           uiQTLayer        = sps.getQuadtreeTULog2MaxSize() - uiLog2TrSize;
        Pel           *piRecQt          = m_pcQTTempTComYuv[ uiQTLayer ].getAddr( compID, uiAbsPartIdx );
  const UInt           uiRecQtStride    = m_pcQTTempTComYuv[ uiQTLayer ].getStride(compID);
  const UInt           uiZOrder         = pcCU->getZorderIdxInCtu() + uiAbsPartIdx;
        Pel           *piRecIPred       = pcCU->getPic()->getPicYuvRec()->getAddr( compID, pcCU->getCtuRsAddr(), uiZOrder );
        UInt           uiRecIPredStride = pcCU->getPic()->getPicYuvRec()->getStride  ( compID );
        TCoeff        *pcCoeff          = m_ppcQTTempCoeff[compID][uiQTLayer] + rTu.getCoefficientOffset(compID);
        Bool           useTransformSkip = pcCU->getTransformSkip(uiAbsPartIdx, compID);

#if ADAPTIVE_QP_SELECTION
        TCoeff        *pcArlCoeff       = m_ppcQTTempArlCoeff[compID][ uiQTLayer ] + rTu.getCoefficientOffset(compID);
#endif

  const UInt           uiChPredMode     = pcCU->getIntraDir( chType, uiAbsPartIdx );
  const UInt           partsPerMinCU    = 1<<(2*(sps.getMaxTotalCUDepth() - sps.getLog2DiffMaxMinCodingBlockSize()));
  const UInt           uiChCodedMode    = (uiChPredMode==DM_CHROMA_IDX && !bIsLuma) ? pcCU->getIntraDir(CHANNEL_TYPE_LUMA, getChromasCorrespondingPULumaIdx(uiAbsPartIdx, chFmt, partsPerMinCU)) : uiChPredMode;
  const UInt           uiChFinalMode    = ((chFmt == CHROMA_422)       && !bIsLuma) ? g_chroma422IntraAngleMappingTable[uiChCodedMode] : uiChCodedMode;

  const Int            blkX                                 = g_auiRasterToPelX[ g_auiZscanToRaster[ uiAbsPartIdx ] ];
  const Int            blkY                                 = g_auiRasterToPelY[ g_auiZscanToRaster[ uiAbsPartIdx ] ];
  const Int            bufferOffset                         = blkX + (blkY * MAX_CU_SIZE);
        Pel  *const    encoderLumaResidual                  = resiLuma[RESIDUAL_ENCODER_SIDE ] + bufferOffset;
        Pel  *const    reconstructedLumaResidual            = resiLuma[RESIDUAL_RECONSTRUCTED] + bufferOffset;
  const Bool           bUseCrossCPrediction                 = isChroma(compID) && (uiChPredMode == DM_CHROMA_IDX) && checkCrossCPrediction;
  const Bool           bUseReconstructedResidualForEstimate = m_pcEncCfg->getUseReconBasedCrossCPredictionEstimate();
        Pel *const     lumaResidualForEstimate              = bUseReconstructedResidualForEstimate ? reconstructedLumaResidual : encoderLumaResidual;

#if DEBUG_STRING
  const Int debugPredModeMask=DebugStringGetPredModeMask(MODE_INTRA);
#endif

  //===== init availability pattern =====
  DEBUG_STRING_NEW(sTemp)

#if !DEBUG_STRING
  if( default0Save1Load2 != 2 )
#endif
  {
    const Bool bUseFilteredPredictions=TComPrediction::filteringIntraReferenceSamples(compID, uiChFinalMode, uiWidth, uiHeight, chFmt, sps.getSpsRangeExtension().getIntraSmoothingDisabledFlag());

    initIntraPatternChType( rTu, compID, bUseFilteredPredictions DEBUG_STRING_PASS_INTO(sDebug) );

    //===== get prediction signal =====
    predIntraAng( compID, uiChFinalMode, piOrg, uiStride, piPred, uiStride, rTu, bUseFilteredPredictions );

    // save prediction
    if( default0Save1Load2 == 1 )
    {
      Pel*  pPred   = piPred;
      Pel*  pPredBuf = m_pSharedPredTransformSkip[compID];
      Int k = 0;
      for( UInt uiY = 0; uiY < uiHeight; uiY++ )
      {
        for( UInt uiX = 0; uiX < uiWidth; uiX++ )
        {
          pPredBuf[ k ++ ] = pPred[ uiX ];
        }
        pPred += uiStride;
      }
    }
  }
#if !DEBUG_STRING
  else
  {
    // load prediction
    Pel*  pPred   = piPred;
    Pel*  pPredBuf = m_pSharedPredTransformSkip[compID];
    Int k = 0;
    for( UInt uiY = 0; uiY < uiHeight; uiY++ )
    {
      for( UInt uiX = 0; uiX < uiWidth; uiX++ )
      {
        pPred[ uiX ] = pPredBuf[ k ++ ];
      }
      pPred += uiStride;
    }
  }
#endif

  //===== get residual signal =====
  {
    // get residual
    Pel*  pOrg    = piOrg;
    Pel*  pPred   = piPred;
    Pel*  pResi   = piResi;

    for( UInt uiY = 0; uiY < uiHeight; uiY++ )
    {
      for( UInt uiX = 0; uiX < uiWidth; uiX++ )
      {
        pResi[ uiX ] = pOrg[ uiX ] - pPred[ uiX ];
      }

      pOrg  += uiStride;
      pResi += uiStride;
      pPred += uiStride;
    }
  }

  if (pcCU->getSlice()->getPPS()->getPpsRangeExtension().getCrossComponentPredictionEnabledFlag())
  {
    if (bUseCrossCPrediction)
    {
      if (xCalcCrossComponentPredictionAlpha( rTu, compID, lumaResidualForEstimate, piResi, uiWidth, uiHeight, MAX_CU_SIZE, uiStride ) == 0)
      {
        return;
      }
      TComTrQuant::crossComponentPrediction ( rTu, compID, reconstructedLumaResidual, piResi, piResi, uiWidth, uiHeight, MAX_CU_SIZE, uiStride, uiStride, false );
    }
    else if (isLuma(compID) && !bUseReconstructedResidualForEstimate)
    {
      xStoreCrossComponentPredictionResult( encoderLumaResidual, piResi, rTu, 0, 0, MAX_CU_SIZE, uiStride );
    }
  }

  //===== transform and quantization =====
  //--- init rate estimation arrays for RDOQ ---
  if( useTransformSkip ? m_pcEncCfg->getUseRDOQTS() : m_pcEncCfg->getUseRDOQ() )
  {
    m_pcEntropyCoder->estimateBit( m_pcTrQuant->m_pcEstBitsSbac, uiWidth, uiHeight, chType );
  }

  //--- transform and quantization ---
  TCoeff uiAbsSum = 0;
  if (bIsLuma)
  {
    pcCU       ->setTrIdxSubParts ( uiTrDepth, uiAbsPartIdx, uiFullDepth );
  }

  const QpParam cQP(*pcCU, compID);

#if RDOQ_CHROMA_LAMBDA
  m_pcTrQuant->selectLambda     (compID);
#endif

  m_pcTrQuant->transformNxN     ( rTu, compID, piResi, uiStride, pcCoeff,
#if ADAPTIVE_QP_SELECTION
    pcArlCoeff,
#endif
    uiAbsSum, cQP
    );

  //--- inverse transform ---

#if DEBUG_STRING
  if ( (uiAbsSum > 0) || (DebugOptionList::DebugString_InvTran.getInt()&debugPredModeMask) )
#else
  if ( uiAbsSum > 0 )
#endif
  {
    m_pcTrQuant->invTransformNxN ( rTu, compID, piResi, uiStride, pcCoeff, cQP DEBUG_STRING_PASS_INTO_OPTIONAL(&sDebug, (DebugOptionList::DebugString_InvTran.getInt()&debugPredModeMask)) );
  }
  else
  {
    Pel* pResi = piResi;
    memset( pcCoeff, 0, sizeof( TCoeff ) * uiWidth * uiHeight );
    for( UInt uiY = 0; uiY < uiHeight; uiY++ )
    {
      memset( pResi, 0, sizeof( Pel ) * uiWidth );
      pResi += uiStride;
    }
  }


  //===== reconstruction =====
  {
    Pel* pPred      = piPred;
    Pel* pResi      = piResi;
    Pel* pReco      = piReco;
    Pel* pRecQt     = piRecQt;
    Pel* pRecIPred  = piRecIPred;

    if (pcCU->getSlice()->getPPS()->getPpsRangeExtension().getCrossComponentPredictionEnabledFlag())
    {
      if (bUseCrossCPrediction)
      {
        TComTrQuant::crossComponentPrediction( rTu, compID, reconstructedLumaResidual, piResi, piResi, uiWidth, uiHeight, MAX_CU_SIZE, uiStride, uiStride, true );
      }
      else if (isLuma(compID))
      {
        xStoreCrossComponentPredictionResult( reconstructedLumaResidual, piResi, rTu, 0, 0, MAX_CU_SIZE, uiStride );
      }
    }

 #if DEBUG_STRING
    std::stringstream ss(stringstream::out);
    const Bool bDebugPred=((DebugOptionList::DebugString_Pred.getInt()&debugPredModeMask) && DEBUG_STRING_CHANNEL_CONDITION(compID));
    const Bool bDebugResi=((DebugOptionList::DebugString_Resi.getInt()&debugPredModeMask) && DEBUG_STRING_CHANNEL_CONDITION(compID));
    const Bool bDebugReco=((DebugOptionList::DebugString_Reco.getInt()&debugPredModeMask) && DEBUG_STRING_CHANNEL_CONDITION(compID));

    if (bDebugPred || bDebugResi || bDebugReco)
    {
      ss << "###: " << "CompID: " << compID << " pred mode (ch/fin): " << uiChPredMode << "/" << uiChFinalMode << " absPartIdx: " << rTu.GetAbsPartIdxTU() << "\n";
      for( UInt uiY = 0; uiY < uiHeight; uiY++ )
      {
        ss << "###: ";
        if (bDebugPred)
        {
          ss << " - pred: ";
          for( UInt uiX = 0; uiX < uiWidth; uiX++ )
          {
            ss << pPred[ uiX ] << ", ";
          }
        }
        if (bDebugResi)
        {
          ss << " - resi: ";
        }
        for( UInt uiX = 0; uiX < uiWidth; uiX++ )
        {
          if (bDebugResi)
          {
            ss << pResi[ uiX ] << ", ";
          }
          pReco    [ uiX ] = Pel(ClipBD<Int>( Int(pPred[uiX]) + Int(pResi[uiX]), bitDepth ));
          pRecQt   [ uiX ] = pReco[ uiX ];
          pRecIPred[ uiX ] = pReco[ uiX ];
        }
        if (bDebugReco)
        {
          ss << " - reco: ";
          for( UInt uiX = 0; uiX < uiWidth; uiX++ )
          {
            ss << pReco[ uiX ] << ", ";
          }
        }
        pPred     += uiStride;
        pResi     += uiStride;
        pReco     += uiStride;
        pRecQt    += uiRecQtStride;
        pRecIPred += uiRecIPredStride;
        ss << "\n";
      }
      DEBUG_STRING_APPEND(sDebug, ss.str())
    }
    else
#endif
    {

      for( UInt uiY = 0; uiY < uiHeight; uiY++ )
      {
        for( UInt uiX = 0; uiX < uiWidth; uiX++ )
        {
          pReco    [ uiX ] = Pel(ClipBD<Int>( Int(pPred[uiX]) + Int(pResi[uiX]), bitDepth ));
          pRecQt   [ uiX ] = pReco[ uiX ];
          pRecIPred[ uiX ] = pReco[ uiX ];
        }
        pPred     += uiStride;
        pResi     += uiStride;
        pReco     += uiStride;
        pRecQt    += uiRecQtStride;
        pRecIPred += uiRecIPredStride;
      }
    }
  }

  //===== update distortion =====
  ruiDist += m_pcRdCost->getDistPart( bitDepth, piReco, uiStride, piOrg, uiStride, uiWidth, uiHeight, compID );
}




Void
TEncSearch::xRecurIntraCodingLumaQT(TComYuv*    pcOrgYuv,
                                    TComYuv*    pcPredYuv,
                                    TComYuv*    pcResiYuv,
                                    Pel         resiLuma[NUMBER_OF_STORED_RESIDUAL_TYPES][MAX_CU_SIZE * MAX_CU_SIZE],
                                    Distortion& ruiDistY,
#if HHI_RQT_INTRA_SPEEDUP
                                    Bool        bCheckFirst,
#endif
                                    Double&     dRDCost,
                                    TComTU&     rTu
                                    DEBUG_STRING_FN_DECLARE(sDebug))
{
  TComDataCU   *pcCU          = rTu.getCU();
  const UInt    uiAbsPartIdx  = rTu.GetAbsPartIdxTU();
  const UInt    uiFullDepth   = rTu.GetTransformDepthTotal();
  const UInt    uiTrDepth     = rTu.GetTransformDepthRel();
  const UInt    uiLog2TrSize  = rTu.GetLog2LumaTrSize();
        Bool    bCheckFull    = ( uiLog2TrSize  <= pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() );
        Bool    bCheckSplit   = ( uiLog2TrSize  >  pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) );

        Pel     resiLumaSplit [NUMBER_OF_STORED_RESIDUAL_TYPES][MAX_CU_SIZE * MAX_CU_SIZE];
        Pel     resiLumaSingle[NUMBER_OF_STORED_RESIDUAL_TYPES][MAX_CU_SIZE * MAX_CU_SIZE];

        Bool    bMaintainResidual[NUMBER_OF_STORED_RESIDUAL_TYPES];
        for (UInt residualTypeIndex = 0; residualTypeIndex < NUMBER_OF_STORED_RESIDUAL_TYPES; residualTypeIndex++)
        {
          bMaintainResidual[residualTypeIndex] = true; //assume true unless specified otherwise
        }

        bMaintainResidual[RESIDUAL_ENCODER_SIDE] = !(m_pcEncCfg->getUseReconBasedCrossCPredictionEstimate());

#if HHI_RQT_INTRA_SPEEDUP
  Int maxTuSize = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize();
  Int isIntraSlice = (pcCU->getSlice()->getSliceType() == I_SLICE);
  // don't check split if TU size is less or equal to max TU size
  Bool noSplitIntraMaxTuSize = bCheckFull;
  if(m_pcEncCfg->getRDpenalty() && ! isIntraSlice)
  {
    // in addition don't check split if TU size is less or equal to 16x16 TU size for non-intra slice
    noSplitIntraMaxTuSize = ( uiLog2TrSize  <= min(maxTuSize,4) );

    // if maximum RD-penalty don't check TU size 32x32
    if(m_pcEncCfg->getRDpenalty()==2)
    {
      bCheckFull    = ( uiLog2TrSize  <= min(maxTuSize,4));
    }
  }
  if( bCheckFirst && noSplitIntraMaxTuSize )

  {
    bCheckSplit = false;
  }
#else
  Int maxTuSize = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize();
  Int isIntraSlice = (pcCU->getSlice()->getSliceType() == I_SLICE);
  // if maximum RD-penalty don't check TU size 32x32
  if((m_pcEncCfg->getRDpenalty()==2)  && !isIntraSlice)
  {
    bCheckFull    = ( uiLog2TrSize  <= min(maxTuSize,4));
  }
#endif
  Double     dSingleCost                        = MAX_DOUBLE;
  Distortion uiSingleDistLuma                   = 0;
  UInt       uiSingleCbfLuma                    = 0;
  Bool       checkTransformSkip  = pcCU->getSlice()->getPPS()->getUseTransformSkip();
  Int        bestModeId[MAX_NUM_COMPONENT] = { 0, 0, 0};
  checkTransformSkip           &= TUCompRectHasAssociatedTransformSkipFlag(rTu.getRect(COMPONENT_Y), pcCU->getSlice()->getPPS()->getPpsRangeExtension().getLog2MaxTransformSkipBlockSize());
  checkTransformSkip           &= (!pcCU->getCUTransquantBypass(0));

  assert (rTu.ProcessComponentSection(COMPONENT_Y));
  const UInt totalAdjustedDepthChan   = rTu.GetTransformDepthTotalAdj(COMPONENT_Y);

  if ( m_pcEncCfg->getUseTransformSkipFast() )
  {
    checkTransformSkip       &= (pcCU->getPartitionSize(uiAbsPartIdx)==SIZE_NxN);
  }

  if( bCheckFull )
  {
    if(checkTransformSkip == true)
    {
      //----- store original entropy coding status -----
      m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );

      Distortion singleDistTmpLuma                    = 0;
      UInt       singleCbfTmpLuma                     = 0;
      Double     singleCostTmp                        = 0;
      Int        firstCheckId                         = 0;

      for(Int modeId = firstCheckId; modeId < 2; modeId ++)
      {
        DEBUG_STRING_NEW(sModeString)
        Int  default0Save1Load2 = 0;
        singleDistTmpLuma=0;
        if(modeId == firstCheckId)
        {
          default0Save1Load2 = 1;
        }
        else
        {
          default0Save1Load2 = 2;
        }


        pcCU->setTransformSkipSubParts ( modeId, COMPONENT_Y, uiAbsPartIdx, totalAdjustedDepthChan );
        xIntraCodingTUBlock( pcOrgYuv, pcPredYuv, pcResiYuv, resiLumaSingle, false, singleDistTmpLuma, COMPONENT_Y, rTu DEBUG_STRING_PASS_INTO(sModeString), default0Save1Load2 );

        singleCbfTmpLuma = pcCU->getCbf( uiAbsPartIdx, COMPONENT_Y, uiTrDepth );

        //----- determine rate and r-d cost -----
        if(modeId == 1 && singleCbfTmpLuma == 0)
        {
          //In order not to code TS flag when cbf is zero, the case for TS with cbf being zero is forbidden.
          singleCostTmp = MAX_DOUBLE;
        }
        else
        {
          UInt uiSingleBits = xGetIntraBitsQT( rTu, true, false, false );
          singleCostTmp     = m_pcRdCost->calcRdCost( uiSingleBits, singleDistTmpLuma );
        }
        if(singleCostTmp < dSingleCost)
        {
          DEBUG_STRING_SWAP(sDebug, sModeString)
          dSingleCost   = singleCostTmp;
          uiSingleDistLuma = singleDistTmpLuma;
          uiSingleCbfLuma = singleCbfTmpLuma;

          bestModeId[COMPONENT_Y] = modeId;
          if(bestModeId[COMPONENT_Y] == firstCheckId)
          {
            xStoreIntraResultQT(COMPONENT_Y, rTu );
            m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiFullDepth ][ CI_TEMP_BEST ] );
          }

          if (pcCU->getSlice()->getPPS()->getPpsRangeExtension().getCrossComponentPredictionEnabledFlag())
          {
            const Int xOffset = rTu.getRect( COMPONENT_Y ).x0;
            const Int yOffset = rTu.getRect( COMPONENT_Y ).y0;
            for (UInt storedResidualIndex = 0; storedResidualIndex < NUMBER_OF_STORED_RESIDUAL_TYPES; storedResidualIndex++)
            {
              if (bMaintainResidual[storedResidualIndex])
              {
                xStoreCrossComponentPredictionResult(resiLuma[storedResidualIndex], resiLumaSingle[storedResidualIndex], rTu, xOffset, yOffset, MAX_CU_SIZE, MAX_CU_SIZE);
              }
            }
          }
        }
        if (modeId == firstCheckId)
        {
          m_pcRDGoOnSbacCoder->load ( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
        }
      }

      pcCU ->setTransformSkipSubParts ( bestModeId[COMPONENT_Y], COMPONENT_Y, uiAbsPartIdx, totalAdjustedDepthChan );

      if(bestModeId[COMPONENT_Y] == firstCheckId)
      {
        xLoadIntraResultQT(COMPONENT_Y, rTu );
        pcCU->setCbfSubParts  ( uiSingleCbfLuma << uiTrDepth, COMPONENT_Y, uiAbsPartIdx, rTu.GetTransformDepthTotalAdj(COMPONENT_Y) );

        m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiFullDepth ][ CI_TEMP_BEST ] );
      }
    }
    else
    {
      //----- store original entropy coding status -----
      if( bCheckSplit )
      {
        m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
      }
      //----- code luma/chroma block with given intra prediction mode and store Cbf-----
      dSingleCost   = 0.0;

      pcCU ->setTransformSkipSubParts ( 0, COMPONENT_Y, uiAbsPartIdx, totalAdjustedDepthChan );
      xIntraCodingTUBlock( pcOrgYuv, pcPredYuv, pcResiYuv, resiLumaSingle, false, uiSingleDistLuma, COMPONENT_Y, rTu DEBUG_STRING_PASS_INTO(sDebug));

      if( bCheckSplit )
      {
        uiSingleCbfLuma = pcCU->getCbf( uiAbsPartIdx, COMPONENT_Y, uiTrDepth );
      }
      //----- determine rate and r-d cost -----
      UInt uiSingleBits = xGetIntraBitsQT( rTu, true, false, false );

      if(m_pcEncCfg->getRDpenalty() && (uiLog2TrSize==5) && !isIntraSlice)
      {
        uiSingleBits=uiSingleBits*4;
      }

      dSingleCost       = m_pcRdCost->calcRdCost( uiSingleBits, uiSingleDistLuma );

      if (pcCU->getSlice()->getPPS()->getPpsRangeExtension().getCrossComponentPredictionEnabledFlag())
      {
        const Int xOffset = rTu.getRect( COMPONENT_Y ).x0;
        const Int yOffset = rTu.getRect( COMPONENT_Y ).y0;
        for (UInt storedResidualIndex = 0; storedResidualIndex < NUMBER_OF_STORED_RESIDUAL_TYPES; storedResidualIndex++)
        {
          if (bMaintainResidual[storedResidualIndex])
          {
            xStoreCrossComponentPredictionResult(resiLuma[storedResidualIndex], resiLumaSingle[storedResidualIndex], rTu, xOffset, yOffset, MAX_CU_SIZE, MAX_CU_SIZE);
          }
        }
      }
    }
  }

  if( bCheckSplit )
  {
    //----- store full entropy coding status, load original entropy coding status -----
    if( bCheckFull )
    {
      m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_TEST ] );
      m_pcRDGoOnSbacCoder->load ( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
    }
    else
    {
      m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
    }
    //----- code splitted block -----
    Double     dSplitCost      = 0.0;
    Distortion uiSplitDistLuma = 0;
    UInt       uiSplitCbfLuma  = 0;

    TComTURecurse tuRecurseChild(rTu, false);
    DEBUG_STRING_NEW(sSplit)
    do
    {
      DEBUG_STRING_NEW(sChild)
#if HHI_RQT_INTRA_SPEEDUP
      xRecurIntraCodingLumaQT( pcOrgYuv, pcPredYuv, pcResiYuv, resiLumaSplit, uiSplitDistLuma, bCheckFirst, dSplitCost, tuRecurseChild DEBUG_STRING_PASS_INTO(sChild) );
#else
      xRecurIntraCodingLumaQT( pcOrgYuv, pcPredYuv, pcResiYuv, resiLumaSplit, uiSplitDistLuma, dSplitCost, tuRecurseChild DEBUG_STRING_PASS_INTO(sChild) );
#endif
      DEBUG_STRING_APPEND(sSplit, sChild)
      uiSplitCbfLuma |= pcCU->getCbf( tuRecurseChild.GetAbsPartIdxTU(), COMPONENT_Y, tuRecurseChild.GetTransformDepthRel() );
    } while (tuRecurseChild.nextSection(rTu) );

    UInt    uiPartsDiv     = rTu.GetAbsPartIdxNumParts();
    {
      if (uiSplitCbfLuma)
      {
        const UInt flag=1<<uiTrDepth;
        UChar *pBase=pcCU->getCbf( COMPONENT_Y );
        for( UInt uiOffs = 0; uiOffs < uiPartsDiv; uiOffs++ )
        {
          pBase[ uiAbsPartIdx + uiOffs ] |= flag;
        }
      }
    }
    //----- restore context states -----
    m_pcRDGoOnSbacCoder->load ( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
    
    //----- determine rate and r-d cost -----
    UInt uiSplitBits = xGetIntraBitsQT( rTu, true, false, false );
    dSplitCost       = m_pcRdCost->calcRdCost( uiSplitBits, uiSplitDistLuma );

    //===== compare and set best =====
    if( dSplitCost < dSingleCost )
    {
      //--- update cost ---
      DEBUG_STRING_SWAP(sSplit, sDebug)
      ruiDistY += uiSplitDistLuma;
      dRDCost  += dSplitCost;

      if (pcCU->getSlice()->getPPS()->getPpsRangeExtension().getCrossComponentPredictionEnabledFlag())
      {
        const Int xOffset = rTu.getRect( COMPONENT_Y ).x0;
        const Int yOffset = rTu.getRect( COMPONENT_Y ).y0;
        for (UInt storedResidualIndex = 0; storedResidualIndex < NUMBER_OF_STORED_RESIDUAL_TYPES; storedResidualIndex++)
        {
          if (bMaintainResidual[storedResidualIndex])
          {
            xStoreCrossComponentPredictionResult(resiLuma[storedResidualIndex], resiLumaSplit[storedResidualIndex], rTu, xOffset, yOffset, MAX_CU_SIZE, MAX_CU_SIZE);
          }
        }
      }

      return;
    }

    //----- set entropy coding status -----
    m_pcRDGoOnSbacCoder->load ( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_TEST ] );

    //--- set transform index and Cbf values ---
    pcCU->setTrIdxSubParts( uiTrDepth, uiAbsPartIdx, uiFullDepth );
    const TComRectangle &tuRect=rTu.getRect(COMPONENT_Y);
    pcCU->setCbfSubParts  ( uiSingleCbfLuma << uiTrDepth, COMPONENT_Y, uiAbsPartIdx, totalAdjustedDepthChan );
    pcCU ->setTransformSkipSubParts  ( bestModeId[COMPONENT_Y], COMPONENT_Y, uiAbsPartIdx, totalAdjustedDepthChan );

    //--- set reconstruction for next intra prediction blocks ---
    const UInt  uiQTLayer   = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;
    const UInt  uiZOrder    = pcCU->getZorderIdxInCtu() + uiAbsPartIdx;
    const UInt  uiWidth     = tuRect.width;
    const UInt  uiHeight    = tuRect.height;
    Pel*  piSrc       = m_pcQTTempTComYuv[ uiQTLayer ].getAddr( COMPONENT_Y, uiAbsPartIdx );
    UInt  uiSrcStride = m_pcQTTempTComYuv[ uiQTLayer ].getStride  ( COMPONENT_Y );
    Pel*  piDes       = pcCU->getPic()->getPicYuvRec()->getAddr( COMPONENT_Y, pcCU->getCtuRsAddr(), uiZOrder );
    UInt  uiDesStride = pcCU->getPic()->getPicYuvRec()->getStride  ( COMPONENT_Y );

    for( UInt uiY = 0; uiY < uiHeight; uiY++, piSrc += uiSrcStride, piDes += uiDesStride )
    {
      for( UInt uiX = 0; uiX < uiWidth; uiX++ )
      {
        piDes[ uiX ] = piSrc[ uiX ];
      }
    }
  }
  ruiDistY += uiSingleDistLuma;
  dRDCost  += dSingleCost;
}


Void
TEncSearch::xSetIntraResultLumaQT(TComYuv* pcRecoYuv, TComTU &rTu)
{
  TComDataCU *pcCU        = rTu.getCU();
  const UInt uiTrDepth    = rTu.GetTransformDepthRel();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  UInt uiTrMode     = pcCU->getTransformIdx( uiAbsPartIdx );
  if(  uiTrMode == uiTrDepth )
  {
    UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();
    UInt uiQTLayer    = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;

    //===== copy transform coefficients =====

    const TComRectangle &tuRect=rTu.getRect(COMPONENT_Y);
    const UInt coeffOffset = rTu.getCoefficientOffset(COMPONENT_Y);
    const UInt numCoeffInBlock = tuRect.width * tuRect.height;

    if (numCoeffInBlock!=0)
    {
      const TCoeff* srcCoeff = m_ppcQTTempCoeff[COMPONENT_Y][uiQTLayer] + coeffOffset;
      TCoeff* destCoeff      = pcCU->getCoeff(COMPONENT_Y) + coeffOffset;
      ::memcpy( destCoeff, srcCoeff, sizeof(TCoeff)*numCoeffInBlock );
#if ADAPTIVE_QP_SELECTION
      const TCoeff* srcArlCoeff = m_ppcQTTempArlCoeff[COMPONENT_Y][ uiQTLayer ] + coeffOffset;
      TCoeff* destArlCoeff      = pcCU->getArlCoeff (COMPONENT_Y)               + coeffOffset;
      ::memcpy( destArlCoeff, srcArlCoeff, sizeof( TCoeff ) * numCoeffInBlock );
#endif
      m_pcQTTempTComYuv[ uiQTLayer ].copyPartToPartComponent( COMPONENT_Y, pcRecoYuv, uiAbsPartIdx, tuRect.width, tuRect.height );
    }

  }
  else
  {
    TComTURecurse tuRecurseChild(rTu, false);
    do
    {
      xSetIntraResultLumaQT( pcRecoYuv, tuRecurseChild );
    } while (tuRecurseChild.nextSection(rTu));
  }
}


Void
TEncSearch::xStoreIntraResultQT(const ComponentID compID, TComTU &rTu )
{
  TComDataCU *pcCU=rTu.getCU();
  const UInt uiTrDepth = rTu.GetTransformDepthRel();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  const UInt uiTrMode     = pcCU->getTransformIdx( uiAbsPartIdx );
  if ( compID==COMPONENT_Y || uiTrMode == uiTrDepth )
  {
    assert(uiTrMode == uiTrDepth);
    const UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();
    const UInt uiQTLayer    = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;

    if (rTu.ProcessComponentSection(compID))
    {
      const TComRectangle &tuRect=rTu.getRect(compID);

      //===== copy transform coefficients =====
      const UInt uiNumCoeff    = tuRect.width * tuRect.height;
      TCoeff* pcCoeffSrc = m_ppcQTTempCoeff[compID] [ uiQTLayer ] + rTu.getCoefficientOffset(compID);
      TCoeff* pcCoeffDst = m_pcQTTempTUCoeff[compID];

      ::memcpy( pcCoeffDst, pcCoeffSrc, sizeof( TCoeff ) * uiNumCoeff );
#if ADAPTIVE_QP_SELECTION
      TCoeff* pcArlCoeffSrc = m_ppcQTTempArlCoeff[compID] [ uiQTLayer ] + rTu.getCoefficientOffset(compID);
      TCoeff* pcArlCoeffDst = m_ppcQTTempTUArlCoeff[compID];
      ::memcpy( pcArlCoeffDst, pcArlCoeffSrc, sizeof( TCoeff ) * uiNumCoeff );
#endif
      //===== copy reconstruction =====
      m_pcQTTempTComYuv[ uiQTLayer ].copyPartToPartComponent( compID, &m_pcQTTempTransformSkipTComYuv, uiAbsPartIdx, tuRect.width, tuRect.height );
    }
  }
}


Void
TEncSearch::xLoadIntraResultQT(const ComponentID compID, TComTU &rTu)
{
  TComDataCU *pcCU=rTu.getCU();
  const UInt uiTrDepth = rTu.GetTransformDepthRel();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  const UInt uiTrMode     = pcCU->getTransformIdx( uiAbsPartIdx );
  if ( compID==COMPONENT_Y || uiTrMode == uiTrDepth )
  {
    assert(uiTrMode == uiTrDepth);
    const UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();
    const UInt uiQTLayer    = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;
    const UInt uiZOrder     = pcCU->getZorderIdxInCtu() + uiAbsPartIdx;

    if (rTu.ProcessComponentSection(compID))
    {
      const TComRectangle &tuRect=rTu.getRect(compID);

      //===== copy transform coefficients =====
      const UInt uiNumCoeff = tuRect.width * tuRect.height;
      TCoeff* pcCoeffDst = m_ppcQTTempCoeff[compID] [ uiQTLayer ] + rTu.getCoefficientOffset(compID);
      TCoeff* pcCoeffSrc = m_pcQTTempTUCoeff[compID];

      ::memcpy( pcCoeffDst, pcCoeffSrc, sizeof( TCoeff ) * uiNumCoeff );
#if ADAPTIVE_QP_SELECTION
      TCoeff* pcArlCoeffDst = m_ppcQTTempArlCoeff[compID] [ uiQTLayer ] + rTu.getCoefficientOffset(compID);
      TCoeff* pcArlCoeffSrc = m_ppcQTTempTUArlCoeff[compID];
      ::memcpy( pcArlCoeffDst, pcArlCoeffSrc, sizeof( TCoeff ) * uiNumCoeff );
#endif
      //===== copy reconstruction =====
      m_pcQTTempTransformSkipTComYuv.copyPartToPartComponent( compID, &m_pcQTTempTComYuv[ uiQTLayer ], uiAbsPartIdx, tuRect.width, tuRect.height );

      Pel*    piRecIPred        = pcCU->getPic()->getPicYuvRec()->getAddr( compID, pcCU->getCtuRsAddr(), uiZOrder );
      UInt    uiRecIPredStride  = pcCU->getPic()->getPicYuvRec()->getStride (compID);
      Pel*    piRecQt           = m_pcQTTempTComYuv[ uiQTLayer ].getAddr( compID, uiAbsPartIdx );
      UInt    uiRecQtStride     = m_pcQTTempTComYuv[ uiQTLayer ].getStride  (compID);
      UInt    uiWidth           = tuRect.width;
      UInt    uiHeight          = tuRect.height;
      Pel* pRecQt               = piRecQt;
      Pel* pRecIPred            = piRecIPred;
      for( UInt uiY = 0; uiY < uiHeight; uiY++ )
      {
        for( UInt uiX = 0; uiX < uiWidth; uiX++ )
        {
          pRecIPred[ uiX ] = pRecQt   [ uiX ];
        }
        pRecQt    += uiRecQtStride;
        pRecIPred += uiRecIPredStride;
      }
    }
  }
}

Void
TEncSearch::xStoreCrossComponentPredictionResult(       Pel    *pResiDst,
                                                  const Pel    *pResiSrc,
                                                        TComTU &rTu,
                                                  const Int     xOffset,
                                                  const Int     yOffset,
                                                  const Int     strideDst,
                                                  const Int     strideSrc )
{
  const Pel *pSrc = pResiSrc + yOffset * strideSrc + xOffset;
        Pel *pDst = pResiDst + yOffset * strideDst + xOffset;

  for( Int y = 0; y < rTu.getRect( COMPONENT_Y ).height; y++ )
  {
    ::memcpy( pDst, pSrc, sizeof(Pel) * rTu.getRect( COMPONENT_Y ).width );
    pDst += strideDst;
    pSrc += strideSrc;
  }
}

SChar
TEncSearch::xCalcCrossComponentPredictionAlpha(       TComTU &rTu,
                                                const ComponentID compID,
                                                const Pel*        piResiL,
                                                const Pel*        piResiC,
                                                const Int         width,
                                                const Int         height,
                                                const Int         strideL,
                                                const Int         strideC )
{
  const Pel *pResiL = piResiL;
  const Pel *pResiC = piResiC;

        TComDataCU *pCU = rTu.getCU();
  const Int  absPartIdx = rTu.GetAbsPartIdxTU( compID );
  const Int diffBitDepth = pCU->getSlice()->getSPS()->getDifferentialLumaChromaBitDepth();

  SChar alpha = 0;
  Int SSxy  = 0;
  Int SSxx  = 0;

  for( UInt uiY = 0; uiY < height; uiY++ )
  {
    for( UInt uiX = 0; uiX < width; uiX++ )
    {
      const Pel scaledResiL = rightShift( pResiL[ uiX ], diffBitDepth );
      SSxy += ( scaledResiL * pResiC[ uiX ] );
      SSxx += ( scaledResiL * scaledResiL   );
    }

    pResiL += strideL;
    pResiC += strideC;
  }

  if( SSxx != 0 )
  {
    Double dAlpha = SSxy / Double( SSxx );
    alpha = SChar(Clip3<Int>(-16, 16, (Int)(dAlpha * 16)));

    static const SChar alphaQuant[17] = {0, 1, 1, 2, 2, 2, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8};

    alpha = (alpha < 0) ? -alphaQuant[Int(-alpha)] : alphaQuant[Int(alpha)];
  }
  pCU->setCrossComponentPredictionAlphaPartRange( alpha, compID, absPartIdx, rTu.GetAbsPartIdxNumParts( compID ) );

  return alpha;
}

Void
TEncSearch::xRecurIntraChromaCodingQT(TComYuv*    pcOrgYuv,
                                      TComYuv*    pcPredYuv,
                                      TComYuv*    pcResiYuv,
                                      Pel         resiLuma[NUMBER_OF_STORED_RESIDUAL_TYPES][MAX_CU_SIZE * MAX_CU_SIZE],
                                      Distortion& ruiDist,
                                      TComTU&     rTu
                                      DEBUG_STRING_FN_DECLARE(sDebug))
{
  TComDataCU         *pcCU                  = rTu.getCU();
  const UInt          uiTrDepth             = rTu.GetTransformDepthRel();
  const UInt          uiAbsPartIdx          = rTu.GetAbsPartIdxTU();
  const ChromaFormat  format                = rTu.GetChromaFormat();
  UInt                uiTrMode              = pcCU->getTransformIdx( uiAbsPartIdx );
  const UInt          numberValidComponents = getNumberValidComponents(format);

  if(  uiTrMode == uiTrDepth )
  {
    if (!rTu.ProcessChannelSection(CHANNEL_TYPE_CHROMA))
    {
      return;
    }

    const UInt uiFullDepth = rTu.GetTransformDepthTotal();

    Bool checkTransformSkip = pcCU->getSlice()->getPPS()->getUseTransformSkip();
    checkTransformSkip &= TUCompRectHasAssociatedTransformSkipFlag(rTu.getRect(COMPONENT_Cb), pcCU->getSlice()->getPPS()->getPpsRangeExtension().getLog2MaxTransformSkipBlockSize());

    if ( m_pcEncCfg->getUseTransformSkipFast() )
    {
      checkTransformSkip &= TUCompRectHasAssociatedTransformSkipFlag(rTu.getRect(COMPONENT_Y), pcCU->getSlice()->getPPS()->getPpsRangeExtension().getLog2MaxTransformSkipBlockSize());

      if (checkTransformSkip)
      {
        Int nbLumaSkip = 0;
        const UInt maxAbsPartIdxSub=uiAbsPartIdx + (rTu.ProcessingAllQuadrants(COMPONENT_Cb)?1:4);
        for(UInt absPartIdxSub = uiAbsPartIdx; absPartIdxSub < maxAbsPartIdxSub; absPartIdxSub ++)
        {
          nbLumaSkip += pcCU->getTransformSkip(absPartIdxSub, COMPONENT_Y);
        }
        checkTransformSkip &= (nbLumaSkip > 0);
      }
    }


    for (UInt ch=COMPONENT_Cb; ch<numberValidComponents; ch++)
    {
      const ComponentID compID = ComponentID(ch);
      DEBUG_STRING_NEW(sDebugBestMode)

      //use RDO to decide whether Cr/Cb takes TS
      m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[uiFullDepth][CI_QT_TRAFO_ROOT] );

      const Bool splitIntoSubTUs = rTu.getRect(compID).width != rTu.getRect(compID).height;

      TComTURecurse TUIterator(rTu, false, (splitIntoSubTUs ? TComTU::VERTICAL_SPLIT : TComTU::DONT_SPLIT), true, compID);

      const UInt partIdxesPerSubTU = TUIterator.GetAbsPartIdxNumParts(compID);

      do
      {
        const UInt subTUAbsPartIdx   = TUIterator.GetAbsPartIdxTU(compID);

        Double     dSingleCost               = MAX_DOUBLE;
        Int        bestModeId                = 0;
        Distortion singleDistC               = 0;
        UInt       singleCbfC                = 0;
        Distortion singleDistCTmp            = 0;
        Double     singleCostTmp             = 0;
        UInt       singleCbfCTmp             = 0;
        SChar      bestCrossCPredictionAlpha = 0;
        Int        bestTransformSkipMode     = 0;

        const Bool checkCrossComponentPrediction =    (pcCU->getIntraDir(CHANNEL_TYPE_CHROMA, subTUAbsPartIdx) == DM_CHROMA_IDX)
                                                   &&  pcCU->getSlice()->getPPS()->getPpsRangeExtension().getCrossComponentPredictionEnabledFlag()
                                                   && (pcCU->getCbf(subTUAbsPartIdx,  COMPONENT_Y, uiTrDepth) != 0);

        const Int  crossCPredictionModesToTest = checkCrossComponentPrediction ? 2 : 1;
        const Int  transformSkipModesToTest    = checkTransformSkip            ? 2 : 1;
        const Int  totalModesToTest            = crossCPredictionModesToTest * transformSkipModesToTest;
              Int  currModeId                  = 0;
              Int  default0Save1Load2          = 0;

        for(Int transformSkipModeId = 0; transformSkipModeId < transformSkipModesToTest; transformSkipModeId++)
        {
          for(Int crossCPredictionModeId = 0; crossCPredictionModeId < crossCPredictionModesToTest; crossCPredictionModeId++)
          {
            pcCU->setCrossComponentPredictionAlphaPartRange(0, compID, subTUAbsPartIdx, partIdxesPerSubTU);
            DEBUG_STRING_NEW(sDebugMode)
            pcCU->setTransformSkipPartRange( transformSkipModeId, compID, subTUAbsPartIdx, partIdxesPerSubTU );
            currModeId++;

            const Bool isOneMode  = (totalModesToTest == 1);
            const Bool isLastMode = (currModeId == totalModesToTest); // currModeId is indexed from 1

            if (isOneMode)
            {
              default0Save1Load2 = 0;
            }
            else if (!isOneMode && (transformSkipModeId == 0) && (crossCPredictionModeId == 0))
            {
              default0Save1Load2 = 1; //save prediction on first mode
            }
            else
            {
              default0Save1Load2 = 2; //load it on subsequent modes
            }

            singleDistCTmp = 0;

            xIntraCodingTUBlock( pcOrgYuv, pcPredYuv, pcResiYuv, resiLuma, (crossCPredictionModeId != 0), singleDistCTmp, compID, TUIterator DEBUG_STRING_PASS_INTO(sDebugMode), default0Save1Load2);
            singleCbfCTmp = pcCU->getCbf( subTUAbsPartIdx, compID, uiTrDepth);

            if (  ((crossCPredictionModeId == 1) && (pcCU->getCrossComponentPredictionAlpha(subTUAbsPartIdx, compID) == 0))
               || ((transformSkipModeId    == 1) && (singleCbfCTmp == 0))) //In order not to code TS flag when cbf is zero, the case for TS with cbf being zero is forbidden.
            {
              singleCostTmp = MAX_DOUBLE;
            }
            else if (!isOneMode)
            {
              UInt bitsTmp = xGetIntraBitsQTChroma( TUIterator, compID, false );
              singleCostTmp  = m_pcRdCost->calcRdCost( bitsTmp, singleDistCTmp);
            }

            if(singleCostTmp < dSingleCost)
            {
              DEBUG_STRING_SWAP(sDebugBestMode, sDebugMode)
              dSingleCost               = singleCostTmp;
              singleDistC               = singleDistCTmp;
              bestCrossCPredictionAlpha = (crossCPredictionModeId != 0) ? pcCU->getCrossComponentPredictionAlpha(subTUAbsPartIdx, compID) : 0;
              bestTransformSkipMode     = transformSkipModeId;
              bestModeId                = currModeId;
              singleCbfC                = singleCbfCTmp;

              if (!isOneMode && !isLastMode)
              {
                xStoreIntraResultQT(compID, TUIterator);
                m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiFullDepth ][ CI_TEMP_BEST ] );
              }
            }

            if (!isOneMode && !isLastMode)
            {
              m_pcRDGoOnSbacCoder->load ( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
            }
          }
        }

        if(bestModeId < totalModesToTest)
        {
          xLoadIntraResultQT(compID, TUIterator);
          pcCU->setCbfPartRange( singleCbfC << uiTrDepth, compID, subTUAbsPartIdx, partIdxesPerSubTU );

          m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiFullDepth ][ CI_TEMP_BEST ] );
        }

        DEBUG_STRING_APPEND(sDebug, sDebugBestMode)
        pcCU ->setTransformSkipPartRange                ( bestTransformSkipMode,     compID, subTUAbsPartIdx, partIdxesPerSubTU );
        pcCU ->setCrossComponentPredictionAlphaPartRange( bestCrossCPredictionAlpha, compID, subTUAbsPartIdx, partIdxesPerSubTU );
        ruiDist += singleDistC;
      } while (TUIterator.nextSection(rTu));

      if (splitIntoSubTUs)
      {
        offsetSubTUCBFs(rTu, compID);
      }
    }
  }
  else
  {
    UInt    uiSplitCbf[MAX_NUM_COMPONENT] = {0,0,0};

    TComTURecurse tuRecurseChild(rTu, false);
    const UInt uiTrDepthChild   = tuRecurseChild.GetTransformDepthRel();
    do
    {
      DEBUG_STRING_NEW(sChild)

      xRecurIntraChromaCodingQT( pcOrgYuv, pcPredYuv, pcResiYuv, resiLuma, ruiDist, tuRecurseChild DEBUG_STRING_PASS_INTO(sChild) );

      DEBUG_STRING_APPEND(sDebug, sChild)
      const UInt uiAbsPartIdxSub=tuRecurseChild.GetAbsPartIdxTU();

      for(UInt ch=COMPONENT_Cb; ch<numberValidComponents; ch++)
      {
        uiSplitCbf[ch] |= pcCU->getCbf( uiAbsPartIdxSub, ComponentID(ch), uiTrDepthChild );
      }
    } while ( tuRecurseChild.nextSection(rTu) );


    UInt uiPartsDiv = rTu.GetAbsPartIdxNumParts();
    for(UInt ch=COMPONENT_Cb; ch<numberValidComponents; ch++)
    {
      if (uiSplitCbf[ch])
      {
        const UInt flag=1<<uiTrDepth;
        ComponentID compID=ComponentID(ch);
        UChar *pBase=pcCU->getCbf( compID );
        for( UInt uiOffs = 0; uiOffs < uiPartsDiv; uiOffs++ )
        {
          pBase[ uiAbsPartIdx + uiOffs ] |= flag;
        }
      }
    }
  }
}




Void
TEncSearch::xSetIntraResultChromaQT(TComYuv*    pcRecoYuv, TComTU &rTu)
{
  if (!rTu.ProcessChannelSection(CHANNEL_TYPE_CHROMA))
  {
    return;
  }
  TComDataCU *pcCU=rTu.getCU();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  const UInt uiTrDepth   = rTu.GetTransformDepthRel();
  UInt uiTrMode     = pcCU->getTransformIdx( uiAbsPartIdx );
  if(  uiTrMode == uiTrDepth )
  {
    UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();
    UInt uiQTLayer    = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;

    //===== copy transform coefficients =====
    const TComRectangle &tuRectCb=rTu.getRect(COMPONENT_Cb);
    UInt uiNumCoeffC    = tuRectCb.width*tuRectCb.height;//( pcCU->getSlice()->getSPS()->getMaxCUWidth() * pcCU->getSlice()->getSPS()->getMaxCUHeight() ) >> ( uiFullDepth << 1 );
    const UInt offset = rTu.getCoefficientOffset(COMPONENT_Cb);

    const UInt numberValidComponents = getNumberValidComponents(rTu.GetChromaFormat());
    for (UInt ch=COMPONENT_Cb; ch<numberValidComponents; ch++)
    {
      const ComponentID component = ComponentID(ch);
      const TCoeff* src           = m_ppcQTTempCoeff[component][uiQTLayer] + offset;//(uiNumCoeffIncC*uiAbsPartIdx);
      TCoeff* dest                = pcCU->getCoeff(component) + offset;//(uiNumCoeffIncC*uiAbsPartIdx);
      ::memcpy( dest, src, sizeof(TCoeff)*uiNumCoeffC );
#if ADAPTIVE_QP_SELECTION
      TCoeff* pcArlCoeffSrc = m_ppcQTTempArlCoeff[component][ uiQTLayer ] + offset;//( uiNumCoeffIncC * uiAbsPartIdx );
      TCoeff* pcArlCoeffDst = pcCU->getArlCoeff(component)                + offset;//( uiNumCoeffIncC * uiAbsPartIdx );
      ::memcpy( pcArlCoeffDst, pcArlCoeffSrc, sizeof( TCoeff ) * uiNumCoeffC );
#endif
    }

    //===== copy reconstruction =====

    m_pcQTTempTComYuv[ uiQTLayer ].copyPartToPartComponent( COMPONENT_Cb, pcRecoYuv, uiAbsPartIdx, tuRectCb.width, tuRectCb.height );
    m_pcQTTempTComYuv[ uiQTLayer ].copyPartToPartComponent( COMPONENT_Cr, pcRecoYuv, uiAbsPartIdx, tuRectCb.width, tuRectCb.height );
  }
  else
  {
    TComTURecurse tuRecurseChild(rTu, false);
    do
    {
      xSetIntraResultChromaQT( pcRecoYuv, tuRecurseChild );
    } while (tuRecurseChild.nextSection(rTu));
  }
}



Void
TEncSearch::estIntraPredLumaQT(TComDataCU* pcCU,
                               TComYuv*    pcOrgYuv,
                               TComYuv*    pcPredYuv,
                               TComYuv*    pcResiYuv,
                               TComYuv*    pcRecoYuv,
                               Pel         resiLuma[NUMBER_OF_STORED_RESIDUAL_TYPES][MAX_CU_SIZE * MAX_CU_SIZE]
                               DEBUG_STRING_FN_DECLARE(sDebug))
{
  const UInt         uiDepth               = pcCU->getDepth(0);
  const UInt         uiInitTrDepth         = pcCU->getPartitionSize(0) == SIZE_2Nx2N ? 0 : 1;
  const UInt         uiNumPU               = 1<<(2*uiInitTrDepth);
  const UInt         uiQNumParts           = pcCU->getTotalNumPart() >> 2;
  const UInt         uiWidthBit            = pcCU->getIntraSizeIdx(0);
  const ChromaFormat chFmt                 = pcCU->getPic()->getChromaFormat();
  const UInt         numberValidComponents = getNumberValidComponents(chFmt);
  const TComSPS     &sps                   = *(pcCU->getSlice()->getSPS());
  const TComPPS     &pps                   = *(pcCU->getSlice()->getPPS());
        Distortion   uiOverallDistY        = 0;
        UInt         CandNum;
        Double       CandCostList[ FAST_UDI_MAX_RDMODE_NUM ];
        Pel          resiLumaPU[NUMBER_OF_STORED_RESIDUAL_TYPES][MAX_CU_SIZE * MAX_CU_SIZE];

        Bool    bMaintainResidual[NUMBER_OF_STORED_RESIDUAL_TYPES];
        for (UInt residualTypeIndex = 0; residualTypeIndex < NUMBER_OF_STORED_RESIDUAL_TYPES; residualTypeIndex++)
        {
          bMaintainResidual[residualTypeIndex] = true; //assume true unless specified otherwise
        }

        bMaintainResidual[RESIDUAL_ENCODER_SIDE] = !(m_pcEncCfg->getUseReconBasedCrossCPredictionEstimate());

  // Lambda calculation at equivalent Qp of 4 is recommended because at that Qp, the quantisation divisor is 1.
#if FULL_NBIT
  const Double sqrtLambdaForFirstPass= (m_pcEncCfg->getCostMode()==COST_MIXED_LOSSLESS_LOSSY_CODING && pcCU->getCUTransquantBypass(0)) ?
                sqrt(0.57 * pow(2.0, ((LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_TEST_QP_PRIME - 12) / 3.0)))
              : m_pcRdCost->getSqrtLambda();
#else
  const Double sqrtLambdaForFirstPass= (m_pcEncCfg->getCostMode()==COST_MIXED_LOSSLESS_LOSSY_CODING && pcCU->getCUTransquantBypass(0)) ?
                sqrt(0.57 * pow(2.0, ((LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_TEST_QP_PRIME - 12 - 6 * (sps.getBitDepth(CHANNEL_TYPE_LUMA) - 8)) / 3.0)))
              : m_pcRdCost->getSqrtLambda();
#endif

  //===== set QP and clear Cbf =====
  if ( pps.getUseDQP() == true)
  {
    pcCU->setQPSubParts( pcCU->getQP(0), 0, uiDepth );
  }
  else
  {
    pcCU->setQPSubParts( pcCU->getSlice()->getSliceQp(), 0, uiDepth );
  }

  //===== loop over partitions =====
  TComTURecurse tuRecurseCU(pcCU, 0);
  TComTURecurse tuRecurseWithPU(tuRecurseCU, false, (uiInitTrDepth==0)?TComTU::DONT_SPLIT : TComTU::QUAD_SPLIT);

  do
  {
    const UInt uiPartOffset=tuRecurseWithPU.GetAbsPartIdxTU();
//  for( UInt uiPU = 0, uiPartOffset=0; uiPU < uiNumPU; uiPU++, uiPartOffset += uiQNumParts )
  //{
    //===== init pattern for luma prediction =====
    DEBUG_STRING_NEW(sTemp2)

    //===== determine set of modes to be tested (using prediction signal only) =====
    Int numModesAvailable     = 35; //total number of Intra modes
    UInt uiRdModeList[FAST_UDI_MAX_RDMODE_NUM];
    Int numModesForFullRD = m_pcEncCfg->getFastUDIUseMPMEnabled()?g_aucIntraModeNumFast_UseMPM[ uiWidthBit ] : g_aucIntraModeNumFast_NotUseMPM[ uiWidthBit ];

    // this should always be true
    assert (tuRecurseWithPU.ProcessComponentSection(COMPONENT_Y));
    initIntraPatternChType( tuRecurseWithPU, COMPONENT_Y, true DEBUG_STRING_PASS_INTO(sTemp2) );

    Bool doFastSearch = (numModesForFullRD != numModesAvailable);
    if (doFastSearch)
    {
      assert(numModesForFullRD < numModesAvailable);

      for( Int i=0; i < numModesForFullRD; i++ )
      {
        CandCostList[ i ] = MAX_DOUBLE;
      }
      CandNum = 0;

      const TComRectangle &puRect=tuRecurseWithPU.getRect(COMPONENT_Y);
      const UInt uiAbsPartIdx=tuRecurseWithPU.GetAbsPartIdxTU();

      Pel* piOrg         = pcOrgYuv ->getAddr( COMPONENT_Y, uiAbsPartIdx );
      Pel* piPred        = pcPredYuv->getAddr( COMPONENT_Y, uiAbsPartIdx );
      UInt uiStride      = pcPredYuv->getStride( COMPONENT_Y );
      DistParam distParam;
      const Bool bUseHadamard=pcCU->getCUTransquantBypass(0) == 0;
      m_pcRdCost->setDistParam(distParam, sps.getBitDepth(CHANNEL_TYPE_LUMA), piOrg, uiStride, piPred, uiStride, puRect.width, puRect.height, bUseHadamard);
      distParam.bApplyWeight = false;
      for( Int modeIdx = 0; modeIdx < numModesAvailable; modeIdx++ )
      {
        UInt       uiMode = modeIdx;
        Distortion uiSad  = 0;

        const Bool bUseFilter=TComPrediction::filteringIntraReferenceSamples(COMPONENT_Y, uiMode, puRect.width, puRect.height, chFmt, sps.getSpsRangeExtension().getIntraSmoothingDisabledFlag());

        predIntraAng( COMPONENT_Y, uiMode, piOrg, uiStride, piPred, uiStride, tuRecurseWithPU, bUseFilter, TComPrediction::UseDPCMForFirstPassIntraEstimation(tuRecurseWithPU, uiMode) );

        // use hadamard transform here
        uiSad+=distParam.DistFunc(&distParam);

        UInt   iModeBits = 0;

        // NB xModeBitsIntra will not affect the mode for chroma that may have already been pre-estimated.
        iModeBits+=xModeBitsIntra( pcCU, uiMode, uiPartOffset, uiDepth, CHANNEL_TYPE_LUMA );

        Double cost      = (Double)uiSad + (Double)iModeBits * sqrtLambdaForFirstPass;

#if DEBUG_INTRA_SEARCH_COSTS
        std::cout << "1st pass mode " << uiMode << " SAD = " << uiSad << ", mode bits = " << iModeBits << ", cost = " << cost << "\n";
#endif

        CandNum += xUpdateCandList( uiMode, cost, numModesForFullRD, uiRdModeList, CandCostList );
      }

      if (m_pcEncCfg->getFastUDIUseMPMEnabled())
      {
        Int uiPreds[NUM_MOST_PROBABLE_MODES] = {-1, -1, -1};

        Int iMode = -1;
        pcCU->getIntraDirPredictor( uiPartOffset, uiPreds, COMPONENT_Y, &iMode );

        const Int numCand = ( iMode >= 0 ) ? iMode : Int(NUM_MOST_PROBABLE_MODES);

        for( Int j=0; j < numCand; j++)
        {
          Bool mostProbableModeIncluded = false;
          Int mostProbableMode = uiPreds[j];

          for( Int i=0; i < numModesForFullRD; i++)
          {
            mostProbableModeIncluded |= (mostProbableMode == uiRdModeList[i]);
          }
          if (!mostProbableModeIncluded)
          {
            uiRdModeList[numModesForFullRD++] = mostProbableMode;
          }
        }
      }
    }
    else
    {
      for( Int i=0; i < numModesForFullRD; i++)
      {
        uiRdModeList[i] = i;
      }
    }

    //===== check modes (using r-d costs) =====
#if HHI_RQT_INTRA_SPEEDUP_MOD
    UInt   uiSecondBestMode  = MAX_UINT;
    Double dSecondBestPUCost = MAX_DOUBLE;
#endif
    DEBUG_STRING_NEW(sPU)
    UInt       uiBestPUMode  = 0;
    Distortion uiBestPUDistY = 0;
    Double     dBestPUCost   = MAX_DOUBLE;

#if ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
    UInt max=numModesForFullRD;

    if (DebugOptionList::ForceLumaMode.isSet())
    {
      max=0;  // we are forcing a direction, so don't bother with mode check
    }
    for ( UInt uiMode = 0; uiMode < max; uiMode++)
#else
    for( UInt uiMode = 0; uiMode < numModesForFullRD; uiMode++ )
#endif
    {
      // set luma prediction mode
      UInt uiOrgMode = uiRdModeList[uiMode];

      pcCU->setIntraDirSubParts ( CHANNEL_TYPE_LUMA, uiOrgMode, uiPartOffset, uiDepth + uiInitTrDepth );

      DEBUG_STRING_NEW(sMode)
      // set context models
      m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST] );

      // determine residual for partition
      Distortion uiPUDistY = 0;
      Double     dPUCost   = 0.0;
#if HHI_RQT_INTRA_SPEEDUP
      xRecurIntraCodingLumaQT( pcOrgYuv, pcPredYuv, pcResiYuv, resiLumaPU, uiPUDistY, true, dPUCost, tuRecurseWithPU DEBUG_STRING_PASS_INTO(sMode) );
#else
      xRecurIntraCodingLumaQT( pcOrgYuv, pcPredYuv, pcResiYuv, resiLumaPU, uiPUDistY, dPUCost, tuRecurseWithPU DEBUG_STRING_PASS_INTO(sMode) );
#endif

#if DEBUG_INTRA_SEARCH_COSTS
      std::cout << "2nd pass [luma,chroma] mode [" << Int(pcCU->getIntraDir(CHANNEL_TYPE_LUMA, uiPartOffset)) << "," << Int(pcCU->getIntraDir(CHANNEL_TYPE_CHROMA, uiPartOffset)) << "] cost = " << dPUCost << "\n";
#endif

      // check r-d cost
      if( dPUCost < dBestPUCost )
      {
        DEBUG_STRING_SWAP(sPU, sMode)
#if HHI_RQT_INTRA_SPEEDUP_MOD
        uiSecondBestMode  = uiBestPUMode;
        dSecondBestPUCost = dBestPUCost;
#endif
        uiBestPUMode  = uiOrgMode;
        uiBestPUDistY = uiPUDistY;
        dBestPUCost   = dPUCost;

        xSetIntraResultLumaQT( pcRecoYuv, tuRecurseWithPU );

        if (pps.getPpsRangeExtension().getCrossComponentPredictionEnabledFlag())
        {
          const Int xOffset = tuRecurseWithPU.getRect( COMPONENT_Y ).x0;
          const Int yOffset = tuRecurseWithPU.getRect( COMPONENT_Y ).y0;
          for (UInt storedResidualIndex = 0; storedResidualIndex < NUMBER_OF_STORED_RESIDUAL_TYPES; storedResidualIndex++)
          {
            if (bMaintainResidual[storedResidualIndex])
            {
              xStoreCrossComponentPredictionResult(resiLuma[storedResidualIndex], resiLumaPU[storedResidualIndex], tuRecurseWithPU, xOffset, yOffset, MAX_CU_SIZE, MAX_CU_SIZE );
            }
          }
        }

        UInt uiQPartNum = tuRecurseWithPU.GetAbsPartIdxNumParts();

        ::memcpy( m_puhQTTempTrIdx,  pcCU->getTransformIdx()       + uiPartOffset, uiQPartNum * sizeof( UChar ) );
        for (UInt component = 0; component < numberValidComponents; component++)
        {
          const ComponentID compID = ComponentID(component);
          ::memcpy( m_puhQTTempCbf[compID], pcCU->getCbf( compID  ) + uiPartOffset, uiQPartNum * sizeof( UChar ) );
          ::memcpy( m_puhQTTempTransformSkipFlag[compID],  pcCU->getTransformSkip(compID)  + uiPartOffset, uiQPartNum * sizeof( UChar ) );
        }
      }
#if HHI_RQT_INTRA_SPEEDUP_MOD
      else if( dPUCost < dSecondBestPUCost )
      {
        uiSecondBestMode  = uiOrgMode;
        dSecondBestPUCost = dPUCost;
      }
#endif
    } // Mode loop

#if HHI_RQT_INTRA_SPEEDUP
#if HHI_RQT_INTRA_SPEEDUP_MOD
    for( UInt ui =0; ui < 2; ++ui )
#endif
    {
#if HHI_RQT_INTRA_SPEEDUP_MOD
      UInt uiOrgMode   = ui ? uiSecondBestMode  : uiBestPUMode;
      if( uiOrgMode == MAX_UINT )
      {
        break;
      }
#else
      UInt uiOrgMode = uiBestPUMode;
#endif

#if ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
      if (DebugOptionList::ForceLumaMode.isSet())
      {
        uiOrgMode = DebugOptionList::ForceLumaMode.getInt();
      }
#endif

      pcCU->setIntraDirSubParts ( CHANNEL_TYPE_LUMA, uiOrgMode, uiPartOffset, uiDepth + uiInitTrDepth );
      DEBUG_STRING_NEW(sModeTree)

      // set context models
      m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST] );

      // determine residual for partition
      Distortion uiPUDistY = 0;
      Double     dPUCost   = 0.0;

      xRecurIntraCodingLumaQT( pcOrgYuv, pcPredYuv, pcResiYuv, resiLumaPU, uiPUDistY, false, dPUCost, tuRecurseWithPU DEBUG_STRING_PASS_INTO(sModeTree));

      // check r-d cost
      if( dPUCost < dBestPUCost )
      {
        DEBUG_STRING_SWAP(sPU, sModeTree)
        uiBestPUMode  = uiOrgMode;
        uiBestPUDistY = uiPUDistY;
        dBestPUCost   = dPUCost;

        xSetIntraResultLumaQT( pcRecoYuv, tuRecurseWithPU );

        if (pps.getPpsRangeExtension().getCrossComponentPredictionEnabledFlag())
        {
          const Int xOffset = tuRecurseWithPU.getRect( COMPONENT_Y ).x0;
          const Int yOffset = tuRecurseWithPU.getRect( COMPONENT_Y ).y0;
          for (UInt storedResidualIndex = 0; storedResidualIndex < NUMBER_OF_STORED_RESIDUAL_TYPES; storedResidualIndex++)
          {
            if (bMaintainResidual[storedResidualIndex])
            {
              xStoreCrossComponentPredictionResult(resiLuma[storedResidualIndex], resiLumaPU[storedResidualIndex], tuRecurseWithPU, xOffset, yOffset, MAX_CU_SIZE, MAX_CU_SIZE );
            }
          }
        }

        const UInt uiQPartNum = tuRecurseWithPU.GetAbsPartIdxNumParts();
        ::memcpy( m_puhQTTempTrIdx,  pcCU->getTransformIdx()       + uiPartOffset, uiQPartNum * sizeof( UChar ) );

        for (UInt component = 0; component < numberValidComponents; component++)
        {
          const ComponentID compID = ComponentID(component);
          ::memcpy( m_puhQTTempCbf[compID], pcCU->getCbf( compID  ) + uiPartOffset, uiQPartNum * sizeof( UChar ) );
          ::memcpy( m_puhQTTempTransformSkipFlag[compID],  pcCU->getTransformSkip(compID)  + uiPartOffset, uiQPartNum * sizeof( UChar ) );
        }
      }
    } // Mode loop
#endif

    DEBUG_STRING_APPEND(sDebug, sPU)

    //--- update overall distortion ---
    uiOverallDistY += uiBestPUDistY;

    //--- update transform index and cbf ---
    const UInt uiQPartNum = tuRecurseWithPU.GetAbsPartIdxNumParts();
    ::memcpy( pcCU->getTransformIdx()       + uiPartOffset, m_puhQTTempTrIdx,  uiQPartNum * sizeof( UChar ) );
    for (UInt component = 0; component < numberValidComponents; component++)
    {
      const ComponentID compID = ComponentID(component);
      ::memcpy( pcCU->getCbf( compID  ) + uiPartOffset, m_puhQTTempCbf[compID], uiQPartNum * sizeof( UChar ) );
      ::memcpy( pcCU->getTransformSkip( compID  ) + uiPartOffset, m_puhQTTempTransformSkipFlag[compID ], uiQPartNum * sizeof( UChar ) );
    }

    //--- set reconstruction for next intra prediction blocks ---
    if( !tuRecurseWithPU.IsLastSection() )
    {
      const TComRectangle &puRect=tuRecurseWithPU.getRect(COMPONENT_Y);
      const UInt  uiCompWidth   = puRect.width;
      const UInt  uiCompHeight  = puRect.height;

      const UInt  uiZOrder      = pcCU->getZorderIdxInCtu() + uiPartOffset;
            Pel*  piDes         = pcCU->getPic()->getPicYuvRec()->getAddr( COMPONENT_Y, pcCU->getCtuRsAddr(), uiZOrder );
      const UInt  uiDesStride   = pcCU->getPic()->getPicYuvRec()->getStride( COMPONENT_Y);
      const Pel*  piSrc         = pcRecoYuv->getAddr( COMPONENT_Y, uiPartOffset );
      const UInt  uiSrcStride   = pcRecoYuv->getStride( COMPONENT_Y);

      for( UInt uiY = 0; uiY < uiCompHeight; uiY++, piSrc += uiSrcStride, piDes += uiDesStride )
      {
        for( UInt uiX = 0; uiX < uiCompWidth; uiX++ )
        {
          piDes[ uiX ] = piSrc[ uiX ];
        }
      }
    }

    //=== update PU data ====
    pcCU->setIntraDirSubParts     ( CHANNEL_TYPE_LUMA, uiBestPUMode, uiPartOffset, uiDepth + uiInitTrDepth );
	
  } while (tuRecurseWithPU.nextSection(tuRecurseCU));


  if( uiNumPU > 1 )
  { // set Cbf for all blocks
    UInt uiCombCbfY = 0;
    UInt uiCombCbfU = 0;
    UInt uiCombCbfV = 0;
    UInt uiPartIdx  = 0;
    for( UInt uiPart = 0; uiPart < 4; uiPart++, uiPartIdx += uiQNumParts )
    {
      uiCombCbfY |= pcCU->getCbf( uiPartIdx, COMPONENT_Y,  1 );
      uiCombCbfU |= pcCU->getCbf( uiPartIdx, COMPONENT_Cb, 1 );
      uiCombCbfV |= pcCU->getCbf( uiPartIdx, COMPONENT_Cr, 1 );
    }
    for( UInt uiOffs = 0; uiOffs < 4 * uiQNumParts; uiOffs++ )
    {
      pcCU->getCbf( COMPONENT_Y  )[ uiOffs ] |= uiCombCbfY;
      pcCU->getCbf( COMPONENT_Cb )[ uiOffs ] |= uiCombCbfU;
      pcCU->getCbf( COMPONENT_Cr )[ uiOffs ] |= uiCombCbfV;
    }
  }

  //===== reset context models =====
  m_pcRDGoOnSbacCoder->load(m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST]);

  //===== set distortion (rate and r-d costs are determined later) =====
  pcCU->getTotalDistortion() = uiOverallDistY;
}




Void
TEncSearch::estIntraPredChromaQT(TComDataCU* pcCU,
                                 TComYuv*    pcOrgYuv,
                                 TComYuv*    pcPredYuv,
                                 TComYuv*    pcResiYuv,
                                 TComYuv*    pcRecoYuv,
                                 Pel         resiLuma[NUMBER_OF_STORED_RESIDUAL_TYPES][MAX_CU_SIZE * MAX_CU_SIZE]
                                 DEBUG_STRING_FN_DECLARE(sDebug))
{
  const UInt    uiInitTrDepth  = pcCU->getPartitionSize(0) != SIZE_2Nx2N && enable4ChromaPUsInIntraNxNCU(pcOrgYuv->getChromaFormat()) ? 1 : 0;

  TComTURecurse tuRecurseCU(pcCU, 0);
  TComTURecurse tuRecurseWithPU(tuRecurseCU, false, (uiInitTrDepth==0)?TComTU::DONT_SPLIT : TComTU::QUAD_SPLIT);
  const UInt    uiQNumParts    = tuRecurseWithPU.GetAbsPartIdxNumParts();
  const UInt    uiDepthCU=tuRecurseWithPU.getCUDepth();
  const UInt    numberValidComponents = pcCU->getPic()->getNumberValidComponents();

  do
  {
    UInt       uiBestMode  = 0;
    Distortion uiBestDist  = 0;
    Double     dBestCost   = MAX_DOUBLE;

    //----- init mode list -----
    if (tuRecurseWithPU.ProcessChannelSection(CHANNEL_TYPE_CHROMA))
    {
      UInt uiModeList[FAST_UDI_MAX_RDMODE_NUM];
      const UInt  uiQPartNum     = uiQNumParts;
      const UInt  uiPartOffset   = tuRecurseWithPU.GetAbsPartIdxTU();
      {
        UInt  uiMinMode = 0;
        UInt  uiMaxMode = NUM_CHROMA_MODE;

        //----- check chroma modes -----
        pcCU->getAllowedChromaDir( uiPartOffset, uiModeList );

#if ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
        if (DebugOptionList::ForceChromaMode.isSet())
        {
          uiMinMode=DebugOptionList::ForceChromaMode.getInt();
          if (uiModeList[uiMinMode]==34)
          {
            uiMinMode=4; // if the fixed mode has been renumbered because DM_CHROMA covers it, use DM_CHROMA.
          }
          uiMaxMode=uiMinMode+1;
        }
#endif

        DEBUG_STRING_NEW(sPU)

        for( UInt uiMode = uiMinMode; uiMode < uiMaxMode; uiMode++ )
        {
          //----- restore context models -----
          m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[uiDepthCU][CI_CURR_BEST] );
          
          DEBUG_STRING_NEW(sMode)
          //----- chroma coding -----
          Distortion uiDist = 0;
          pcCU->setIntraDirSubParts  ( CHANNEL_TYPE_CHROMA, uiModeList[uiMode], uiPartOffset, uiDepthCU+uiInitTrDepth );
          xRecurIntraChromaCodingQT       ( pcOrgYuv, pcPredYuv, pcResiYuv, resiLuma, uiDist, tuRecurseWithPU DEBUG_STRING_PASS_INTO(sMode) );

          if( pcCU->getSlice()->getPPS()->getUseTransformSkip() )
          {
            m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[uiDepthCU][CI_CURR_BEST] );
          }

          UInt    uiBits = xGetIntraBitsQT( tuRecurseWithPU, false, true, false );
          Double  dCost  = m_pcRdCost->calcRdCost( uiBits, uiDist );

          //----- compare -----
          if( dCost < dBestCost )
          {
            DEBUG_STRING_SWAP(sPU, sMode);
            dBestCost   = dCost;
            uiBestDist  = uiDist;
            uiBestMode  = uiModeList[uiMode];

            xSetIntraResultChromaQT( pcRecoYuv, tuRecurseWithPU );
            for (UInt componentIndex = COMPONENT_Cb; componentIndex < numberValidComponents; componentIndex++)
            {
              const ComponentID compID = ComponentID(componentIndex);
              ::memcpy( m_puhQTTempCbf[compID], pcCU->getCbf( compID )+uiPartOffset, uiQPartNum * sizeof( UChar ) );
              ::memcpy( m_puhQTTempTransformSkipFlag[compID], pcCU->getTransformSkip( compID )+uiPartOffset, uiQPartNum * sizeof( UChar ) );
              ::memcpy( m_phQTTempCrossComponentPredictionAlpha[compID], pcCU->getCrossComponentPredictionAlpha(compID)+uiPartOffset, uiQPartNum * sizeof( SChar ) );
            }
          }
        }

        DEBUG_STRING_APPEND(sDebug, sPU)

        //----- set data -----
        for (UInt componentIndex = COMPONENT_Cb; componentIndex < numberValidComponents; componentIndex++)
        {
          const ComponentID compID = ComponentID(componentIndex);
          ::memcpy( pcCU->getCbf( compID )+uiPartOffset, m_puhQTTempCbf[compID], uiQPartNum * sizeof( UChar ) );
          ::memcpy( pcCU->getTransformSkip( compID )+uiPartOffset, m_puhQTTempTransformSkipFlag[compID], uiQPartNum * sizeof( UChar ) );
          ::memcpy( pcCU->getCrossComponentPredictionAlpha(compID)+uiPartOffset, m_phQTTempCrossComponentPredictionAlpha[compID], uiQPartNum * sizeof( SChar ) );
        }
      }

      if( ! tuRecurseWithPU.IsLastSection() )
      {
        for (UInt ch=COMPONENT_Cb; ch<numberValidComponents; ch++)
        {
          const ComponentID compID    = ComponentID(ch);
          const TComRectangle &tuRect = tuRecurseWithPU.getRect(compID);
          const UInt  uiCompWidth     = tuRect.width;
          const UInt  uiCompHeight    = tuRect.height;
          const UInt  uiZOrder        = pcCU->getZorderIdxInCtu() + tuRecurseWithPU.GetAbsPartIdxTU();
                Pel*  piDes           = pcCU->getPic()->getPicYuvRec()->getAddr( compID, pcCU->getCtuRsAddr(), uiZOrder );
          const UInt  uiDesStride     = pcCU->getPic()->getPicYuvRec()->getStride( compID);
          const Pel*  piSrc           = pcRecoYuv->getAddr( compID, uiPartOffset );
          const UInt  uiSrcStride     = pcRecoYuv->getStride( compID);

          for( UInt uiY = 0; uiY < uiCompHeight; uiY++, piSrc += uiSrcStride, piDes += uiDesStride )
          {
            for( UInt uiX = 0; uiX < uiCompWidth; uiX++ )
            {
              piDes[ uiX ] = piSrc[ uiX ];
            }
          }
        }
      }

      pcCU->setIntraDirSubParts( CHANNEL_TYPE_CHROMA, uiBestMode, uiPartOffset, uiDepthCU+uiInitTrDepth );
      pcCU->getTotalDistortion      () += uiBestDist;
    }

  } while (tuRecurseWithPU.nextSection(tuRecurseCU));

  //----- restore context models -----

  if( uiInitTrDepth != 0 )
  { // set Cbf for all blocks
    UInt uiCombCbfU = 0;
    UInt uiCombCbfV = 0;
    UInt uiPartIdx  = 0;
    for( UInt uiPart = 0; uiPart < 4; uiPart++, uiPartIdx += uiQNumParts )
    {
      uiCombCbfU |= pcCU->getCbf( uiPartIdx, COMPONENT_Cb, 1 );
      uiCombCbfV |= pcCU->getCbf( uiPartIdx, COMPONENT_Cr, 1 );
    }
    for( UInt uiOffs = 0; uiOffs < 4 * uiQNumParts; uiOffs++ )
    {
      pcCU->getCbf( COMPONENT_Cb )[ uiOffs ] |= uiCombCbfU;
      pcCU->getCbf( COMPONENT_Cr )[ uiOffs ] |= uiCombCbfV;
    }
  }

  m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[uiDepthCU][CI_CURR_BEST] );
}




/** Function for encoding and reconstructing luma/chroma samples of a PCM mode CU.
 * \param pcCU pointer to current CU
 * \param uiAbsPartIdx part index
 * \param pOrg pointer to original sample arrays
 * \param pPCM pointer to PCM code arrays
 * \param pPred pointer to prediction signal arrays
 * \param pResi pointer to residual signal arrays
 * \param pReco pointer to reconstructed sample arrays
 * \param uiStride stride of the original/prediction/residual sample arrays
 * \param uiWidth block width
 * \param uiHeight block height
 * \param compID texture component type
 */
Void TEncSearch::xEncPCM (TComDataCU* pcCU, UInt uiAbsPartIdx, Pel* pOrg, Pel* pPCM, Pel* pPred, Pel* pResi, Pel* pReco, UInt uiStride, UInt uiWidth, UInt uiHeight, const ComponentID compID )
{
  const UInt uiReconStride   = pcCU->getPic()->getPicYuvRec()->getStride(compID);
  const UInt uiPCMBitDepth   = pcCU->getSlice()->getSPS()->getPCMBitDepth(toChannelType(compID));
  const Int  channelBitDepth = pcCU->getSlice()->getSPS()->getBitDepth(toChannelType(compID));
  Pel* pRecoPic = pcCU->getPic()->getPicYuvRec()->getAddr(compID, pcCU->getCtuRsAddr(), pcCU->getZorderIdxInCtu()+uiAbsPartIdx);

  const Int pcmShiftRight=(channelBitDepth - Int(uiPCMBitDepth));

  assert(pcmShiftRight >= 0);

  for( UInt uiY = 0; uiY < uiHeight; uiY++ )
  {
    for( UInt uiX = 0; uiX < uiWidth; uiX++ )
    {
      // Reset pred and residual
      pPred[uiX] = 0;
      pResi[uiX] = 0;
      // Encode
      pPCM[uiX] = (pOrg[uiX]>>pcmShiftRight);
      // Reconstruction
      pReco   [uiX] = (pPCM[uiX]<<(pcmShiftRight));
      pRecoPic[uiX] = pReco[uiX];
    }
    pPred += uiStride;
    pResi += uiStride;
    pPCM += uiWidth;
    pOrg += uiStride;
    pReco += uiStride;
    pRecoPic += uiReconStride;
  }
}


//!  Function for PCM mode estimation.
Void TEncSearch::IPCMSearch( TComDataCU* pcCU, TComYuv* pcOrgYuv, TComYuv* pcPredYuv, TComYuv* pcResiYuv, TComYuv* pcRecoYuv )
{
  UInt              uiDepth      = pcCU->getDepth(0);
  const Distortion  uiDistortion = 0;
  UInt              uiBits;

  Double dCost;

  for (UInt ch=0; ch < pcCU->getPic()->getNumberValidComponents(); ch++)
  {
    const ComponentID compID  = ComponentID(ch);
    const UInt width  = pcCU->getWidth(0)  >> pcCU->getPic()->getComponentScaleX(compID);
    const UInt height = pcCU->getHeight(0) >> pcCU->getPic()->getComponentScaleY(compID);
    const UInt stride = pcPredYuv->getStride(compID);

    Pel * pOrig    = pcOrgYuv->getAddr  (compID, 0, width);
    Pel * pResi    = pcResiYuv->getAddr(compID, 0, width);
    Pel * pPred    = pcPredYuv->getAddr(compID, 0, width);
    Pel * pReco    = pcRecoYuv->getAddr(compID, 0, width);
    Pel * pPCM     = pcCU->getPCMSample (compID);

    xEncPCM ( pcCU, 0, pOrig, pPCM, pPred, pResi, pReco, stride, width, height, compID );

  }

  m_pcEntropyCoder->resetBits();
  xEncIntraHeader ( pcCU, uiDepth, 0, true, false);
  uiBits = m_pcEntropyCoder->getNumberOfWrittenBits();

  dCost = m_pcRdCost->calcRdCost( uiBits, uiDistortion );

  m_pcRDGoOnSbacCoder->load(m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST]);

  pcCU->getTotalBits()       = uiBits;
  pcCU->getTotalCost()       = dCost;
  pcCU->getTotalDistortion() = uiDistortion;

  pcCU->copyToPic(uiDepth);
}




Void TEncSearch::xGetInterPredictionError( TComDataCU* pcCU, TComYuv* pcYuvOrg, Int iPartIdx, Distortion& ruiErr, Bool /*bHadamard*/ )
{
  motionCompensation( pcCU, &m_tmpYuvPred, REF_PIC_LIST_X, iPartIdx );

  UInt uiAbsPartIdx = 0;
  Int iWidth = 0;
  Int iHeight = 0;
  pcCU->getPartIndexAndSize( iPartIdx, uiAbsPartIdx, iWidth, iHeight );

  DistParam cDistParam;

  cDistParam.bApplyWeight = false;


  m_pcRdCost->setDistParam( cDistParam, pcCU->getSlice()->getSPS()->getBitDepth(CHANNEL_TYPE_LUMA),
                            pcYuvOrg->getAddr( COMPONENT_Y, uiAbsPartIdx ), pcYuvOrg->getStride(COMPONENT_Y),
                            m_tmpYuvPred .getAddr( COMPONENT_Y, uiAbsPartIdx ), m_tmpYuvPred.getStride(COMPONENT_Y),
                            iWidth, iHeight, m_pcEncCfg->getUseHADME() && (pcCU->getCUTransquantBypass(iPartIdx) == 0) );

  ruiErr = cDistParam.DistFunc( &cDistParam );
}

//! estimation of best merge coding
Void TEncSearch::xMergeEstimation( TComDataCU* pcCU, TComYuv* pcYuvOrg, Int iPUIdx, UInt& uiInterDir, TComMvField* pacMvField, UInt& uiMergeIndex, Distortion& ruiCost, TComMvField* cMvFieldNeighbours, UChar* uhInterDirNeighbours, Int& numValidMergeCand )
{

  UInt uiAbsPartIdx = 0;
  Int iWidth = 0;
  Int iHeight = 0;

 
  pcCU->getPartIndexAndSize( iPUIdx, uiAbsPartIdx, iWidth, iHeight );
  UInt uiDepth = pcCU->getDepth( uiAbsPartIdx );

  PartSize partSize = pcCU->getPartitionSize( 0 );
  if ( pcCU->getSlice()->getPPS()->getLog2ParallelMergeLevelMinus2() && partSize != SIZE_2Nx2N && pcCU->getWidth( 0 ) <= 8 )
  {
    if ( iPUIdx == 0 )
    {
      pcCU->setPartSizeSubParts( SIZE_2Nx2N, 0, uiDepth ); // temporarily set
      pcCU->getInterMergeCandidates( 0, 0, cMvFieldNeighbours,uhInterDirNeighbours, numValidMergeCand );
      pcCU->setPartSizeSubParts( partSize, 0, uiDepth ); // restore
    }
  }
  else
  {
    pcCU->getInterMergeCandidates( uiAbsPartIdx, iPUIdx, cMvFieldNeighbours, uhInterDirNeighbours, numValidMergeCand );
  }

  xRestrictBipredMergeCand( pcCU, iPUIdx, cMvFieldNeighbours, uhInterDirNeighbours, numValidMergeCand );

  ruiCost = std::numeric_limits<Distortion>::max();
  for( UInt uiMergeCand = 0; uiMergeCand < numValidMergeCand; ++uiMergeCand )
  {
    Distortion uiCostCand = std::numeric_limits<Distortion>::max();
    UInt       uiBitsCand = 0;

    PartSize ePartSize = pcCU->getPartitionSize( 0 );

    pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField( cMvFieldNeighbours[0 + 2*uiMergeCand], ePartSize, uiAbsPartIdx, 0, iPUIdx );
    pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField( cMvFieldNeighbours[1 + 2*uiMergeCand], ePartSize, uiAbsPartIdx, 0, iPUIdx );

    xGetInterPredictionError( pcCU, pcYuvOrg, iPUIdx, uiCostCand, m_pcEncCfg->getUseHADME() );
    uiBitsCand = uiMergeCand + 1;
    if (uiMergeCand == m_pcEncCfg->getMaxNumMergeCand() -1)
    {
        uiBitsCand--;
    }
    uiCostCand = uiCostCand + m_pcRdCost->getCost( uiBitsCand );
    if ( uiCostCand < ruiCost )
    {
      ruiCost = uiCostCand;
      pacMvField[0] = cMvFieldNeighbours[0 + 2*uiMergeCand];
      pacMvField[1] = cMvFieldNeighbours[1 + 2*uiMergeCand];
      uiInterDir = uhInterDirNeighbours[uiMergeCand];
      uiMergeIndex = uiMergeCand;
    }
  }
 
}

/** convert bi-pred merge candidates to uni-pred
 * \param pcCU
 * \param puIdx
 * \param mvFieldNeighbours
 * \param interDirNeighbours
 * \param numValidMergeCand
 * \returns Void
 */
Void TEncSearch::xRestrictBipredMergeCand( TComDataCU* pcCU, UInt puIdx, TComMvField* mvFieldNeighbours, UChar* interDirNeighbours, Int numValidMergeCand )
{
	
  if ( pcCU->isBipredRestriction(puIdx) )
  {
    for( UInt mergeCand = 0; mergeCand < numValidMergeCand; ++mergeCand )
    {
      if ( interDirNeighbours[mergeCand] == 3 )
      {
        interDirNeighbours[mergeCand] = 1;
        mvFieldNeighbours[(mergeCand << 1) + 1].setMvField(TComMv(0,0), -1);
      }
    }
  }
}

//! search of the best candidate for inter prediction
#if AMP_MRG
Void TEncSearch::predInterSearch( TComDataCU* pcCU, TComYuv* pcOrgYuv, TComYuv* pcPredYuv, TComYuv* pcResiYuv, TComYuv* pcRecoYuv DEBUG_STRING_FN_DECLARE(sDebug), Bool bUseRes, Bool bUseMRG )
#else
Void TEncSearch::predInterSearch( TComDataCU* pcCU, TComYuv* pcOrgYuv, TComYuv* pcPredYuv, TComYuv* pcResiYuv, TComYuv* pcRecoYuv, Bool bUseRes )
#endif
{
  for(UInt i=0; i<NUM_REF_PIC_LIST_01; i++)
  {
    m_acYuvPred[i].clear();
  }
  m_cYuvPredTemp.clear();
  pcPredYuv->clear();

  if ( !bUseRes )
  {
    pcResiYuv->clear();
  }

  pcRecoYuv->clear();
  
  TComMv       cMvSrchRngLT;
  TComMv       cMvSrchRngRB;

  TComMv       cMvZero;
  TComMv       TempMv; //kolya

  TComMv       cMv[2];
  TComMv       cMvBi[2];
  TComMv       cMvTemp[2][33];

  Int          iNumPart    = pcCU->getNumPartitions();
  Int          iNumPredDir = pcCU->getSlice()->isInterP() ? 1 : 2;

  TComMv       cMvPred[2][33];

  TComMv       cMvPredBi[2][33];
  Int          aaiMvpIdxBi[2][33];

  Int          aaiMvpIdx[2][33];
  Int          aaiMvpNum[2][33];

  AMVPInfo     aacAMVPInfo[2][33];

  Int          iRefIdx[2]={0,0}; //If un-initialized, may cause SEGV in bi-directional prediction iterative stage.
  Int          iRefIdxBi[2];

  UInt         uiPartAddr;
  Int          iRoiWidth, iRoiHeight;

  UInt         uiMbBits[3] = {1, 1, 0};

  UInt         uiLastMode = 0;
  Int          iRefStart, iRefEnd;

  PartSize     ePartSize = pcCU->getPartitionSize( 0 );

  Int          bestBiPRefIdxL1 = 0;
  Int          bestBiPMvpL1 = 0;
  Distortion   biPDistTemp = std::numeric_limits<Distortion>::max();

  TComMvField cMvFieldNeighbours[MRG_MAX_NUM_CANDS << 1]; // double length for mv of both lists
  UChar uhInterDirNeighbours[MRG_MAX_NUM_CANDS];
  Int numValidMergeCand = 0 ;

  for ( Int iPartIdx = 0; iPartIdx < iNumPart; iPartIdx++ )
  {
    Distortion   uiCost[2] = { std::numeric_limits<Distortion>::max(), std::numeric_limits<Distortion>::max() };
    Distortion   uiCostBi  =   std::numeric_limits<Distortion>::max();
    Distortion   uiCostTemp;

    UInt         uiBits[3];
    UInt         uiBitsTemp;
    Distortion   bestBiPDist = std::numeric_limits<Distortion>::max();

    Distortion   uiCostTempL0[MAX_NUM_REF];
    for (Int iNumRef=0; iNumRef < MAX_NUM_REF; iNumRef++)
    {
      uiCostTempL0[iNumRef] = std::numeric_limits<Distortion>::max();
    }
    UInt         uiBitsTempL0[MAX_NUM_REF];

    TComMv       mvValidList1;
    Int          refIdxValidList1 = 0;
    UInt         bitsValidList1 = MAX_UINT;
    Distortion   costValidList1 = std::numeric_limits<Distortion>::max();

    xGetBlkBits( ePartSize, pcCU->getSlice()->isInterP(), iPartIdx, uiLastMode, uiMbBits);

    pcCU->getPartIndexAndSize( iPartIdx, uiPartAddr, iRoiWidth, iRoiHeight );
	
	
#if AMP_MRG
    Bool bTestNormalMC = true;

    if ( bUseMRG && pcCU->getWidth( 0 ) > 8 && iNumPart == 2 )
    {
      bTestNormalMC = false;
    }

    if (bTestNormalMC)
    {
#endif

    //  Uni-directional prediction
    for ( Int iRefList = 0; iRefList < iNumPredDir; iRefList++ )
    {
      RefPicList  eRefPicList = ( iRefList ? REF_PIC_LIST_1 : REF_PIC_LIST_0 );

      for ( Int iRefIdxTemp = 0; iRefIdxTemp < pcCU->getSlice()->getNumRefIdx(eRefPicList); iRefIdxTemp++ )
      {
        uiBitsTemp = uiMbBits[iRefList];
        if ( pcCU->getSlice()->getNumRefIdx(eRefPicList) > 1 )
        {
          uiBitsTemp += iRefIdxTemp+1;
          if ( iRefIdxTemp == pcCU->getSlice()->getNumRefIdx(eRefPicList)-1 )
          {
            uiBitsTemp--;
          }
        }
        xEstimateMvPredAMVP( pcCU, pcOrgYuv, iPartIdx, eRefPicList, iRefIdxTemp, cMvPred[iRefList][iRefIdxTemp], false, &biPDistTemp);
        aaiMvpIdx[iRefList][iRefIdxTemp] = pcCU->getMVPIdx(eRefPicList, uiPartAddr);
        aaiMvpNum[iRefList][iRefIdxTemp] = pcCU->getMVPNum(eRefPicList, uiPartAddr);

        if(pcCU->getSlice()->getMvdL1ZeroFlag() && iRefList==1 && biPDistTemp < bestBiPDist)
        {
          bestBiPDist = biPDistTemp;
          bestBiPMvpL1 = aaiMvpIdx[iRefList][iRefIdxTemp];
          bestBiPRefIdxL1 = iRefIdxTemp;
        }

        uiBitsTemp += m_auiMVPIdxCost[aaiMvpIdx[iRefList][iRefIdxTemp]][AMVP_MAX_NUM_CANDS];

        if ( m_pcEncCfg->getFastMEForGenBLowDelayEnabled() && iRefList == 1 )    // list 1
        {
          if ( pcCU->getSlice()->getList1IdxToList0Idx( iRefIdxTemp ) >= 0 )
          {
            cMvTemp[1][iRefIdxTemp] = cMvTemp[0][pcCU->getSlice()->getList1IdxToList0Idx( iRefIdxTemp )];
            uiCostTemp = uiCostTempL0[pcCU->getSlice()->getList1IdxToList0Idx( iRefIdxTemp )];
            /*first subtract the bit-rate part of the cost of the other list*/
            uiCostTemp -= m_pcRdCost->getCost( uiBitsTempL0[pcCU->getSlice()->getList1IdxToList0Idx( iRefIdxTemp )] );
            /*correct the bit-rate part of the current ref*/
            m_pcRdCost->setPredictor  ( cMvPred[iRefList][iRefIdxTemp] );
            uiBitsTemp += m_pcRdCost->getBitsOfVectorWithPredictor( cMvTemp[1][iRefIdxTemp].getHor(), cMvTemp[1][iRefIdxTemp].getVer() );
            /*calculate the correct cost*/
            uiCostTemp += m_pcRdCost->getCost( uiBitsTemp );
          }
          else
          {
            xMotionEstimation ( pcCU, pcOrgYuv, iPartIdx, eRefPicList, &cMvPred[iRefList][iRefIdxTemp], iRefIdxTemp, cMvTemp[iRefList][iRefIdxTemp], uiBitsTemp, uiCostTemp );
          }
        }
        else
        {
          xMotionEstimation ( pcCU, pcOrgYuv, iPartIdx, eRefPicList, &cMvPred[iRefList][iRefIdxTemp], iRefIdxTemp, cMvTemp[iRefList][iRefIdxTemp], uiBitsTemp, uiCostTemp );
        }
        xCopyAMVPInfo(pcCU->getCUMvField(eRefPicList)->getAMVPInfo(), &aacAMVPInfo[iRefList][iRefIdxTemp]); // must always be done ( also when AMVP_MODE = AM_NONE )
        xCheckBestMVP(pcCU, eRefPicList, cMvTemp[iRefList][iRefIdxTemp], cMvPred[iRefList][iRefIdxTemp], aaiMvpIdx[iRefList][iRefIdxTemp], uiBitsTemp, uiCostTemp);

        if ( iRefList == 0 )
        {
          uiCostTempL0[iRefIdxTemp] = uiCostTemp;
          uiBitsTempL0[iRefIdxTemp] = uiBitsTemp;
        }
        if ( uiCostTemp < uiCost[iRefList] )
        {
          uiCost[iRefList] = uiCostTemp;
          uiBits[iRefList] = uiBitsTemp; // storing for bi-prediction

          // set motion
          cMv[iRefList]     = cMvTemp[iRefList][iRefIdxTemp];
          iRefIdx[iRefList] = iRefIdxTemp;
        }

        if ( iRefList == 1 && uiCostTemp < costValidList1 && pcCU->getSlice()->getList1IdxToList0Idx( iRefIdxTemp ) < 0 )
        {
          costValidList1 = uiCostTemp;
          bitsValidList1 = uiBitsTemp;

          // set motion
          mvValidList1     = cMvTemp[iRefList][iRefIdxTemp];
          refIdxValidList1 = iRefIdxTemp;
        }
      }
    }

    //  Bi-predictive Motion estimation
    if ( (pcCU->getSlice()->isInterB()) && (pcCU->isBipredRestriction(iPartIdx) == false) )
    {

      cMvBi[0] = cMv[0];            cMvBi[1] = cMv[1];
      iRefIdxBi[0] = iRefIdx[0];    iRefIdxBi[1] = iRefIdx[1];

      ::memcpy(cMvPredBi, cMvPred, sizeof(cMvPred));
      ::memcpy(aaiMvpIdxBi, aaiMvpIdx, sizeof(aaiMvpIdx));

      UInt uiMotBits[2];

      if(pcCU->getSlice()->getMvdL1ZeroFlag())
      {
        xCopyAMVPInfo(&aacAMVPInfo[1][bestBiPRefIdxL1], pcCU->getCUMvField(REF_PIC_LIST_1)->getAMVPInfo());
        pcCU->setMVPIdxSubParts( bestBiPMvpL1, REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        aaiMvpIdxBi[1][bestBiPRefIdxL1] = bestBiPMvpL1;
        cMvPredBi[1][bestBiPRefIdxL1]   = pcCU->getCUMvField(REF_PIC_LIST_1)->getAMVPInfo()->m_acMvCand[bestBiPMvpL1];

        cMvBi[1] = cMvPredBi[1][bestBiPRefIdxL1];
        iRefIdxBi[1] = bestBiPRefIdxL1;
        pcCU->getCUMvField( REF_PIC_LIST_1 )->setAllMv( cMvBi[1], ePartSize, uiPartAddr, 0, iPartIdx );
        pcCU->getCUMvField( REF_PIC_LIST_1 )->setAllRefIdx( iRefIdxBi[1], ePartSize, uiPartAddr, 0, iPartIdx );
        TComYuv* pcYuvPred = &m_acYuvPred[REF_PIC_LIST_1];
        motionCompensation( pcCU, pcYuvPred, REF_PIC_LIST_1, iPartIdx );

        uiMotBits[0] = uiBits[0] - uiMbBits[0];
        uiMotBits[1] = uiMbBits[1];

        if ( pcCU->getSlice()->getNumRefIdx(REF_PIC_LIST_1) > 1 )
        {
          uiMotBits[1] += bestBiPRefIdxL1+1;
          if ( bestBiPRefIdxL1 == pcCU->getSlice()->getNumRefIdx(REF_PIC_LIST_1)-1 )
          {
            uiMotBits[1]--;
          }
        }

        uiMotBits[1] += m_auiMVPIdxCost[aaiMvpIdxBi[1][bestBiPRefIdxL1]][AMVP_MAX_NUM_CANDS];

        uiBits[2] = uiMbBits[2] + uiMotBits[0] + uiMotBits[1];

        cMvTemp[1][bestBiPRefIdxL1] = cMvBi[1];
      }
      else
      {
        uiMotBits[0] = uiBits[0] - uiMbBits[0];
        uiMotBits[1] = uiBits[1] - uiMbBits[1];
        uiBits[2] = uiMbBits[2] + uiMotBits[0] + uiMotBits[1];
      }

      // 4-times iteration (default)
      Int iNumIter = 4;

      // fast encoder setting: only one iteration
      if ( m_pcEncCfg->getFastInterSearchMode()==FASTINTERSEARCH_MODE1 || m_pcEncCfg->getFastInterSearchMode()==FASTINTERSEARCH_MODE2 || pcCU->getSlice()->getMvdL1ZeroFlag() )
      {
        iNumIter = 1;
      }

      for ( Int iIter = 0; iIter < iNumIter; iIter++ )
      {
        Int         iRefList    = iIter % 2;

        if ( m_pcEncCfg->getFastInterSearchMode()==FASTINTERSEARCH_MODE1 || m_pcEncCfg->getFastInterSearchMode()==FASTINTERSEARCH_MODE2 )
        {
          if( uiCost[0] <= uiCost[1] )
          {
            iRefList = 1;
          }
          else
          {
            iRefList = 0;
          }
        }
        else if ( iIter == 0 )
        {
          iRefList = 0;
        }
        if ( iIter == 0 && !pcCU->getSlice()->getMvdL1ZeroFlag())
        {
          pcCU->getCUMvField(RefPicList(1-iRefList))->setAllMv( cMv[1-iRefList], ePartSize, uiPartAddr, 0, iPartIdx );
          pcCU->getCUMvField(RefPicList(1-iRefList))->setAllRefIdx( iRefIdx[1-iRefList], ePartSize, uiPartAddr, 0, iPartIdx );
          TComYuv*  pcYuvPred = &m_acYuvPred[1-iRefList];
          motionCompensation ( pcCU, pcYuvPred, RefPicList(1-iRefList), iPartIdx );
        }

        RefPicList  eRefPicList = ( iRefList ? REF_PIC_LIST_1 : REF_PIC_LIST_0 );

        if(pcCU->getSlice()->getMvdL1ZeroFlag())
        {
          iRefList = 0;
          eRefPicList = REF_PIC_LIST_0;
        }

        Bool bChanged = false;

        iRefStart = 0;
        iRefEnd   = pcCU->getSlice()->getNumRefIdx(eRefPicList)-1;

        for ( Int iRefIdxTemp = iRefStart; iRefIdxTemp <= iRefEnd; iRefIdxTemp++ )
        {
          uiBitsTemp = uiMbBits[2] + uiMotBits[1-iRefList];
          if ( pcCU->getSlice()->getNumRefIdx(eRefPicList) > 1 )
          {
            uiBitsTemp += iRefIdxTemp+1;
            if ( iRefIdxTemp == pcCU->getSlice()->getNumRefIdx(eRefPicList)-1 )
            {
              uiBitsTemp--;
            }
          }
          uiBitsTemp += m_auiMVPIdxCost[aaiMvpIdxBi[iRefList][iRefIdxTemp]][AMVP_MAX_NUM_CANDS];
          // call ME
          xMotionEstimation ( pcCU, pcOrgYuv, iPartIdx, eRefPicList, &cMvPredBi[iRefList][iRefIdxTemp], iRefIdxTemp, cMvTemp[iRefList][iRefIdxTemp], uiBitsTemp, uiCostTemp, true );

          xCopyAMVPInfo(&aacAMVPInfo[iRefList][iRefIdxTemp], pcCU->getCUMvField(eRefPicList)->getAMVPInfo());
          xCheckBestMVP(pcCU, eRefPicList, cMvTemp[iRefList][iRefIdxTemp], cMvPredBi[iRefList][iRefIdxTemp], aaiMvpIdxBi[iRefList][iRefIdxTemp], uiBitsTemp, uiCostTemp);

          if ( uiCostTemp < uiCostBi )
          {
            bChanged = true;

            cMvBi[iRefList]     = cMvTemp[iRefList][iRefIdxTemp];
            iRefIdxBi[iRefList] = iRefIdxTemp;

            uiCostBi            = uiCostTemp;
            uiMotBits[iRefList] = uiBitsTemp - uiMbBits[2] - uiMotBits[1-iRefList];
            uiBits[2]           = uiBitsTemp;

            if(iNumIter!=1)
            {
              //  Set motion
              pcCU->getCUMvField( eRefPicList )->setAllMv( cMvBi[iRefList], ePartSize, uiPartAddr, 0, iPartIdx );
              pcCU->getCUMvField( eRefPicList )->setAllRefIdx( iRefIdxBi[iRefList], ePartSize, uiPartAddr, 0, iPartIdx );

              TComYuv* pcYuvPred = &m_acYuvPred[iRefList];
              motionCompensation( pcCU, pcYuvPred, eRefPicList, iPartIdx );
            }
          }
        } // for loop-iRefIdxTemp

        if ( !bChanged )
        {
          if ( uiCostBi <= uiCost[0] && uiCostBi <= uiCost[1] )
          {
            xCopyAMVPInfo(&aacAMVPInfo[0][iRefIdxBi[0]], pcCU->getCUMvField(REF_PIC_LIST_0)->getAMVPInfo());
            xCheckBestMVP(pcCU, REF_PIC_LIST_0, cMvBi[0], cMvPredBi[0][iRefIdxBi[0]], aaiMvpIdxBi[0][iRefIdxBi[0]], uiBits[2], uiCostBi);
            if(!pcCU->getSlice()->getMvdL1ZeroFlag())
            {
              xCopyAMVPInfo(&aacAMVPInfo[1][iRefIdxBi[1]], pcCU->getCUMvField(REF_PIC_LIST_1)->getAMVPInfo());
              xCheckBestMVP(pcCU, REF_PIC_LIST_1, cMvBi[1], cMvPredBi[1][iRefIdxBi[1]], aaiMvpIdxBi[1][iRefIdxBi[1]], uiBits[2], uiCostBi);
            }
          }
          break;
        }
      } // for loop-iter
    } // if (B_SLICE)

#if AMP_MRG
    } //end if bTestNormalMC
#endif
    //  Clear Motion Field
    pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField( TComMvField(), ePartSize, uiPartAddr, 0, iPartIdx );
    pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField( TComMvField(), ePartSize, uiPartAddr, 0, iPartIdx );
    pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvd    ( cMvZero,       ePartSize, uiPartAddr, 0, iPartIdx );
    pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvd    ( cMvZero,       ePartSize, uiPartAddr, 0, iPartIdx );

    pcCU->setMVPIdxSubParts( -1, REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
    pcCU->setMVPNumSubParts( -1, REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
    pcCU->setMVPIdxSubParts( -1, REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
    pcCU->setMVPNumSubParts( -1, REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));

    UInt uiMEBits = 0;
    // Set Motion Field_
    cMv[1] = mvValidList1;
	
    iRefIdx[1] = refIdxValidList1;
    uiBits[1] = bitsValidList1;
    uiCost[1] = costValidList1;

#if AMP_MRG
    if (bTestNormalMC)
    {
#endif
    if ( uiCostBi <= uiCost[0] && uiCostBi <= uiCost[1])
    {
      uiLastMode = 2;
      pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMv( cMvBi[0], ePartSize, uiPartAddr, 0, iPartIdx );
      pcCU->getCUMvField(REF_PIC_LIST_0)->setAllRefIdx( iRefIdxBi[0], ePartSize, uiPartAddr, 0, iPartIdx );
      pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMv( cMvBi[1], ePartSize, uiPartAddr, 0, iPartIdx );
      pcCU->getCUMvField(REF_PIC_LIST_1)->setAllRefIdx( iRefIdxBi[1], ePartSize, uiPartAddr, 0, iPartIdx );

      TempMv = cMvBi[0] - cMvPredBi[0][iRefIdxBi[0]];
      pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvd    ( TempMv,                 ePartSize, uiPartAddr, 0, iPartIdx );

      TempMv = cMvBi[1] - cMvPredBi[1][iRefIdxBi[1]];
      pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvd    ( TempMv,                 ePartSize, uiPartAddr, 0, iPartIdx );

      pcCU->setInterDirSubParts( 3, uiPartAddr, iPartIdx, pcCU->getDepth(0) );

      pcCU->setMVPIdxSubParts( aaiMvpIdxBi[0][iRefIdxBi[0]], REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
      pcCU->setMVPNumSubParts( aaiMvpNum[0][iRefIdxBi[0]], REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
      pcCU->setMVPIdxSubParts( aaiMvpIdxBi[1][iRefIdxBi[1]], REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
      pcCU->setMVPNumSubParts( aaiMvpNum[1][iRefIdxBi[1]], REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));

      uiMEBits = uiBits[2];
    }
    else if ( uiCost[0] <= uiCost[1] )
    {
      uiLastMode = 0;
      pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMv( cMv[0], ePartSize, uiPartAddr, 0, iPartIdx );
      pcCU->getCUMvField(REF_PIC_LIST_0)->setAllRefIdx( iRefIdx[0], ePartSize, uiPartAddr, 0, iPartIdx );

      TempMv = cMv[0] - cMvPred[0][iRefIdx[0]];
      pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvd    ( TempMv,                 ePartSize, uiPartAddr, 0, iPartIdx );

      pcCU->setInterDirSubParts( 1, uiPartAddr, iPartIdx, pcCU->getDepth(0) );

      pcCU->setMVPIdxSubParts( aaiMvpIdx[0][iRefIdx[0]], REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
      pcCU->setMVPNumSubParts( aaiMvpNum[0][iRefIdx[0]], REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));

      uiMEBits = uiBits[0];
    }
    else
    {
      uiLastMode = 1;
      pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMv( cMv[1], ePartSize, uiPartAddr, 0, iPartIdx );
      pcCU->getCUMvField(REF_PIC_LIST_1)->setAllRefIdx( iRefIdx[1], ePartSize, uiPartAddr, 0, iPartIdx );

      TempMv = cMv[1] - cMvPred[1][iRefIdx[1]];
      pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvd    ( TempMv,                 ePartSize, uiPartAddr, 0, iPartIdx );

      pcCU->setInterDirSubParts( 2, uiPartAddr, iPartIdx, pcCU->getDepth(0) );

      pcCU->setMVPIdxSubParts( aaiMvpIdx[1][iRefIdx[1]], REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
      pcCU->setMVPNumSubParts( aaiMvpNum[1][iRefIdx[1]], REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));

      uiMEBits = uiBits[1];
    }
#if AMP_MRG
    } // end if bTestNormalMC
#endif

    if ( pcCU->getPartitionSize( uiPartAddr ) != SIZE_2Nx2N )
    {
      UInt uiMRGInterDir = 0;
      TComMvField cMRGMvField[2];
      UInt uiMRGIndex = 0;

      UInt uiMEInterDir = 0;
      TComMvField cMEMvField[2];

      m_pcRdCost->selectMotionLambda( true, 0, pcCU->getCUTransquantBypass(uiPartAddr) );

#if AMP_MRG
      // calculate ME cost
      Distortion uiMEError = std::numeric_limits<Distortion>::max();
      Distortion uiMECost  = std::numeric_limits<Distortion>::max();

      if (bTestNormalMC)
      {
        xGetInterPredictionError( pcCU, pcOrgYuv, iPartIdx, uiMEError, m_pcEncCfg->getUseHADME() );
        uiMECost = uiMEError + m_pcRdCost->getCost( uiMEBits );
      }
#else
      // calculate ME cost
      Distortion uiMEError = std::numeric_limits<Distortion>::max();
      xGetInterPredictionError( pcCU, pcOrgYuv, iPartIdx, uiMEError, m_pcEncCfg->getUseHADME() );
      Distortion uiMECost = uiMEError + m_pcRdCost->getCost( uiMEBits );
#endif
      // save ME result.
      uiMEInterDir = pcCU->getInterDir( uiPartAddr );
      TComDataCU::getMvField( pcCU, uiPartAddr, REF_PIC_LIST_0, cMEMvField[0] );
      TComDataCU::getMvField( pcCU, uiPartAddr, REF_PIC_LIST_1, cMEMvField[1] );

      // find Merge result
      Distortion uiMRGCost = std::numeric_limits<Distortion>::max();

      xMergeEstimation( pcCU, pcOrgYuv, iPartIdx, uiMRGInterDir, cMRGMvField, uiMRGIndex, uiMRGCost, cMvFieldNeighbours, uhInterDirNeighbours, numValidMergeCand);

      if ( uiMRGCost < uiMECost )
      {
        // set Merge result
        pcCU->setMergeFlagSubParts ( true,          uiPartAddr, iPartIdx, pcCU->getDepth( uiPartAddr ) );
        pcCU->setMergeIndexSubParts( uiMRGIndex,    uiPartAddr, iPartIdx, pcCU->getDepth( uiPartAddr ) );
        pcCU->setInterDirSubParts  ( uiMRGInterDir, uiPartAddr, iPartIdx, pcCU->getDepth( uiPartAddr ) );
        pcCU->getCUMvField( REF_PIC_LIST_0 )->setAllMvField( cMRGMvField[0], ePartSize, uiPartAddr, 0, iPartIdx );
        pcCU->getCUMvField( REF_PIC_LIST_1 )->setAllMvField( cMRGMvField[1], ePartSize, uiPartAddr, 0, iPartIdx );

        pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvd    ( cMvZero,            ePartSize, uiPartAddr, 0, iPartIdx );
        pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvd    ( cMvZero,            ePartSize, uiPartAddr, 0, iPartIdx );

        pcCU->setMVPIdxSubParts( -1, REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        pcCU->setMVPNumSubParts( -1, REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        pcCU->setMVPIdxSubParts( -1, REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        pcCU->setMVPNumSubParts( -1, REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
      }
      else
      {
        // set ME result
        pcCU->setMergeFlagSubParts( false,        uiPartAddr, iPartIdx, pcCU->getDepth( uiPartAddr ) );
        pcCU->setInterDirSubParts ( uiMEInterDir, uiPartAddr, iPartIdx, pcCU->getDepth( uiPartAddr ) );
        pcCU->getCUMvField( REF_PIC_LIST_0 )->setAllMvField( cMEMvField[0], ePartSize, uiPartAddr, 0, iPartIdx );
        pcCU->getCUMvField( REF_PIC_LIST_1 )->setAllMvField( cMEMvField[1], ePartSize, uiPartAddr, 0, iPartIdx );
      }
    }

    //  MC
    motionCompensation ( pcCU, pcPredYuv, REF_PIC_LIST_X, iPartIdx );

  } //  end of for ( Int iPartIdx = 0; iPartIdx < iNumPart; iPartIdx++ )

  setWpScalingDistParam( pcCU, -1, REF_PIC_LIST_X );
 // CTUW = iRoiWidth;
 // CTUH = iRoiHeight;
 // myfile << CTUH << ',' << CTUW << endl;
  return;
}


// AMVP
Void TEncSearch::xEstimateMvPredAMVP( TComDataCU* pcCU, TComYuv* pcOrgYuv, UInt uiPartIdx, RefPicList eRefPicList, Int iRefIdx, TComMv& rcMvPred, Bool bFilled, Distortion* puiDistBiP )
{

  AMVPInfo*  pcAMVPInfo = pcCU->getCUMvField(eRefPicList)->getAMVPInfo();

  TComMv     cBestMv;
  Int        iBestIdx   = 0;
  TComMv     cZeroMv;
  TComMv     cMvPred;
  Distortion uiBestCost = std::numeric_limits<Distortion>::max();
  UInt       uiPartAddr = 0;
  Int        iRoiWidth, iRoiHeight;
  Int        i;
 
  pcCU->getPartIndexAndSize( uiPartIdx, uiPartAddr, iRoiWidth, iRoiHeight );
 

  // Fill the MV Candidates
  if (!bFilled)
  {
    pcCU->fillMvpCand( uiPartIdx, uiPartAddr, eRefPicList, iRefIdx, pcAMVPInfo );
  }

  // initialize Mvp index & Mvp
  iBestIdx = 0;
  cBestMv  = pcAMVPInfo->m_acMvCand[0];
  if (pcAMVPInfo->iN <= 1)
  {
    rcMvPred = cBestMv;

    pcCU->setMVPIdxSubParts( iBestIdx, eRefPicList, uiPartAddr, uiPartIdx, pcCU->getDepth(uiPartAddr));
    pcCU->setMVPNumSubParts( pcAMVPInfo->iN, eRefPicList, uiPartAddr, uiPartIdx, pcCU->getDepth(uiPartAddr));

    if(pcCU->getSlice()->getMvdL1ZeroFlag() && eRefPicList==REF_PIC_LIST_1)
    {
      (*puiDistBiP) = xGetTemplateCost( pcCU, uiPartAddr, pcOrgYuv, &m_cYuvPredTemp, rcMvPred, 0, AMVP_MAX_NUM_CANDS, eRefPicList, iRefIdx, iRoiWidth, iRoiHeight);
    }
    return;
  }

  if (bFilled)
  {
    assert(pcCU->getMVPIdx(eRefPicList,uiPartAddr) >= 0);
    rcMvPred = pcAMVPInfo->m_acMvCand[pcCU->getMVPIdx(eRefPicList,uiPartAddr)];
    return;
  }

  m_cYuvPredTemp.clear();
  //-- Check Minimum Cost.
  for ( i = 0 ; i < pcAMVPInfo->iN; i++)
  {
    Distortion uiTmpCost;
    uiTmpCost = xGetTemplateCost( pcCU, uiPartAddr, pcOrgYuv, &m_cYuvPredTemp, pcAMVPInfo->m_acMvCand[i], i, AMVP_MAX_NUM_CANDS, eRefPicList, iRefIdx, iRoiWidth, iRoiHeight);
    if ( uiBestCost > uiTmpCost )
    {
      uiBestCost = uiTmpCost;
      cBestMv   = pcAMVPInfo->m_acMvCand[i];
      iBestIdx  = i;
      (*puiDistBiP) = uiTmpCost;
    }
  }

  m_cYuvPredTemp.clear();

  // Setting Best MVP
  rcMvPred = cBestMv;
  pcCU->setMVPIdxSubParts( iBestIdx, eRefPicList, uiPartAddr, uiPartIdx, pcCU->getDepth(uiPartAddr));
  pcCU->setMVPNumSubParts( pcAMVPInfo->iN, eRefPicList, uiPartAddr, uiPartIdx, pcCU->getDepth(uiPartAddr));
  return;
  
}

UInt TEncSearch::xGetMvpIdxBits(Int iIdx, Int iNum)
{
  assert(iIdx >= 0 && iNum >= 0 && iIdx < iNum);

  if (iNum == 1)
  {
    return 0;
  }

  UInt uiLength = 1;
  Int iTemp = iIdx;
  if ( iTemp == 0 )
  {
    return uiLength;
  }

  Bool bCodeLast = ( iNum-1 > iTemp );

  uiLength += (iTemp-1);

  if( bCodeLast )
  {
    uiLength++;
  }

  return uiLength;
}

Void TEncSearch::xGetBlkBits( PartSize eCUMode, Bool bPSlice, Int iPartIdx, UInt uiLastMode, UInt uiBlkBit[3])
{
  if ( eCUMode == SIZE_2Nx2N )
  {
    uiBlkBit[0] = (! bPSlice) ? 3 : 1;
    uiBlkBit[1] = 3;
    uiBlkBit[2] = 5;
  }
  else if ( (eCUMode == SIZE_2NxN || eCUMode == SIZE_2NxnU) || eCUMode == SIZE_2NxnD )
  {
    UInt aauiMbBits[2][3][3] = { { {0,0,3}, {0,0,0}, {0,0,0} } , { {5,7,7}, {7,5,7}, {9-3,9-3,9-3} } };
    if ( bPSlice )
    {
      uiBlkBit[0] = 3;
      uiBlkBit[1] = 0;
      uiBlkBit[2] = 0;
    }
    else
    {
      ::memcpy( uiBlkBit, aauiMbBits[iPartIdx][uiLastMode], 3*sizeof(UInt) );
    }
  }
  else if ( (eCUMode == SIZE_Nx2N || eCUMode == SIZE_nLx2N) || eCUMode == SIZE_nRx2N )
  {
    UInt aauiMbBits[2][3][3] = { { {0,2,3}, {0,0,0}, {0,0,0} } , { {5,7,7}, {7-2,7-2,9-2}, {9-3,9-3,9-3} } };
    if ( bPSlice )
    {
      uiBlkBit[0] = 3;
      uiBlkBit[1] = 0;
      uiBlkBit[2] = 0;
    }
    else
    {
      ::memcpy( uiBlkBit, aauiMbBits[iPartIdx][uiLastMode], 3*sizeof(UInt) );
    }
  }
  else if ( eCUMode == SIZE_NxN )
  {
    uiBlkBit[0] = (! bPSlice) ? 3 : 1;
    uiBlkBit[1] = 3;
    uiBlkBit[2] = 5;
  }
  else
  {
    printf("Wrong!\n");
    assert( 0 );
  }
}

Void TEncSearch::xCopyAMVPInfo (AMVPInfo* pSrc, AMVPInfo* pDst)
{
  pDst->iN = pSrc->iN;
  for (Int i = 0; i < pSrc->iN; i++)
  {
    pDst->m_acMvCand[i] = pSrc->m_acMvCand[i];
  }
}

Void TEncSearch::xCheckBestMVP ( TComDataCU* pcCU, RefPicList eRefPicList, TComMv cMv, TComMv& rcMvPred, Int& riMVPIdx, UInt& ruiBits, Distortion& ruiCost )
{
  AMVPInfo* pcAMVPInfo = pcCU->getCUMvField(eRefPicList)->getAMVPInfo();
  
  assert(pcAMVPInfo->m_acMvCand[riMVPIdx] == rcMvPred);

  if (pcAMVPInfo->iN < 2)
  {
    return;
  }

  m_pcRdCost->selectMotionLambda( true, 0, pcCU->getCUTransquantBypass(0) );
  m_pcRdCost->setCostScale ( 0    );

  Int iBestMVPIdx = riMVPIdx;

  m_pcRdCost->setPredictor( rcMvPred );
  Int iOrgMvBits  = m_pcRdCost->getBitsOfVectorWithPredictor(cMv.getHor(), cMv.getVer());
  iOrgMvBits += m_auiMVPIdxCost[riMVPIdx][AMVP_MAX_NUM_CANDS];
  Int iBestMvBits = iOrgMvBits;

  for (Int iMVPIdx = 0; iMVPIdx < pcAMVPInfo->iN; iMVPIdx++)
  {
    if (iMVPIdx == riMVPIdx)
    {
      continue;
    }

    m_pcRdCost->setPredictor( pcAMVPInfo->m_acMvCand[iMVPIdx] );

    Int iMvBits = m_pcRdCost->getBitsOfVectorWithPredictor(cMv.getHor(), cMv.getVer());
    iMvBits += m_auiMVPIdxCost[iMVPIdx][AMVP_MAX_NUM_CANDS];

    if (iMvBits < iBestMvBits)
    {
      iBestMvBits = iMvBits;
      iBestMVPIdx = iMVPIdx;
    }
  }

  if (iBestMVPIdx != riMVPIdx)  //if changed
  {
    rcMvPred = pcAMVPInfo->m_acMvCand[iBestMVPIdx];

    riMVPIdx = iBestMVPIdx;
    UInt uiOrgBits = ruiBits;
    ruiBits = uiOrgBits - iOrgMvBits + iBestMvBits;
    ruiCost = (ruiCost - m_pcRdCost->getCost( uiOrgBits ))  + m_pcRdCost->getCost( ruiBits );
  }
  
}


Distortion TEncSearch::xGetTemplateCost( TComDataCU* pcCU,
                                         UInt        uiPartAddr,
                                         TComYuv*    pcOrgYuv,
                                         TComYuv*    pcTemplateCand,
                                         TComMv      cMvCand,
                                         Int         iMVPIdx,
                                         Int         iMVPNum,
                                         RefPicList  eRefPicList,
                                         Int         iRefIdx,
                                         Int         iSizeX,
                                         Int         iSizeY
                                         )
{
  Distortion uiCost = std::numeric_limits<Distortion>::max();

  TComPicYuv* pcPicYuvRef = pcCU->getSlice()->getRefPic( eRefPicList, iRefIdx )->getPicYuvRec();

  pcCU->clipMv( cMvCand );

  // prediction pattern
  if ( pcCU->getSlice()->testWeightPred() && pcCU->getSlice()->getSliceType()==P_SLICE )
  {
    xPredInterBlk( COMPONENT_Y, pcCU, pcPicYuvRef, uiPartAddr, &cMvCand, iSizeX, iSizeY, pcTemplateCand, true, pcCU->getSlice()->getSPS()->getBitDepth(CHANNEL_TYPE_LUMA) );
  }
  else
  {
    xPredInterBlk( COMPONENT_Y, pcCU, pcPicYuvRef, uiPartAddr, &cMvCand, iSizeX, iSizeY, pcTemplateCand, false, pcCU->getSlice()->getSPS()->getBitDepth(CHANNEL_TYPE_LUMA) );
  }

  if ( pcCU->getSlice()->testWeightPred() && pcCU->getSlice()->getSliceType()==P_SLICE )
  {
    xWeightedPredictionUni( pcCU, pcTemplateCand, uiPartAddr, iSizeX, iSizeY, eRefPicList, pcTemplateCand, iRefIdx );
  }

  // calc distortion

  uiCost = m_pcRdCost->getDistPart( pcCU->getSlice()->getSPS()->getBitDepth(CHANNEL_TYPE_LUMA), pcTemplateCand->getAddr(COMPONENT_Y, uiPartAddr), pcTemplateCand->getStride(COMPONENT_Y), pcOrgYuv->getAddr(COMPONENT_Y, uiPartAddr), pcOrgYuv->getStride(COMPONENT_Y), iSizeX, iSizeY, COMPONENT_Y, DF_SAD );
  uiCost = (UInt) m_pcRdCost->calcRdCost( m_auiMVPIdxCost[iMVPIdx][iMVPNum], uiCost, DF_SAD );
  return uiCost;
}


Void TEncSearch::xMotionEstimation( TComDataCU* pcCU, TComYuv* pcYuvOrg, Int iPartIdx, RefPicList eRefPicList, TComMv* pcMvPred, Int iRefIdxPred, TComMv& rcMv, UInt& ruiBits, Distortion& ruiCost, Bool bBi  )
{
  UInt          uiPartAddr;
  Int           iRoiWidth;
  Int           iRoiHeight;

  TComMv        cMvHalf, cMvQter;
  TComMv        cMvSrchRngLT;
  TComMv        cMvSrchRngRB;
  // Distortion   INTCOST=0;
  TComYuv*      pcYuv = pcYuvOrg;
   
  assert(eRefPicList < MAX_NUM_REF_LIST_ADAPT_SR && iRefIdxPred<Int(MAX_IDX_ADAPT_SR));
  m_iSearchRange = m_aaiAdaptSR[eRefPicList][iRefIdxPred];

  Int           iSrchRng      = ( bBi ? m_bipredSearchRange : m_iSearchRange );
  TComPattern   tmpPattern;
  TComPattern*  pcPatternKey  = &tmpPattern;

  Double        fWeight       = 1.0;

  pcCU->getPartIndexAndSize( iPartIdx, uiPartAddr, iRoiWidth, iRoiHeight );

  if ( bBi ) // Bipredictive ME
  {
    TComYuv*  pcYuvOther = &m_acYuvPred[1-(Int)eRefPicList];
    pcYuv                = &m_cYuvPredTemp;

    pcYuvOrg->copyPartToPartYuv( pcYuv, uiPartAddr, iRoiWidth, iRoiHeight );

    pcYuv->removeHighFreq( pcYuvOther, uiPartAddr, iRoiWidth, iRoiHeight, pcCU->getSlice()->getSPS()->getBitDepths().recon, m_pcEncCfg->getClipForBiPredMeEnabled() );

    fWeight = 0.5;
  }
  m_cDistParam.bIsBiPred = bBi;

  //  Search key pattern initialization
  pcPatternKey->initPattern( pcYuv->getAddr  ( COMPONENT_Y, uiPartAddr ),
                             iRoiWidth,
                             iRoiHeight,
                             pcYuv->getStride(COMPONENT_Y),
                             pcCU->getSlice()->getSPS()->getBitDepth(CHANNEL_TYPE_LUMA) );

  Pel*        piRefY      = pcCU->getSlice()->getRefPic( eRefPicList, iRefIdxPred )->getPicYuvRec()->getAddr( COMPONENT_Y, pcCU->getCtuRsAddr(), pcCU->getZorderIdxInCtu() + uiPartAddr );
  Int         iRefStride  = pcCU->getSlice()->getRefPic( eRefPicList, iRefIdxPred )->getPicYuvRec()->getStride(COMPONENT_Y);

  TComMv      cMvPred = *pcMvPred;

  if ( bBi )
  {
	  
    xSetSearchRange   ( pcCU, rcMv   , iSrchRng, cMvSrchRngLT, cMvSrchRngRB );
  }
  else
  {
	  
    xSetSearchRange   ( pcCU, cMvPred, iSrchRng, cMvSrchRngLT, cMvSrchRngRB );
  }

  m_pcRdCost->selectMotionLambda(true, 0, pcCU->getCUTransquantBypass(uiPartAddr) );

  m_pcRdCost->setPredictor  ( *pcMvPred );
  m_pcRdCost->setCostScale  ( 2 );

  setWpScalingDistParam( pcCU, iRefIdxPred, eRefPicList );
  //  Do integer search
  if ( (m_motionEstimationSearchMethod==MESEARCH_FULL) || bBi )
  {
    xPatternSearch      ( pcPatternKey, piRefY, iRefStride, &cMvSrchRngLT, &cMvSrchRngRB, rcMv, ruiCost );
  }
  else
  {
    rcMv = *pcMvPred;
    const TComMv *pIntegerMv2Nx2NPred=0;
    if (pcCU->getPartitionSize(0) != SIZE_2Nx2N || pcCU->getDepth(0) != 0)
    {
      pIntegerMv2Nx2NPred = &(m_integerMv2Nx2N[eRefPicList][iRefIdxPred]);
    }
    
    // EMI: Save Block width and height in global variables, to use in our NN
    // TODO: Code Cleaning
    PUHeight = iRoiHeight;
    PUWidth = iRoiWidth;


    xPatternSearchFast  ( pcCU, pcPatternKey, piRefY, iRefStride, &cMvSrchRngLT, &cMvSrchRngRB, rcMv, ruiCost, pIntegerMv2Nx2NPred );
    if (pcCU->getPartitionSize(0) == SIZE_2Nx2N)
    {
      m_integerMv2Nx2N[eRefPicList][iRefIdxPred] = rcMv;
    }
  }

  m_pcRdCost->selectMotionLambda( true, 0, pcCU->getCUTransquantBypass(uiPartAddr) );

  m_pcRdCost->setCostScale ( 1 );
    
  const Bool bIsLosslessCoded = pcCU->getCUTransquantBypass(uiPartAddr) != 0;
  xPatternSearchFracDIF( bIsLosslessCoded, pcPatternKey, piRefY, iRefStride, &rcMv, cMvHalf, cMvQter, ruiCost );

  m_pcRdCost->setCostScale( 0 );

  // EMI: Modification
  
  /* 
  Fractional Motion Estimation values computed by standard are stored in TComMv variables cMvHalf & cMvQter
  We create other TComMv variables, and replace the standard values with our NN predicted values
  Our NN modifies global variables MVX_HALF & MVX_QRTER, which in return are set used to set our new Mv
  */
  TComMv MV_HALF, MV_QRTER;
  MV_HALF.setHor(MVX_HALF);
  MV_HALF.setVer(MVY_HALF);
  MV_QRTER.setHor(MVX_QRTER);
  MV_QRTER.setVer(MVY_QRTER);

  // For finding Integer Motion Estimation, Set Horizontal and Vertical values to zero:

  // MV_HALF.setHor(0);
  // MV_HALF.setVer(0);
  // MV_QRTER.setHor(0);
  // MV_QRTER.setVer(0);

  /* 
  EMI: To Write the errors and output MV in a CSV file:
  Real values for errors: U,V,H           - NN values for errors: IN[]
  Real values for MV: cMvHalf, cMvQter    - NN values for MV: MV_HALF, MV_QRTER
  Block Width and Height: iRoiWidth, iRoiHeight
  */
  
  // ofstream mv_nn;
  // ofstream errors;
  // errors.open("/home/emi/git-repos/data/HM16.9/extract_data/SSE_errors.csv", ios::app);
  // mv_nn.open("/home/emi/git-repos/data/HM16.9/extract_data/mv_nn.csv", ios::app);
  // errors << U1 << ',' << V1 << ',' << U2 << ',' << H1 << ',' << C << ',' << H2 << ',' << U3 << ',' << V2 << ',' << U4 << ',' << iRoiHeight << ',' << iRoiWidth << endl;
  // errors << ',' << xP << ',' << yP << ',' << PIdx  << ',' << PAddr << endl;
  // errors << ',' << uiPartAddr << ',' << iPartIdx << endl;
  
  /*
  Write the values of the output class directly instead of coordinates:
  Half * 0.5 + Quarter * 0.25:  results in range from -0.75->0.75
  Add both X & Y + 0.75:        range is now 0->1.5
  Multiply X by 4:              X values are now [0, 1, 2, 3, 4, 5, 6]
  Multiply Y by 4*7=28:         Y values are now [0, 7, 14, 21, 28, 35, 42]
  Adding X+Y results in the desired output class, given that the mapping starts from 
  0 for top left corner, 24 center, and 48 for bottom right corner
  */
  
  // int MV_X = (((cMvHalf.getHor() * 0.5) + (cMvQter.getHor() * 0.25)) + 0.75) * 4;
  // int MV_Y = (((cMvHalf.getVer() * 0.5) + (cMvQter.getVer() * 0.25)) + 0.75) * 28;
  // int OUT_CLASS = MV_Y + MV_X;
  // mv_nn << OUT_CLASS << endl;

  // Replace Motion Vector with values computed by our NN

  rcMv <<= 2;
  // rcMv += (cMvHalf <<= 1);
  // rcMv += cMvQter;
  rcMv += (MV_HALF <<= 1);
  rcMv += MV_QRTER;
  
  // End of modification

  UInt uiMvBits = m_pcRdCost->getBitsOfVectorWithPredictor( rcMv.getHor(), rcMv.getVer() );

  ruiBits      += uiMvBits;
  ruiCost       = (Distortion)( floor( fWeight * ( (Double)ruiCost - (Double)m_pcRdCost->getCost( uiMvBits ) ) ) + (Double)m_pcRdCost->getCost( ruiBits ) );
}


Void TEncSearch::xSetSearchRange ( const TComDataCU* const pcCU, const TComMv& cMvPred, const Int iSrchRng,
                                   TComMv& rcMvSrchRngLT, TComMv& rcMvSrchRngRB )
{
  Int  iMvShift = 2;
  TComMv cTmpMvPred = cMvPred;
  pcCU->clipMv( cTmpMvPred );

  rcMvSrchRngLT.setHor( cTmpMvPred.getHor() - (iSrchRng << iMvShift) );
  rcMvSrchRngLT.setVer( cTmpMvPred.getVer() - (iSrchRng << iMvShift) );

  rcMvSrchRngRB.setHor( cTmpMvPred.getHor() + (iSrchRng << iMvShift) );
  rcMvSrchRngRB.setVer( cTmpMvPred.getVer() + (iSrchRng << iMvShift) );
  pcCU->clipMv        ( rcMvSrchRngLT );
  pcCU->clipMv        ( rcMvSrchRngRB );

#if ME_ENABLE_ROUNDING_OF_MVS
  rcMvSrchRngLT.divideByPowerOf2(iMvShift);
  rcMvSrchRngRB.divideByPowerOf2(iMvShift);
#else
  rcMvSrchRngLT >>= iMvShift;
  rcMvSrchRngRB >>= iMvShift;
#endif
}


Void TEncSearch::xPatternSearch(const TComPattern* const pcPatternKey,
	const Pel*               piRefY,
	const Int                iRefStride,
	const TComMv* const      pcMvSrchRngLT,
	const TComMv* const      pcMvSrchRngRB,
	TComMv&      rcMv,
	Distortion&  ruiSAD)
{
	Int   iSrchRngHorLeft = pcMvSrchRngLT->getHor();
	Int   iSrchRngHorRight = pcMvSrchRngRB->getHor();
	Int   iSrchRngVerTop = pcMvSrchRngLT->getVer();
	Int   iSrchRngVerBottom = pcMvSrchRngRB->getVer();

	Distortion  uiSad;
	Distortion  uiSadBest = std::numeric_limits<Distortion>::max();
	Int         iBestX = 0;
	Int         iBestY = 0;


	m_pcRdCost->setDistParam(pcPatternKey, piRefY, iRefStride, m_cDistParam);

	// fast encoder decision: use subsampled SAD for integer ME
	if (m_pcEncCfg->getFastInterSearchMode() == FASTINTERSEARCH_MODE1 || m_pcEncCfg->getFastInterSearchMode() == FASTINTERSEARCH_MODE3)
	{
		if (m_cDistParam.iRows > 8)
		{
			m_cDistParam.iSubShift = 1;
		}
	}

	piRefY += (iSrchRngVerTop * iRefStride);

	for (Int y = iSrchRngVerTop; y <= iSrchRngVerBottom; y++)
	{
		for (Int x = iSrchRngHorLeft; x <= iSrchRngHorRight; x++)
		{
			//  find min. distortion position
			m_cDistParam.pCur = piRefY + x;

			setDistParamComp(COMPONENT_Y);

			m_cDistParam.bitDepth = pcPatternKey->getBitDepthY();
			uiSad = m_cDistParam.DistFunc(&m_cDistParam);

			// motion cost
			uiSad += m_pcRdCost->getCostOfVectorWithPredictor(x, y);

			if (uiSad < uiSadBest)
			{
				uiSadBest = uiSad;
				iBestX = x;
				iBestY = y;
				m_cDistParam.m_maximumDistortionForEarlyExit = uiSad;
			}
		}
		piRefY += iRefStride;
	}




		rcMv.set(iBestX, iBestY);


		ruiSAD = uiSadBest - m_pcRdCost->getCostOfVectorWithPredictor(iBestX, iBestY);

		//getchar();
		return;
	}


Void TEncSearch::xPatternSearchFast( const TComDataCU* const  pcCU,
                                     const TComPattern* const pcPatternKey,
                                     const Pel* const         piRefY,
                                     const Int                iRefStride,
                                     const TComMv* const      pcMvSrchRngLT,
                                     const TComMv* const      pcMvSrchRngRB,
                                     TComMv&                  rcMv,
                                     Distortion&              ruiSAD,
                                     const TComMv* const      pIntegerMv2Nx2NPred )
{
  assert (MD_LEFT < NUM_MV_PREDICTORS);
  pcCU->getMvPredLeft       ( m_acMvPredictors[MD_LEFT] );
  assert (MD_ABOVE < NUM_MV_PREDICTORS);
  pcCU->getMvPredAbove      ( m_acMvPredictors[MD_ABOVE] );
  assert (MD_ABOVE_RIGHT < NUM_MV_PREDICTORS);
  pcCU->getMvPredAboveRight ( m_acMvPredictors[MD_ABOVE_RIGHT] );

  switch ( m_motionEstimationSearchMethod )
  {
    case MESEARCH_DIAMOND:
      xTZSearch( pcCU, pcPatternKey, piRefY, iRefStride, pcMvSrchRngLT, pcMvSrchRngRB, rcMv, ruiSAD, pIntegerMv2Nx2NPred, false );
	  
      C = array_e[0];
      for (int i = 1; i <=index_ref - 1; i++)
      {
        if (array_e[i] < C)
          C = array_e[i];

      }
	 
      // index_ref = index_ref + 1;
      U1 = array_e[index_ref];
      V1 = array_e[index_ref + 1];
      U2 = array_e[index_ref + 2];
      H1 = array_e[index_ref + 3];	  
      H2 = array_e[index_ref + 4];
      U3 = array_e[index_ref + 5];
      V2 = array_e[index_ref + 6];
      U4 = array_e[index_ref + 7];
      	  
      // EMI: neural network implementation

      NN_pred();      

      //end of neural network code

      break;



    case MESEARCH_SELECTIVE:
      xTZSearchSelective( pcCU, pcPatternKey, piRefY, iRefStride, pcMvSrchRngLT, pcMvSrchRngRB, rcMv, ruiSAD, pIntegerMv2Nx2NPred );
      break;

    case MESEARCH_DIAMOND_ENHANCED:
      xTZSearch( pcCU, pcPatternKey, piRefY, iRefStride, pcMvSrchRngLT, pcMvSrchRngRB, rcMv, ruiSAD, pIntegerMv2Nx2NPred, true );
      break;

    case MESEARCH_FULL: // shouldn't get here.
    default:
      break;
  }
}


Void TEncSearch::xTZSearch( const TComDataCU* const pcCU,
                            const TComPattern* const pcPatternKey,
                            const Pel* const         piRefY,
                            const Int                iRefStride,
                            const TComMv* const      pcMvSrchRngLT,
                            const TComMv* const      pcMvSrchRngRB,
                            TComMv&                  rcMv,
                            Distortion&              ruiSAD,
                            const TComMv* const      pIntegerMv2Nx2NPred,
                            const Bool               bExtendedSettings)
{
  const Bool bUseAdaptiveRaster                      = bExtendedSettings;
  const Int  iRaster                                 = 5;
  const Bool bTestOtherPredictedMV                   = bExtendedSettings;
  const Bool bTestZeroVector                         = true;
  const Bool bTestZeroVectorStart                    = bExtendedSettings;
  const Bool bTestZeroVectorStop                     = false;
  const Bool bFirstSearchDiamond                     = true;  // 1 = xTZ8PointDiamondSearch   0 = xTZ8PointSquareSearch
  const Bool bFirstCornersForDiamondDist1            = bExtendedSettings;
  const Bool bFirstSearchStop                        = m_pcEncCfg->getFastMEAssumingSmootherMVEnabled();
  const UInt uiFirstSearchRounds                     = 3;     // first search stop X rounds after best match (must be >=1)
  const Bool bEnableRasterSearch                     = true;
  const Bool bAlwaysRasterSearch                     = bExtendedSettings;  // true: BETTER but factor 2 slower
  const Bool bRasterRefinementEnable                 = false; // enable either raster refinement or star refinement
  const Bool bRasterRefinementDiamond                = false; // 1 = xTZ8PointDiamondSearch   0 = xTZ8PointSquareSearch
  const Bool bRasterRefinementCornersForDiamondDist1 = bExtendedSettings;
  const Bool bStarRefinementEnable                   = true;  // enable either star refinement or raster refinement
  const Bool bStarRefinementDiamond                  = true;  // 1 = xTZ8PointDiamondSearch   0 = xTZ8PointSquareSearch
  const Bool bStarRefinementCornersForDiamondDist1   = bExtendedSettings;
  const Bool bStarRefinementStop                     = false;
  const UInt uiStarRefinementRounds                  = 2;  // star refinement stop X rounds after best match (must be >=1)
  const Bool bNewZeroNeighbourhoodTest               = bExtendedSettings;

  UInt uiSearchRange = m_iSearchRange;
  pcCU->clipMv( rcMv );
#if ME_ENABLE_ROUNDING_OF_MVS
  rcMv.divideByPowerOf2(2);
#else
  rcMv >>= 2;
#endif
  // init TZSearchStruct
  IntTZSearchStruct cStruct;
  cStruct.iYStride    = iRefStride;
  cStruct.piRefY      = piRefY;
  cStruct.uiBestSad   = MAX_UINT;

  // set rcMv (Median predictor) as start point and as best point
  xTZSearchHelp( pcPatternKey, cStruct, rcMv.getHor(), rcMv.getVer(), 0, 0 );

  // test whether one of PRED_A, PRED_B, PRED_C MV is better start point than Median predictor
  if ( bTestOtherPredictedMV )
  {
    for ( UInt index = 0; index < NUM_MV_PREDICTORS; index++ )
    {
      TComMv cMv = m_acMvPredictors[index];
      pcCU->clipMv( cMv );
#if ME_ENABLE_ROUNDING_OF_MVS
      cMv.divideByPowerOf2(2);
#else
      cMv >>= 2;
#endif
      if (cMv != rcMv && (cMv.getHor() != cStruct.iBestX && cMv.getVer() != cStruct.iBestY))
      {
        // only test cMV if not obviously previously tested.
        xTZSearchHelp( pcPatternKey, cStruct, cMv.getHor(), cMv.getVer(), 0, 0 );
      }
    }
  }

  // test whether zero Mv is better start point than Median predictor
  if ( bTestZeroVector )
  {
    if ((rcMv.getHor() != 0 || rcMv.getVer() != 0) &&
        (0 != cStruct.iBestX || 0 != cStruct.iBestY))
    {
      // only test 0-vector if not obviously previously tested.
      xTZSearchHelp( pcPatternKey, cStruct, 0, 0, 0, 0 );
    }
  }

  Int   iSrchRngHorLeft   = pcMvSrchRngLT->getHor();
  Int   iSrchRngHorRight  = pcMvSrchRngRB->getHor();
  Int   iSrchRngVerTop    = pcMvSrchRngLT->getVer();
  Int   iSrchRngVerBottom = pcMvSrchRngRB->getVer();

  if (pIntegerMv2Nx2NPred != 0)
  {
    TComMv integerMv2Nx2NPred = *pIntegerMv2Nx2NPred;
    integerMv2Nx2NPred <<= 2;
    pcCU->clipMv( integerMv2Nx2NPred );
#if ME_ENABLE_ROUNDING_OF_MVS
    integerMv2Nx2NPred.divideByPowerOf2(2);
#else
    integerMv2Nx2NPred >>= 2;
#endif
    if ((rcMv != integerMv2Nx2NPred) &&
        (integerMv2Nx2NPred.getHor() != cStruct.iBestX || integerMv2Nx2NPred.getVer() != cStruct.iBestY))
    {
      // only test integerMv2Nx2NPred if not obviously previously tested.
      xTZSearchHelp(pcPatternKey, cStruct, integerMv2Nx2NPred.getHor(), integerMv2Nx2NPred.getVer(), 0, 0);
    }

    // reset search range
    TComMv cMvSrchRngLT;
    TComMv cMvSrchRngRB;
    Int iSrchRng = m_iSearchRange;
    TComMv currBestMv(cStruct.iBestX, cStruct.iBestY );
    currBestMv <<= 2;
    xSetSearchRange( pcCU, currBestMv, iSrchRng, cMvSrchRngLT, cMvSrchRngRB );
    iSrchRngHorLeft   = cMvSrchRngLT.getHor();
    iSrchRngHorRight  = cMvSrchRngRB.getHor();
    iSrchRngVerTop    = cMvSrchRngLT.getVer();
    iSrchRngVerBottom = cMvSrchRngRB.getVer();
  }

  // start search
  Int  iDist = 0;
  Int  iStartX = cStruct.iBestX;
  Int  iStartY = cStruct.iBestY;

  const Bool bBestCandidateZero = (cStruct.iBestX == 0) && (cStruct.iBestY == 0);

  // first search around best position up to now.
  // The following works as a "subsampled/log" window search around the best candidate
  for (iDist = 1; iDist <= (Int)uiSearchRange; iDist *= 2)
	  
  {
    if ( bFirstSearchDiamond == 1 )
    {
      xTZ8PointDiamondSearch ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist, bFirstCornersForDiamondDist1 );
    }
    else
    {
      xTZ8PointSquareSearch  ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist );
    }

    if ( bFirstSearchStop && ( cStruct.uiBestRound >= uiFirstSearchRounds ) ) // stop criterion
    {
      break;
    }
  }

  if (!bNewZeroNeighbourhoodTest)
  {
    // test whether zero Mv is a better start point than Median predictor
    if ( bTestZeroVectorStart && ((cStruct.iBestX != 0) || (cStruct.iBestY != 0)) )
    {
      xTZSearchHelp( pcPatternKey, cStruct, 0, 0, 0, 0 );
      if ( (cStruct.iBestX == 0) && (cStruct.iBestY == 0) )
      {
        // test its neighborhood
        for ( iDist = 1; iDist <= (Int)uiSearchRange; iDist*=2 )
        {
          xTZ8PointDiamondSearch( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, 0, 0, iDist, false );
          if ( bTestZeroVectorStop && (cStruct.uiBestRound > 0) ) // stop criterion
          {
            break;
          }
        }
      }
    }
  }
  else
  {
    // Test also zero neighbourhood but with half the range
    // It was reported that the original (above) search scheme using bTestZeroVectorStart did not
    // make sense since one would have already checked the zero candidate earlier
    // and thus the conditions for that test would have not been satisfied
    if (bTestZeroVectorStart == true && bBestCandidateZero != true)
    {
      for ( iDist = 1; iDist <= ((Int)uiSearchRange >> 1); iDist*=2 )
      {
        xTZ8PointDiamondSearch( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, 0, 0, iDist, false );
        if ( bTestZeroVectorStop && (cStruct.uiBestRound > 2) ) // stop criterion
        {
          break;
        }
      }
    }
  }

  // calculate only 2 missing points instead 8 points if cStruct.uiBestDistance == 1
  if ( cStruct.uiBestDistance == 1 )
  {
    cStruct.uiBestDistance = 0;
    xTZ2PointSearch( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB );
  }

  // raster search if distance is too big
  if (bUseAdaptiveRaster)
  {
    int iWindowSize = iRaster;
    Int   iSrchRngRasterLeft   = iSrchRngHorLeft;
    Int   iSrchRngRasterRight  = iSrchRngHorRight;
    Int   iSrchRngRasterTop    = iSrchRngVerTop;
    Int   iSrchRngRasterBottom = iSrchRngVerBottom;

    if (!(bEnableRasterSearch && ( ((Int)(cStruct.uiBestDistance) > iRaster))))
    {
      iWindowSize ++;
      iSrchRngRasterLeft /= 2;
      iSrchRngRasterRight /= 2;
      iSrchRngRasterTop /= 2;
      iSrchRngRasterBottom /= 2;
    }
    cStruct.uiBestDistance = iWindowSize;
    for ( iStartY = iSrchRngRasterTop; iStartY <= iSrchRngRasterBottom; iStartY += iWindowSize )
    {
      for ( iStartX = iSrchRngRasterLeft; iStartX <= iSrchRngRasterRight; iStartX += iWindowSize )
      {
        xTZSearchHelp( pcPatternKey, cStruct, iStartX, iStartY, 0, iWindowSize );
      }
    }
  }
  else
  {
    if ( bEnableRasterSearch && ( ((Int)(cStruct.uiBestDistance) > iRaster) || bAlwaysRasterSearch ) )
    {
      cStruct.uiBestDistance = iRaster;
      for ( iStartY = iSrchRngVerTop; iStartY <= iSrchRngVerBottom; iStartY += iRaster )
      {
        for ( iStartX = iSrchRngHorLeft; iStartX <= iSrchRngHorRight; iStartX += iRaster )
        {
          xTZSearchHelp( pcPatternKey, cStruct, iStartX, iStartY, 0, iRaster );
        }
      }
    }
  }

  // raster refinement

  if ( bRasterRefinementEnable && cStruct.uiBestDistance > 0 )
  {
    while ( cStruct.uiBestDistance > 0 )
    {
      iStartX = cStruct.iBestX;
      iStartY = cStruct.iBestY;
      if ( cStruct.uiBestDistance > 1 )
      {
        iDist = cStruct.uiBestDistance >>= 1;
        if ( bRasterRefinementDiamond == 1 )
        {
          xTZ8PointDiamondSearch ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist, bRasterRefinementCornersForDiamondDist1 );
        }
        else
        {
          xTZ8PointSquareSearch  ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist );
        }
      }

      // calculate only 2 missing points instead 8 points if cStruct.uiBestDistance == 1
      if ( cStruct.uiBestDistance == 1 )
      {
        cStruct.uiBestDistance = 0;
        if ( cStruct.ucPointNr != 0 )
        {
          xTZ2PointSearch( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB );
        }
      }
    }
  }

  // star refinement
  if ( bStarRefinementEnable && cStruct.uiBestDistance > 0 )
  {
    while ( cStruct.uiBestDistance > 0 )
    {
      iStartX = cStruct.iBestX;
      iStartY = cStruct.iBestY;
      cStruct.uiBestDistance = 0;
      cStruct.ucPointNr = 0;
      for ( iDist = 1; iDist < (Int)uiSearchRange + 1; iDist*=2 )
      {
        if ( bStarRefinementDiamond == 1 )
        {
          xTZ8PointDiamondSearch ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist, bStarRefinementCornersForDiamondDist1 );
        }
        else
        {
          xTZ8PointSquareSearch  ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist );
        }
        if ( bStarRefinementStop && (cStruct.uiBestRound >= uiStarRefinementRounds) ) // stop criterion
        {
          break;
        }
      }

      // calculate only 2 missing points instead 8 points if cStrukt.uiBestDistance == 1
      if ( cStruct.uiBestDistance == 1 )
      {
        cStruct.uiBestDistance = 0;
        if ( cStruct.ucPointNr != 0 )
        {
          xTZ2PointSearch( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB );
        }
      }
    }
  }


  // EMI: BIG DIFFERENCE!
  // getting the 8 SAD points
  iDist = 1;
  iStartX = cStruct.iBestX;
  iStartY = cStruct.iBestY;
  index_ref = counter_i;
  
  xTZ8PointSquareSearch(pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist);

  iDist = 2;
  xTZ8PointSquareSearch2(pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist);

  // write out best match
  rcMv.set( cStruct.iBestX, cStruct.iBestY );
  ruiSAD = cStruct.uiBestSad - m_pcRdCost->getCostOfVectorWithPredictor( cStruct.iBestX, cStruct.iBestY );
}


Void TEncSearch::xTZSearchSelective( const TComDataCU* const   pcCU,
                                     const TComPattern* const  pcPatternKey,
                                     const Pel* const          piRefY,
                                     const Int                 iRefStride,
                                     const TComMv* const       pcMvSrchRngLT,
                                     const TComMv* const       pcMvSrchRngRB,
                                     TComMv                   &rcMv,
                                     Distortion               &ruiSAD,
                                     const TComMv* const       pIntegerMv2Nx2NPred )
{
  const Bool bTestOtherPredictedMV    = true;
  const Bool bTestZeroVector          = true;
  const Bool bEnableRasterSearch      = true;
  const Bool bAlwaysRasterSearch      = false;  // 1: BETTER but factor 15x slower
  const Bool bStarRefinementEnable    = true;   // enable either star refinement or raster refinement
  const Bool bStarRefinementDiamond   = true;   // 1 = xTZ8PointDiamondSearch   0 = xTZ8PointSquareSearch
  const Bool bStarRefinementStop      = false;
  const UInt uiStarRefinementRounds   = 2;  // star refinement stop X rounds after best match (must be >=1)
  const UInt uiSearchRange            = m_iSearchRange;
  const Int  uiSearchRangeInitial     = m_iSearchRange >> 2;
  const Int  uiSearchStep             = 4;
  const Int  iMVDistThresh            = 8;

  Int   iSrchRngHorLeft         = pcMvSrchRngLT->getHor();
  Int   iSrchRngHorRight        = pcMvSrchRngRB->getHor();
  Int   iSrchRngVerTop          = pcMvSrchRngLT->getVer();
  Int   iSrchRngVerBottom       = pcMvSrchRngRB->getVer();
  Int   iFirstSrchRngHorLeft    = 0;
  Int   iFirstSrchRngHorRight   = 0;
  Int   iFirstSrchRngVerTop     = 0;
  Int   iFirstSrchRngVerBottom  = 0;
  Int   iStartX                 = 0;
  Int   iStartY                 = 0;
  Int   iBestX                  = 0;
  Int   iBestY                  = 0;
  Int   iDist                   = 0;

  pcCU->clipMv( rcMv );
#if ME_ENABLE_ROUNDING_OF_MVS
  rcMv.divideByPowerOf2(2);
#else
  rcMv >>= 2;
#endif
  // init TZSearchStruct
  IntTZSearchStruct cStruct;
  cStruct.iYStride    = iRefStride;
  cStruct.piRefY      = piRefY;
  cStruct.uiBestSad   = MAX_UINT;
  cStruct.iBestX = 0;
  cStruct.iBestY = 0;


  // set rcMv (Median predictor) as start point and as best point
  xTZSearchHelp( pcPatternKey, cStruct, rcMv.getHor(), rcMv.getVer(), 0, 0 );

  // test whether one of PRED_A, PRED_B, PRED_C MV is better start point than Median predictor
  if ( bTestOtherPredictedMV )
  {
    for ( UInt index = 0; index < NUM_MV_PREDICTORS; index++ )
    {
      TComMv cMv = m_acMvPredictors[index];
      pcCU->clipMv( cMv );
#if ME_ENABLE_ROUNDING_OF_MVS
      cMv.divideByPowerOf2(2);
#else
      cMv >>= 2;
#endif
      xTZSearchHelp( pcPatternKey, cStruct, cMv.getHor(), cMv.getVer(), 0, 0 );
    }
  }

  // test whether zero Mv is better start point than Median predictor
  if ( bTestZeroVector )
  {
    xTZSearchHelp( pcPatternKey, cStruct, 0, 0, 0, 0 );
  }

  if ( pIntegerMv2Nx2NPred != 0 )
  {
    TComMv integerMv2Nx2NPred = *pIntegerMv2Nx2NPred;
    integerMv2Nx2NPred <<= 2;
    pcCU->clipMv( integerMv2Nx2NPred );
#if ME_ENABLE_ROUNDING_OF_MVS
    integerMv2Nx2NPred.divideByPowerOf2(2);
#else
    integerMv2Nx2NPred >>= 2;
#endif
    xTZSearchHelp(pcPatternKey, cStruct, integerMv2Nx2NPred.getHor(), integerMv2Nx2NPred.getVer(), 0, 0);

    // reset search range
    TComMv cMvSrchRngLT;
    TComMv cMvSrchRngRB;
    Int iSrchRng = m_iSearchRange;
    TComMv currBestMv(cStruct.iBestX, cStruct.iBestY );
    currBestMv <<= 2;
    xSetSearchRange( pcCU, currBestMv, iSrchRng, cMvSrchRngLT, cMvSrchRngRB );
    iSrchRngHorLeft   = cMvSrchRngLT.getHor();
    iSrchRngHorRight  = cMvSrchRngRB.getHor();
    iSrchRngVerTop    = cMvSrchRngLT.getVer();
    iSrchRngVerBottom = cMvSrchRngRB.getVer();
  }

  // Initial search
  iBestX = cStruct.iBestX;
  iBestY = cStruct.iBestY; 
  iFirstSrchRngHorLeft    = ((iBestX - uiSearchRangeInitial) > iSrchRngHorLeft)   ? (iBestX - uiSearchRangeInitial) : iSrchRngHorLeft;
  iFirstSrchRngVerTop     = ((iBestY - uiSearchRangeInitial) > iSrchRngVerTop)    ? (iBestY - uiSearchRangeInitial) : iSrchRngVerTop;
  iFirstSrchRngHorRight   = ((iBestX + uiSearchRangeInitial) < iSrchRngHorRight)  ? (iBestX + uiSearchRangeInitial) : iSrchRngHorRight;  
  iFirstSrchRngVerBottom  = ((iBestY + uiSearchRangeInitial) < iSrchRngVerBottom) ? (iBestY + uiSearchRangeInitial) : iSrchRngVerBottom;    

  for ( iStartY = iFirstSrchRngVerTop; iStartY <= iFirstSrchRngVerBottom; iStartY += uiSearchStep )
  {
    for ( iStartX = iFirstSrchRngHorLeft; iStartX <= iFirstSrchRngHorRight; iStartX += uiSearchStep )
    {
      xTZSearchHelp( pcPatternKey, cStruct, iStartX, iStartY, 0, 0 );
      xTZ8PointDiamondSearch ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, 1, false );
      xTZ8PointDiamondSearch ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, 2, false );
    }
  }

  Int iMaxMVDistToPred = (abs(cStruct.iBestX - iBestX) > iMVDistThresh || abs(cStruct.iBestY - iBestY) > iMVDistThresh);

  //full search with early exit if MV is distant from predictors
  if ( bEnableRasterSearch && (iMaxMVDistToPred || bAlwaysRasterSearch) )
  {
    for ( iStartY = iSrchRngVerTop; iStartY <= iSrchRngVerBottom; iStartY += 1 )
    {
      for ( iStartX = iSrchRngHorLeft; iStartX <= iSrchRngHorRight; iStartX += 1 )
      {
        xTZSearchHelp( pcPatternKey, cStruct, iStartX, iStartY, 0, 1 );
      }
    }
  }
  //Smaller MV, refine around predictor
  else if ( bStarRefinementEnable && cStruct.uiBestDistance > 0 )
  {
    // start refinement
    while ( cStruct.uiBestDistance > 0 )
    {
      iStartX = cStruct.iBestX;
      iStartY = cStruct.iBestY;
      cStruct.uiBestDistance = 0;
      cStruct.ucPointNr = 0;
      for ( iDist = 1; iDist < (Int)uiSearchRange + 1; iDist*=2 )
      {
        if ( bStarRefinementDiamond == 1 )
        {
          xTZ8PointDiamondSearch ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist, false );
        }
        else
        {
          xTZ8PointSquareSearch  ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist );
        }
        if ( bStarRefinementStop && (cStruct.uiBestRound >= uiStarRefinementRounds) ) // stop criterion
        {
          break;
        }
      }

      // calculate only 2 missing points instead 8 points if cStrukt.uiBestDistance == 1
      if ( cStruct.uiBestDistance == 1 )
      {
        cStruct.uiBestDistance = 0;
        if ( cStruct.ucPointNr != 0 )
        {
          xTZ2PointSearch( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB );
        }
      }
    }
  }

  // write out best match
  rcMv.set( cStruct.iBestX, cStruct.iBestY );
  ruiSAD = cStruct.uiBestSad - m_pcRdCost->getCostOfVectorWithPredictor( cStruct.iBestX, cStruct.iBestY );

}


Void TEncSearch::xPatternSearchFracDIF(
                                       Bool         bIsLosslessCoded,
                                       TComPattern* pcPatternKey,
                                       Pel*         piRefY,
                                       Int          iRefStride,
                                       TComMv*      pcMvInt,
                                       TComMv&      rcMvHalf,
                                       TComMv&      rcMvQter,
                                       Distortion&  ruiCost
                                      )
{
  //  Reference pattern initialization (integer scale)
	
  TComPattern cPatternRoi;
  Int         iOffset    = pcMvInt->getHor() + pcMvInt->getVer() * iRefStride;
  cPatternRoi.initPattern(piRefY + iOffset,
                          pcPatternKey->getROIYWidth(),
                          pcPatternKey->getROIYHeight(),
                          iRefStride,
                          pcPatternKey->getBitDepthY());

  //  Half-pel refinement
  xExtDIFUpSamplingH ( &cPatternRoi );

  rcMvHalf = *pcMvInt;   rcMvHalf <<= 1;    // for mv-cost
  TComMv baseRefMv(0, 0);
  ruiCost = xPatternRefinement( pcPatternKey, baseRefMv, 2, rcMvHalf, !bIsLosslessCoded );

  m_pcRdCost->setCostScale( 0 );

  xExtDIFUpSamplingQ ( &cPatternRoi, rcMvHalf );
  baseRefMv = rcMvHalf;
  baseRefMv <<= 1;

  rcMvQter = *pcMvInt;   rcMvQter <<= 1;    // for mv-cost
  rcMvQter += rcMvHalf;  rcMvQter <<= 1;
  ruiCost = xPatternRefinement( pcPatternKey, baseRefMv, 1, rcMvQter, !bIsLosslessCoded );
}


//! encode residual and calculate rate-distortion for a CU block
Void TEncSearch::encodeResAndCalcRdInterCU( TComDataCU* pcCU, TComYuv* pcYuvOrg, TComYuv* pcYuvPred,
                                            TComYuv* pcYuvResi, TComYuv* pcYuvResiBest, TComYuv* pcYuvRec,
                                            Bool bSkipResidual DEBUG_STRING_FN_DECLARE(sDebug) )
{
  assert ( !pcCU->isIntra(0) );

  const UInt cuWidthPixels      = pcCU->getWidth ( 0 );
  const UInt cuHeightPixels     = pcCU->getHeight( 0 );
  const Int  numValidComponents = pcCU->getPic()->getNumberValidComponents();
  const TComSPS &sps=*(pcCU->getSlice()->getSPS());

  // The pcCU is not marked as skip-mode at this point, and its m_pcTrCoeff, m_pcArlCoeff, m_puhCbf, m_puhTrIdx will all be 0.
  // due to prior calls to TComDataCU::initEstData(  );

  if ( bSkipResidual ) //  No residual coding : SKIP mode
  {
    pcCU->setSkipFlagSubParts( true, 0, pcCU->getDepth(0) );

    pcYuvResi->clear();

    pcYuvPred->copyToPartYuv( pcYuvRec, 0 );
    Distortion distortion = 0;

    for (Int comp=0; comp < numValidComponents; comp++)
    {
      const ComponentID compID=ComponentID(comp);
      const UInt csx=pcYuvOrg->getComponentScaleX(compID);
      const UInt csy=pcYuvOrg->getComponentScaleY(compID);
      distortion += m_pcRdCost->getDistPart( sps.getBitDepth(toChannelType(compID)), pcYuvRec->getAddr(compID), pcYuvRec->getStride(compID), pcYuvOrg->getAddr(compID),
                                               pcYuvOrg->getStride(compID), cuWidthPixels >> csx, cuHeightPixels >> csy, compID);
    }

    m_pcRDGoOnSbacCoder->load(m_pppcRDSbacCoder[pcCU->getDepth(0)][CI_CURR_BEST]);
    m_pcEntropyCoder->resetBits();

    if (pcCU->getSlice()->getPPS()->getTransquantBypassEnableFlag())
    {
      m_pcEntropyCoder->encodeCUTransquantBypassFlag(pcCU, 0, true);
    }

    m_pcEntropyCoder->encodeSkipFlag(pcCU, 0, true);
    m_pcEntropyCoder->encodeMergeIndex( pcCU, 0, true );

    UInt uiBits = m_pcEntropyCoder->getNumberOfWrittenBits();
    pcCU->getTotalBits()       = uiBits;
    pcCU->getTotalDistortion() = distortion;
    pcCU->getTotalCost()       = m_pcRdCost->calcRdCost( uiBits, distortion );

    m_pcRDGoOnSbacCoder->store(m_pppcRDSbacCoder[pcCU->getDepth(0)][CI_TEMP_BEST]);

#if DEBUG_STRING
    pcYuvResiBest->clear(); // Clear the residual image, if we didn't code it.
    for(UInt i=0; i<MAX_NUM_COMPONENT+1; i++)
    {
      sDebug+=debug_reorder_data_inter_token[i];
    }
#endif

    return;
  }

  //  Residual coding.

   pcYuvResi->subtract( pcYuvOrg, pcYuvPred, 0, cuWidthPixels );

  TComTURecurse tuLevel0(pcCU, 0);

  Double     nonZeroCost       = 0;
  UInt       nonZeroBits       = 0;
  Distortion nonZeroDistortion = 0;
  Distortion zeroDistortion    = 0;

  m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ pcCU->getDepth( 0 ) ][ CI_CURR_BEST ] );

  xEstimateInterResidualQT( pcYuvResi,  nonZeroCost, nonZeroBits, nonZeroDistortion, &zeroDistortion, tuLevel0 DEBUG_STRING_PASS_INTO(sDebug) );

  // -------------------------------------------------------
  // set the coefficients in the pcCU, and also calculates the residual data.
  // If a block full of 0's is efficient, then just use 0's.
  // The costs at this point do not include header bits.

  m_pcEntropyCoder->resetBits();
  m_pcEntropyCoder->encodeQtRootCbfZero( );
  const UInt   zeroResiBits = m_pcEntropyCoder->getNumberOfWrittenBits();
  const Double zeroCost     = (pcCU->isLosslessCoded( 0 )) ? (nonZeroCost+1) : (m_pcRdCost->calcRdCost( zeroResiBits, zeroDistortion ));

  if ( zeroCost < nonZeroCost || !pcCU->getQtRootCbf(0) )
  {
    const UInt uiQPartNum = tuLevel0.GetAbsPartIdxNumParts();
    ::memset( pcCU->getTransformIdx()     , 0, uiQPartNum * sizeof(UChar) );
    for (Int comp=0; comp < numValidComponents; comp++)
    {
      const ComponentID component = ComponentID(comp);
      ::memset( pcCU->getCbf( component ) , 0, uiQPartNum * sizeof(UChar) );
      ::memset( pcCU->getCrossComponentPredictionAlpha(component), 0, ( uiQPartNum * sizeof(SChar) ) );
    }
    static const UInt useTS[MAX_NUM_COMPONENT]={0,0,0};
    pcCU->setTransformSkipSubParts ( useTS, 0, pcCU->getDepth(0) );
#if DEBUG_STRING
    sDebug.clear();
    for(UInt i=0; i<MAX_NUM_COMPONENT+1; i++)
    {
      sDebug+=debug_reorder_data_inter_token[i];
    }
#endif
  }
  else
  {
    xSetInterResidualQTData( NULL, false, tuLevel0); // Call first time to set coefficients.
  }

  // all decisions now made. Fully encode the CU, including the headers:
  m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[pcCU->getDepth(0)][CI_CURR_BEST] );

  UInt finalBits = 0;
  xAddSymbolBitsInter( pcCU, finalBits );
  // we've now encoded the pcCU, and so have a valid bit cost

  if ( !pcCU->getQtRootCbf( 0 ) )
  {
    pcYuvResiBest->clear(); // Clear the residual image, if we didn't code it.
  }
  else
  {
    xSetInterResidualQTData( pcYuvResiBest, true, tuLevel0 ); // else set the residual image data pcYUVResiBest from the various temp images.
  }
  m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ pcCU->getDepth( 0 ) ][ CI_TEMP_BEST ] );

  pcYuvRec->addClip ( pcYuvPred, pcYuvResiBest, 0, cuWidthPixels, sps.getBitDepths() );

  // update with clipped distortion and cost (previously unclipped reconstruction values were used)

  Distortion finalDistortion = 0;
  for(Int comp=0; comp<numValidComponents; comp++)
  {
    const ComponentID compID=ComponentID(comp);
    finalDistortion += m_pcRdCost->getDistPart( sps.getBitDepth(toChannelType(compID)), pcYuvRec->getAddr(compID ), pcYuvRec->getStride(compID ), pcYuvOrg->getAddr(compID ), pcYuvOrg->getStride(compID), cuWidthPixels >> pcYuvOrg->getComponentScaleX(compID), cuHeightPixels >> pcYuvOrg->getComponentScaleY(compID), compID);
  }

  pcCU->getTotalBits()       = finalBits;
  pcCU->getTotalDistortion() = finalDistortion;
  pcCU->getTotalCost()       = m_pcRdCost->calcRdCost( finalBits, finalDistortion );
}



Void TEncSearch::xEstimateInterResidualQT( TComYuv    *pcResi,
                                           Double     &rdCost,
                                           UInt       &ruiBits,
                                           Distortion &ruiDist,
                                           Distortion *puiZeroDist,
                                           TComTU     &rTu
                                           DEBUG_STRING_FN_DECLARE(sDebug) )
{
  TComDataCU *pcCU        = rTu.getCU();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  const UInt uiDepth      = rTu.GetTransformDepthTotal();
  const UInt uiTrMode     = rTu.GetTransformDepthRel();
  const UInt subTUDepth   = uiTrMode + 1;
  const UInt numValidComp = pcCU->getPic()->getNumberValidComponents();
  DEBUG_STRING_NEW(sSingleStringComp[MAX_NUM_COMPONENT])

  assert( pcCU->getDepth( 0 ) == pcCU->getDepth( uiAbsPartIdx ) );
  const UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();

  UInt SplitFlag = ((pcCU->getSlice()->getSPS()->getQuadtreeTUMaxDepthInter() == 1) && pcCU->isInter(uiAbsPartIdx) && ( pcCU->getPartitionSize(uiAbsPartIdx) != SIZE_2Nx2N ));
#if DEBUG_STRING
  const Int debugPredModeMask = DebugStringGetPredModeMask(pcCU->getPredictionMode(uiAbsPartIdx));
#endif

  Bool bCheckFull;

  if ( SplitFlag && uiDepth == pcCU->getDepth(uiAbsPartIdx) && ( uiLog2TrSize >  pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) ) )
  {
    bCheckFull = false;
  }
  else
  {
    bCheckFull =  ( uiLog2TrSize <= pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() );
  }

  const Bool bCheckSplit  = ( uiLog2TrSize >  pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) );

  assert( bCheckFull || bCheckSplit );

  // code full block
  Double     dSingleCost = MAX_DOUBLE;
  UInt       uiSingleBits                                                                                                        = 0;
  Distortion uiSingleDistComp            [MAX_NUM_COMPONENT][2/*0 = top (or whole TU for non-4:2:2) sub-TU, 1 = bottom sub-TU*/] = {{0,0},{0,0},{0,0}};
  Distortion uiSingleDist                                                                                                        = 0;
  TCoeff     uiAbsSum                    [MAX_NUM_COMPONENT][2/*0 = top (or whole TU for non-4:2:2) sub-TU, 1 = bottom sub-TU*/] = {{0,0},{0,0},{0,0}};
  UInt       uiBestTransformMode         [MAX_NUM_COMPONENT][2/*0 = top (or whole TU for non-4:2:2) sub-TU, 1 = bottom sub-TU*/] = {{0,0},{0,0},{0,0}};
  //  Stores the best explicit RDPCM mode for a TU encoded without split
  UInt       bestExplicitRdpcmModeUnSplit[MAX_NUM_COMPONENT][2/*0 = top (or whole TU for non-4:2:2) sub-TU, 1 = bottom sub-TU*/] = {{3,3}, {3,3}, {3,3}};
  SChar      bestCrossCPredictionAlpha   [MAX_NUM_COMPONENT][2/*0 = top (or whole TU for non-4:2:2) sub-TU, 1 = bottom sub-TU*/] = {{0,0},{0,0},{0,0}};

  m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_ROOT ] );

  if( bCheckFull )
  {
    Double minCost[MAX_NUM_COMPONENT][2/*0 = top (or whole TU for non-4:2:2) sub-TU, 1 = bottom sub-TU*/];
    Bool checkTransformSkip[MAX_NUM_COMPONENT];
    pcCU->setTrIdxSubParts( uiTrMode, uiAbsPartIdx, uiDepth );

    m_pcEntropyCoder->resetBits();

    memset( m_pTempPel, 0, sizeof( Pel ) * rTu.getRect(COMPONENT_Y).width * rTu.getRect(COMPONENT_Y).height ); // not necessary needed for inside of recursion (only at the beginning)

    const UInt uiQTTempAccessLayer = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;
    TCoeff *pcCoeffCurr[MAX_NUM_COMPONENT];
#if ADAPTIVE_QP_SELECTION
    TCoeff *pcArlCoeffCurr[MAX_NUM_COMPONENT];
#endif

    for(UInt i=0; i<numValidComp; i++)
    {
      minCost[i][0] = MAX_DOUBLE;
      minCost[i][1] = MAX_DOUBLE;
    }

    Pel crossCPredictedResidualBuffer[ MAX_TU_SIZE * MAX_TU_SIZE ];

    for(UInt i=0; i<numValidComp; i++)
    {
      checkTransformSkip[i]=false;
      const ComponentID compID=ComponentID(i);
      const Int channelBitDepth=pcCU->getSlice()->getSPS()->getBitDepth(toChannelType(compID));
      pcCoeffCurr[compID]    = m_ppcQTTempCoeff[compID][uiQTTempAccessLayer] + rTu.getCoefficientOffset(compID);
#if ADAPTIVE_QP_SELECTION
      pcArlCoeffCurr[compID] = m_ppcQTTempArlCoeff[compID ][uiQTTempAccessLayer] +  rTu.getCoefficientOffset(compID);
#endif

      if(rTu.ProcessComponentSection(compID))
      {
        const QpParam cQP(*pcCU, compID);

        checkTransformSkip[compID] = pcCU->getSlice()->getPPS()->getUseTransformSkip() &&
                                     TUCompRectHasAssociatedTransformSkipFlag(rTu.getRect(compID), pcCU->getSlice()->getPPS()->getPpsRangeExtension().getLog2MaxTransformSkipBlockSize()) &&
                                     (!pcCU->isLosslessCoded(0));

        const Bool splitIntoSubTUs = rTu.getRect(compID).width != rTu.getRect(compID).height;

        TComTURecurse TUIterator(rTu, false, (splitIntoSubTUs ? TComTU::VERTICAL_SPLIT : TComTU::DONT_SPLIT), true, compID);

        const UInt partIdxesPerSubTU = TUIterator.GetAbsPartIdxNumParts(compID);

        do
        {
          const UInt           subTUIndex             = TUIterator.GetSectionNumber();
          const UInt           subTUAbsPartIdx        = TUIterator.GetAbsPartIdxTU(compID);
          const TComRectangle &tuCompRect             = TUIterator.getRect(compID);
          const UInt           subTUBufferOffset      = tuCompRect.width * tuCompRect.height * subTUIndex;

                TCoeff        *currentCoefficients    = pcCoeffCurr[compID] + subTUBufferOffset;
#if ADAPTIVE_QP_SELECTION
                TCoeff        *currentARLCoefficients = pcArlCoeffCurr[compID] + subTUBufferOffset;
#endif
          const Bool isCrossCPredictionAvailable      =    isChroma(compID)
                                                         && pcCU->getSlice()->getPPS()->getPpsRangeExtension().getCrossComponentPredictionEnabledFlag()
                                                         && (pcCU->getCbf(subTUAbsPartIdx, COMPONENT_Y, uiTrMode) != 0);

          SChar preCalcAlpha = 0;
          const Pel *pLumaResi = m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix( COMPONENT_Y, rTu.getRect( COMPONENT_Y ).x0, rTu.getRect( COMPONENT_Y ).y0 );

          if (isCrossCPredictionAvailable)
          {
            const Bool bUseReconstructedResidualForEstimate = m_pcEncCfg->getUseReconBasedCrossCPredictionEstimate();
            const Pel  *const lumaResidualForEstimate       = bUseReconstructedResidualForEstimate ? pLumaResi                                                     : pcResi->getAddrPix(COMPONENT_Y, tuCompRect.x0, tuCompRect.y0);
            const UInt        lumaResidualStrideForEstimate = bUseReconstructedResidualForEstimate ? m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(COMPONENT_Y) : pcResi->getStride(COMPONENT_Y);

            preCalcAlpha = xCalcCrossComponentPredictionAlpha(TUIterator,
                                                              compID,
                                                              lumaResidualForEstimate,
                                                              pcResi->getAddrPix(compID, tuCompRect.x0, tuCompRect.y0),
                                                              tuCompRect.width,
                                                              tuCompRect.height,
                                                              lumaResidualStrideForEstimate,
                                                              pcResi->getStride(compID));
          }

          const Int transformSkipModesToTest    = checkTransformSkip[compID] ? 2 : 1;
          const Int crossCPredictionModesToTest = (preCalcAlpha != 0)        ? 2 : 1; // preCalcAlpha cannot be anything other than 0 if isCrossCPredictionAvailable is false

          const Bool isOneMode                  = (crossCPredictionModesToTest == 1) && (transformSkipModesToTest == 1);

          for (Int transformSkipModeId = 0; transformSkipModeId < transformSkipModesToTest; transformSkipModeId++)
          {
            pcCU->setTransformSkipPartRange(transformSkipModeId, compID, subTUAbsPartIdx, partIdxesPerSubTU);

            for (Int crossCPredictionModeId = 0; crossCPredictionModeId < crossCPredictionModesToTest; crossCPredictionModeId++)
            {
              const Bool isFirstMode          = (transformSkipModeId == 0) && (crossCPredictionModeId == 0);
              const Bool bUseCrossCPrediction = crossCPredictionModeId != 0;

              m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_ROOT ] );
              m_pcEntropyCoder->resetBits();

              pcCU->setTransformSkipPartRange(transformSkipModeId, compID, subTUAbsPartIdx, partIdxesPerSubTU);
              pcCU->setCrossComponentPredictionAlphaPartRange((bUseCrossCPrediction ? preCalcAlpha : 0), compID, subTUAbsPartIdx, partIdxesPerSubTU );

              if ((compID != COMPONENT_Cr) && ((transformSkipModeId == 1) ? m_pcEncCfg->getUseRDOQTS() : m_pcEncCfg->getUseRDOQ()))
              {
                m_pcEntropyCoder->estimateBit(m_pcTrQuant->m_pcEstBitsSbac, tuCompRect.width, tuCompRect.height, toChannelType(compID));
              }

#if RDOQ_CHROMA_LAMBDA
              m_pcTrQuant->selectLambda(compID);
#endif

              Pel *pcResiCurrComp = m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix(compID, tuCompRect.x0, tuCompRect.y0);
              UInt resiStride     = m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID);

              TCoeff bestCoeffComp   [MAX_TU_SIZE*MAX_TU_SIZE];
              Pel    bestResiComp    [MAX_TU_SIZE*MAX_TU_SIZE];

#if ADAPTIVE_QP_SELECTION
              TCoeff bestArlCoeffComp[MAX_TU_SIZE*MAX_TU_SIZE];
#endif
              TCoeff     currAbsSum   = 0;
              UInt       currCompBits = 0;
              Distortion currCompDist = 0;
              Double     currCompCost = 0;
              UInt       nonCoeffBits = 0;
              Distortion nonCoeffDist = 0;
              Double     nonCoeffCost = 0;

              if(!isOneMode && !isFirstMode)
              {
                memcpy(bestCoeffComp,    currentCoefficients,    (sizeof(TCoeff) * tuCompRect.width * tuCompRect.height));
#if ADAPTIVE_QP_SELECTION
                memcpy(bestArlCoeffComp, currentARLCoefficients, (sizeof(TCoeff) * tuCompRect.width * tuCompRect.height));
#endif
                for(Int y = 0; y < tuCompRect.height; y++)
                {
                  memcpy(&bestResiComp[y * tuCompRect.width], (pcResiCurrComp + (y * resiStride)), (sizeof(Pel) * tuCompRect.width));
                }
              }

              if (bUseCrossCPrediction)
              {
                TComTrQuant::crossComponentPrediction(TUIterator,
                                                      compID,
                                                      pLumaResi,
                                                      pcResi->getAddrPix(compID, tuCompRect.x0, tuCompRect.y0),
                                                      crossCPredictedResidualBuffer,
                                                      tuCompRect.width,
                                                      tuCompRect.height,
                                                      m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(COMPONENT_Y),
                                                      pcResi->getStride(compID),
                                                      tuCompRect.width,
                                                      false);

                m_pcTrQuant->transformNxN(TUIterator, compID, crossCPredictedResidualBuffer, tuCompRect.width, currentCoefficients,
#if ADAPTIVE_QP_SELECTION
                                          currentARLCoefficients,
#endif
                                          currAbsSum, cQP);
              }
              else
              {
                m_pcTrQuant->transformNxN(TUIterator, compID, pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ), pcResi->getStride(compID), currentCoefficients,
#if ADAPTIVE_QP_SELECTION
                                          currentARLCoefficients,
#endif
                                          currAbsSum, cQP);
              }

              if(isFirstMode || (currAbsSum == 0))
              {
                if (bUseCrossCPrediction)
                {
                  TComTrQuant::crossComponentPrediction(TUIterator,
                                                        compID,
                                                        pLumaResi,
                                                        m_pTempPel,
                                                        m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix(compID, tuCompRect.x0, tuCompRect.y0),
                                                        tuCompRect.width,
                                                        tuCompRect.height,
                                                        m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(COMPONENT_Y),
                                                        tuCompRect.width,
                                                        m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID),
                                                        true);

                  nonCoeffDist = m_pcRdCost->getDistPart( channelBitDepth, m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                          m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride( compID ), pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                          pcResi->getStride(compID), tuCompRect.width, tuCompRect.height, compID); // initialized with zero residual distortion
                }
                else
                {
                  nonCoeffDist = m_pcRdCost->getDistPart( channelBitDepth, m_pTempPel, tuCompRect.width, pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                          pcResi->getStride(compID), tuCompRect.width, tuCompRect.height, compID); // initialized with zero residual distortion
                }

                m_pcEntropyCoder->encodeQtCbfZero( TUIterator, toChannelType(compID) );

                if ( isCrossCPredictionAvailable )
                {
                  m_pcEntropyCoder->encodeCrossComponentPrediction( TUIterator, compID );
                }

                nonCoeffBits = m_pcEntropyCoder->getNumberOfWrittenBits();
                nonCoeffCost = m_pcRdCost->calcRdCost( nonCoeffBits, nonCoeffDist );
              }

              if((puiZeroDist != NULL) && isFirstMode)
              {
                *puiZeroDist += nonCoeffDist; // initialized with zero residual distortion
              }

              DEBUG_STRING_NEW(sSingleStringTest)

              if( currAbsSum > 0 ) //if non-zero coefficients are present, a residual needs to be derived for further prediction
              {
                if (isFirstMode)
                {
                  m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_ROOT ] );
                  m_pcEntropyCoder->resetBits();
                }

                m_pcEntropyCoder->encodeQtCbf( TUIterator, compID, true );

                if (isCrossCPredictionAvailable)
                {
                  m_pcEntropyCoder->encodeCrossComponentPrediction( TUIterator, compID );
                }

                m_pcEntropyCoder->encodeCoeffNxN( TUIterator, currentCoefficients, compID );
                currCompBits = m_pcEntropyCoder->getNumberOfWrittenBits();

                pcResiCurrComp = m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 );

                m_pcTrQuant->invTransformNxN( TUIterator, compID, pcResiCurrComp, m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID), currentCoefficients, cQP DEBUG_STRING_PASS_INTO_OPTIONAL(&sSingleStringTest, (DebugOptionList::DebugString_InvTran.getInt()&debugPredModeMask)) );

                if (bUseCrossCPrediction)
                {
                  TComTrQuant::crossComponentPrediction(TUIterator,
                                                        compID,
                                                        pLumaResi,
                                                        m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix(compID, tuCompRect.x0, tuCompRect.y0),
                                                        m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix(compID, tuCompRect.x0, tuCompRect.y0),
                                                        tuCompRect.width,
                                                        tuCompRect.height,
                                                        m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(COMPONENT_Y),
                                                        m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID     ),
                                                        m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID     ),
                                                        true);
                }

                currCompDist = m_pcRdCost->getDistPart( channelBitDepth, m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                        m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID),
                                                        pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                        pcResi->getStride(compID),
                                                        tuCompRect.width, tuCompRect.height, compID);

                currCompCost = m_pcRdCost->calcRdCost(currCompBits, currCompDist);
                  
                if (pcCU->isLosslessCoded(0))
                {
                  nonCoeffCost = MAX_DOUBLE;
                }
              }
              else if ((transformSkipModeId == 1) && !bUseCrossCPrediction)
              {
                currCompCost = MAX_DOUBLE;
              }
              else
              {
                currCompBits = nonCoeffBits;
                currCompDist = nonCoeffDist;
                currCompCost = nonCoeffCost;
              }

              // evaluate
              if ((currCompCost < minCost[compID][subTUIndex]) || ((transformSkipModeId == 1) && (currCompCost == minCost[compID][subTUIndex])))
              {
                bestExplicitRdpcmModeUnSplit[compID][subTUIndex] = pcCU->getExplicitRdpcmMode(compID, subTUAbsPartIdx);

                if(isFirstMode) //check for forced null
                {
                  if((nonCoeffCost < currCompCost) || (currAbsSum == 0))
                  {
                    memset(currentCoefficients, 0, (sizeof(TCoeff) * tuCompRect.width * tuCompRect.height));

                    currAbsSum   = 0;
                    currCompBits = nonCoeffBits;
                    currCompDist = nonCoeffDist;
                    currCompCost = nonCoeffCost;
                  }
                }

#if DEBUG_STRING
                if (currAbsSum > 0)
                {
                  DEBUG_STRING_SWAP(sSingleStringComp[compID], sSingleStringTest)
                }
                else
                {
                  sSingleStringComp[compID].clear();
                }
#endif

                uiAbsSum                 [compID][subTUIndex] = currAbsSum;
                uiSingleDistComp         [compID][subTUIndex] = currCompDist;
                minCost                  [compID][subTUIndex] = currCompCost;
                uiBestTransformMode      [compID][subTUIndex] = transformSkipModeId;
                bestCrossCPredictionAlpha[compID][subTUIndex] = (crossCPredictionModeId == 1) ? pcCU->getCrossComponentPredictionAlpha(subTUAbsPartIdx, compID) : 0;

                if (uiAbsSum[compID][subTUIndex] == 0)
                {
                  if (bUseCrossCPrediction)
                  {
                    TComTrQuant::crossComponentPrediction(TUIterator,
                                                          compID,
                                                          pLumaResi,
                                                          m_pTempPel,
                                                          m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix(compID, tuCompRect.x0, tuCompRect.y0),
                                                          tuCompRect.width,
                                                          tuCompRect.height,
                                                          m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(COMPONENT_Y),
                                                          tuCompRect.width,
                                                          m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID),
                                                          true);
                  }
                  else
                  {
                    pcResiCurrComp = m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix(compID, tuCompRect.x0, tuCompRect.y0);
                    const UInt uiStride = m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID);
                    for(UInt uiY = 0; uiY < tuCompRect.height; uiY++)
                    {
                      memset(pcResiCurrComp, 0, (sizeof(Pel) * tuCompRect.width));
                      pcResiCurrComp += uiStride;
                    }
                  }
                }
              }
              else
              {
                // reset
                memcpy(currentCoefficients,    bestCoeffComp,    (sizeof(TCoeff) * tuCompRect.width * tuCompRect.height));
#if ADAPTIVE_QP_SELECTION
                memcpy(currentARLCoefficients, bestArlCoeffComp, (sizeof(TCoeff) * tuCompRect.width * tuCompRect.height));
#endif
                for (Int y = 0; y < tuCompRect.height; y++)
                {
                  memcpy((pcResiCurrComp + (y * resiStride)), &bestResiComp[y * tuCompRect.width], (sizeof(Pel) * tuCompRect.width));
                }
              }
            }
          }

          pcCU->setExplicitRdpcmModePartRange            (   bestExplicitRdpcmModeUnSplit[compID][subTUIndex],                            compID, subTUAbsPartIdx, partIdxesPerSubTU);
          pcCU->setTransformSkipPartRange                (   uiBestTransformMode         [compID][subTUIndex],                            compID, subTUAbsPartIdx, partIdxesPerSubTU );
          pcCU->setCbfPartRange                          ((((uiAbsSum                    [compID][subTUIndex] > 0) ? 1 : 0) << uiTrMode), compID, subTUAbsPartIdx, partIdxesPerSubTU );
          pcCU->setCrossComponentPredictionAlphaPartRange(   bestCrossCPredictionAlpha   [compID][subTUIndex],                            compID, subTUAbsPartIdx, partIdxesPerSubTU );
        } while (TUIterator.nextSection(rTu)); //end of sub-TU loop
      } // processing section
    } // component loop

    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      const ComponentID compID = ComponentID(ch);
      if (rTu.ProcessComponentSection(compID) && (rTu.getRect(compID).width != rTu.getRect(compID).height))
      {
        offsetSubTUCBFs(rTu, compID); //the CBFs up to now have been defined for two sub-TUs - shift them down a level and replace with the parent level CBF
      }
    }

    m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_ROOT ] );
    m_pcEntropyCoder->resetBits();

    if( uiLog2TrSize > pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) )
    {
      m_pcEntropyCoder->encodeTransformSubdivFlag( 0, 5 - uiLog2TrSize );
    }

    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      const UInt chOrderChange = ((ch + 1) == numValidComp) ? 0 : (ch + 1);
      const ComponentID compID=ComponentID(chOrderChange);
      if( rTu.ProcessComponentSection(compID) )
      {
        m_pcEntropyCoder->encodeQtCbf( rTu, compID, true );
      }
    }

    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      const ComponentID compID=ComponentID(ch);
      if (rTu.ProcessComponentSection(compID))
      {
        if(isChroma(compID) && (uiAbsSum[COMPONENT_Y][0] != 0))
        {
          m_pcEntropyCoder->encodeCrossComponentPrediction( rTu, compID );
        }

        m_pcEntropyCoder->encodeCoeffNxN( rTu, pcCoeffCurr[compID], compID );
        for (UInt subTUIndex = 0; subTUIndex < 2; subTUIndex++)
        {
          uiSingleDist += uiSingleDistComp[compID][subTUIndex];
        }
      }
    }

    uiSingleBits = m_pcEntropyCoder->getNumberOfWrittenBits();

    dSingleCost = m_pcRdCost->calcRdCost( uiSingleBits, uiSingleDist );
  } // check full

  // code sub-blocks
  if( bCheckSplit )
  {
    if( bCheckFull )
    {
      m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_TEST ] );
      m_pcRDGoOnSbacCoder->load ( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_ROOT ] );
    }
    Distortion uiSubdivDist = 0;
    UInt       uiSubdivBits = 0;
    Double     dSubdivCost = 0.0;

    //save the non-split CBFs in case we need to restore them later

    UInt bestCBF     [MAX_NUM_COMPONENT];
    UInt bestsubTUCBF[MAX_NUM_COMPONENT][2];
    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      const ComponentID compID=ComponentID(ch);

      if (rTu.ProcessComponentSection(compID))
      {
        bestCBF[compID] = pcCU->getCbf(uiAbsPartIdx, compID, uiTrMode);

        const TComRectangle &tuCompRect = rTu.getRect(compID);
        if (tuCompRect.width != tuCompRect.height)
        {
          const UInt partIdxesPerSubTU = rTu.GetAbsPartIdxNumParts(compID) >> 1;

          for (UInt subTU = 0; subTU < 2; subTU++)
          {
            bestsubTUCBF[compID][subTU] = pcCU->getCbf ((uiAbsPartIdx + (subTU * partIdxesPerSubTU)), compID, subTUDepth);
          }
        }
      }
    }


    TComTURecurse tuRecurseChild(rTu, false);
    const UInt uiQPartNumSubdiv = tuRecurseChild.GetAbsPartIdxNumParts();

    DEBUG_STRING_NEW(sSplitString[MAX_NUM_COMPONENT])

    do
    {
      DEBUG_STRING_NEW(childString)
      xEstimateInterResidualQT( pcResi, dSubdivCost, uiSubdivBits, uiSubdivDist, bCheckFull ? NULL : puiZeroDist,  tuRecurseChild DEBUG_STRING_PASS_INTO(childString));
#if DEBUG_STRING
      // split the string by component and append to the relevant output (because decoder decodes in channel order, whereas this search searches by TU-order)
      std::size_t lastPos=0;
      const std::size_t endStrng=childString.find(debug_reorder_data_inter_token[MAX_NUM_COMPONENT], lastPos);
      for(UInt ch = 0; ch < numValidComp; ch++)
      {
        if (lastPos!=std::string::npos && childString.find(debug_reorder_data_inter_token[ch], lastPos)==lastPos)
        {
          lastPos+=strlen(debug_reorder_data_inter_token[ch]); // skip leading string
        }
        std::size_t pos=childString.find(debug_reorder_data_inter_token[ch+1], lastPos);
        if (pos!=std::string::npos && pos>endStrng)
        {
          lastPos=endStrng;
        }
        sSplitString[ch]+=childString.substr(lastPos, (pos==std::string::npos)? std::string::npos : (pos-lastPos) );
        lastPos=pos;
      }
#endif
    } while ( tuRecurseChild.nextSection(rTu) ) ;

    UInt uiCbfAny=0;
    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      UInt uiYUVCbf = 0;
      for( UInt ui = 0; ui < 4; ++ui )
      {
        uiYUVCbf |= pcCU->getCbf( uiAbsPartIdx + ui * uiQPartNumSubdiv, ComponentID(ch),  uiTrMode + 1 );
      }
      UChar *pBase=pcCU->getCbf( ComponentID(ch) );
      const UInt flags=uiYUVCbf << uiTrMode;
      for( UInt ui = 0; ui < 4 * uiQPartNumSubdiv; ++ui )
      {
        pBase[uiAbsPartIdx + ui] |= flags;
      }
      uiCbfAny|=uiYUVCbf;
    }

    m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_ROOT ] );
    m_pcEntropyCoder->resetBits();

    // when compID isn't a channel, code Cbfs:
    xEncodeInterResidualQT( MAX_NUM_COMPONENT, rTu );
    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      xEncodeInterResidualQT( ComponentID(ch), rTu );
    }

    uiSubdivBits = m_pcEntropyCoder->getNumberOfWrittenBits();
    dSubdivCost  = m_pcRdCost->calcRdCost( uiSubdivBits, uiSubdivDist );

    if (!bCheckFull || (uiCbfAny && (dSubdivCost < dSingleCost)))
    {
      rdCost += dSubdivCost;
      ruiBits += uiSubdivBits;
      ruiDist += uiSubdivDist;
#if DEBUG_STRING
      for(UInt ch = 0; ch < numValidComp; ch++)
      {
        DEBUG_STRING_APPEND(sDebug, debug_reorder_data_inter_token[ch])
        DEBUG_STRING_APPEND(sDebug, sSplitString[ch])
      }
#endif
    }
    else
    {
      rdCost  += dSingleCost;
      ruiBits += uiSingleBits;
      ruiDist += uiSingleDist;

      //restore state to unsplit

      pcCU->setTrIdxSubParts( uiTrMode, uiAbsPartIdx, uiDepth );

      for(UInt ch = 0; ch < numValidComp; ch++)
      {
        const ComponentID compID=ComponentID(ch);

        DEBUG_STRING_APPEND(sDebug, debug_reorder_data_inter_token[ch])
        if (rTu.ProcessComponentSection(compID))
        {
          DEBUG_STRING_APPEND(sDebug, sSingleStringComp[compID])

          const Bool splitIntoSubTUs   = rTu.getRect(compID).width != rTu.getRect(compID).height;
          const UInt numberOfSections  = splitIntoSubTUs ? 2 : 1;
          const UInt partIdxesPerSubTU = rTu.GetAbsPartIdxNumParts(compID) >> (splitIntoSubTUs ? 1 : 0);

          for (UInt subTUIndex = 0; subTUIndex < numberOfSections; subTUIndex++)
          {
            const UInt  uisubTUPartIdx = uiAbsPartIdx + (subTUIndex * partIdxesPerSubTU);

            if (splitIntoSubTUs)
            {
              const UChar combinedCBF = (bestsubTUCBF[compID][subTUIndex] << subTUDepth) | (bestCBF[compID] << uiTrMode);
              pcCU->setCbfPartRange(combinedCBF, compID, uisubTUPartIdx, partIdxesPerSubTU);
            }
            else
            {
              pcCU->setCbfPartRange((bestCBF[compID] << uiTrMode), compID, uisubTUPartIdx, partIdxesPerSubTU);
            }

            pcCU->setCrossComponentPredictionAlphaPartRange(bestCrossCPredictionAlpha[compID][subTUIndex], compID, uisubTUPartIdx, partIdxesPerSubTU);
            pcCU->setTransformSkipPartRange(uiBestTransformMode[compID][subTUIndex], compID, uisubTUPartIdx, partIdxesPerSubTU);
            pcCU->setExplicitRdpcmModePartRange(bestExplicitRdpcmModeUnSplit[compID][subTUIndex], compID, uisubTUPartIdx, partIdxesPerSubTU);
          }
        }
      }

      m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_TEST ] );
    }
  }
  else
  {
    rdCost  += dSingleCost;
    ruiBits += uiSingleBits;
    ruiDist += uiSingleDist;
#if DEBUG_STRING
    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      const ComponentID compID=ComponentID(ch);
      DEBUG_STRING_APPEND(sDebug, debug_reorder_data_inter_token[compID])

      if (rTu.ProcessComponentSection(compID))
      {
        DEBUG_STRING_APPEND(sDebug, sSingleStringComp[compID])
      }
    }
#endif
  }
  DEBUG_STRING_APPEND(sDebug, debug_reorder_data_inter_token[MAX_NUM_COMPONENT])
}



Void TEncSearch::xEncodeInterResidualQT( const ComponentID compID, TComTU &rTu )
{
  TComDataCU* pcCU=rTu.getCU();
  const UInt uiAbsPartIdx=rTu.GetAbsPartIdxTU();
  const UInt uiCurrTrMode = rTu.GetTransformDepthRel();
  assert( pcCU->getDepth( 0 ) == pcCU->getDepth( uiAbsPartIdx ) );
  const UInt uiTrMode = pcCU->getTransformIdx( uiAbsPartIdx );

  const Bool bSubdiv = uiCurrTrMode != uiTrMode;

  const UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();

  if (compID==MAX_NUM_COMPONENT)  // we are not processing a channel, instead we always recurse and code the CBFs
  {
    if( uiLog2TrSize <= pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() && uiLog2TrSize > pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) )
    {
      if((pcCU->getSlice()->getSPS()->getQuadtreeTUMaxDepthInter() == 1) && (pcCU->getPartitionSize(uiAbsPartIdx) != SIZE_2Nx2N))
      {
        assert(bSubdiv); // Inferred splitting rule - see derivation and use of interSplitFlag in the specification.
      }
      else
      {
        m_pcEntropyCoder->encodeTransformSubdivFlag( bSubdiv, 5 - uiLog2TrSize );
      }
    }

    assert( !pcCU->isIntra(uiAbsPartIdx) );

    const Bool bFirstCbfOfCU = uiCurrTrMode == 0;

    for (UInt ch=COMPONENT_Cb; ch<pcCU->getPic()->getNumberValidComponents(); ch++)
    {
      const ComponentID compIdInner=ComponentID(ch);
      if( bFirstCbfOfCU || rTu.ProcessingAllQuadrants(compIdInner) )
      {
        if( bFirstCbfOfCU || pcCU->getCbf( uiAbsPartIdx, compIdInner, uiCurrTrMode - 1 ) )
        {
          m_pcEntropyCoder->encodeQtCbf( rTu, compIdInner, !bSubdiv );
        }
      }
      else
      {
        assert( pcCU->getCbf( uiAbsPartIdx, compIdInner, uiCurrTrMode ) == pcCU->getCbf( uiAbsPartIdx, compIdInner, uiCurrTrMode - 1 ) );
      }
    }

    if (!bSubdiv)
    {
      m_pcEntropyCoder->encodeQtCbf( rTu, COMPONENT_Y, true );
    }
  }

  if( !bSubdiv )
  {
    if (compID != MAX_NUM_COMPONENT) // we have already coded the CBFs, so now we code coefficients
    {
      if (rTu.ProcessComponentSection(compID))
      {
        if (isChroma(compID) && (pcCU->getCbf(uiAbsPartIdx, COMPONENT_Y, uiTrMode) != 0))
        {
          m_pcEntropyCoder->encodeCrossComponentPrediction(rTu, compID);
        }

        if (pcCU->getCbf(uiAbsPartIdx, compID, uiTrMode) != 0)
        {
          const UInt uiQTTempAccessLayer = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;
          TCoeff *pcCoeffCurr = m_ppcQTTempCoeff[compID][uiQTTempAccessLayer] + rTu.getCoefficientOffset(compID);
          m_pcEntropyCoder->encodeCoeffNxN( rTu, pcCoeffCurr, compID );
        }
      }
    }
  }
  else
  {
    if( compID==MAX_NUM_COMPONENT || pcCU->getCbf( uiAbsPartIdx, compID, uiCurrTrMode ) )
    {
      TComTURecurse tuRecurseChild(rTu, false);
      do
      {
        xEncodeInterResidualQT( compID, tuRecurseChild );
      } while (tuRecurseChild.nextSection(rTu));
    }
  }
}




Void TEncSearch::xSetInterResidualQTData( TComYuv* pcResi, Bool bSpatial, TComTU &rTu ) // TODO: turn this into two functions for bSpatial=true and false.
{
  TComDataCU* pcCU=rTu.getCU();
  const UInt uiCurrTrMode=rTu.GetTransformDepthRel();
  const UInt uiAbsPartIdx=rTu.GetAbsPartIdxTU();
  assert( pcCU->getDepth( 0 ) == pcCU->getDepth( uiAbsPartIdx ) );
  const UInt uiTrMode = pcCU->getTransformIdx( uiAbsPartIdx );
  const TComSPS *sps=pcCU->getSlice()->getSPS();

  if( uiCurrTrMode == uiTrMode )
  {
    const UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();
    const UInt uiQTTempAccessLayer = sps->getQuadtreeTULog2MaxSize() - uiLog2TrSize;

    if( bSpatial )
    {
      // Data to be copied is in the spatial domain, i.e., inverse-transformed.

      for(UInt i=0; i<pcResi->getNumberValidComponents(); i++)
      {
        const ComponentID compID=ComponentID(i);
        if (rTu.ProcessComponentSection(compID))
        {
          const TComRectangle &rectCompTU(rTu.getRect(compID));
          m_pcQTTempTComYuv[uiQTTempAccessLayer].copyPartToPartComponentMxN    ( compID, pcResi, rectCompTU );
        }
      }
    }
    else
    {
      for (UInt ch=0; ch < getNumberValidComponents(sps->getChromaFormatIdc()); ch++)
      {
        const ComponentID compID   = ComponentID(ch);
        if (rTu.ProcessComponentSection(compID))
        {
          const TComRectangle &rectCompTU(rTu.getRect(compID));
          const UInt numCoeffInBlock    = rectCompTU.width * rectCompTU.height;
          const UInt offset             = rTu.getCoefficientOffset(compID);
          TCoeff* dest                  = pcCU->getCoeff(compID)                        + offset;
          const TCoeff* src             = m_ppcQTTempCoeff[compID][uiQTTempAccessLayer] + offset;
          ::memcpy( dest, src, sizeof(TCoeff)*numCoeffInBlock );

#if ADAPTIVE_QP_SELECTION
          TCoeff* pcArlCoeffSrc            = m_ppcQTTempArlCoeff[compID][uiQTTempAccessLayer] + offset;
          TCoeff* pcArlCoeffDst            = pcCU->getArlCoeff(compID)                        + offset;
          ::memcpy( pcArlCoeffDst, pcArlCoeffSrc, sizeof( TCoeff ) * numCoeffInBlock );
#endif
        }
      }
    }
  }
  else
  {

    TComTURecurse tuRecurseChild(rTu, false);
    do
    {
      xSetInterResidualQTData( pcResi, bSpatial, tuRecurseChild );
    } while (tuRecurseChild.nextSection(rTu));
  }
}




UInt TEncSearch::xModeBitsIntra( TComDataCU* pcCU, UInt uiMode, UInt uiPartOffset, UInt uiDepth, const ChannelType chType )
{
  // Reload only contexts required for coding intra mode information
  m_pcRDGoOnSbacCoder->loadIntraDirMode( m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST], chType );

  // Temporarily set the intra dir being tested, and only
  // for absPartIdx, since encodeIntraDirModeLuma/Chroma only use
  // the entry at absPartIdx.

  UChar &rIntraDirVal=pcCU->getIntraDir( chType )[uiPartOffset];
  UChar origVal=rIntraDirVal;
  rIntraDirVal = uiMode;
  //pcCU->setIntraDirSubParts ( chType, uiMode, uiPartOffset, uiDepth + uiInitTrDepth );

  m_pcEntropyCoder->resetBits();
  if (isLuma(chType))
  {
    m_pcEntropyCoder->encodeIntraDirModeLuma ( pcCU, uiPartOffset);
  }
  else
  {
    m_pcEntropyCoder->encodeIntraDirModeChroma ( pcCU, uiPartOffset);
  }

  rIntraDirVal = origVal; // restore

  return m_pcEntropyCoder->getNumberOfWrittenBits();
}




UInt TEncSearch::xUpdateCandList( UInt uiMode, Double uiCost, UInt uiFastCandNum, UInt * CandModeList, Double * CandCostList )
{
  UInt i;
  UInt shift=0;

  while ( shift<uiFastCandNum && uiCost<CandCostList[ uiFastCandNum-1-shift ] )
  {
    shift++;
  }

  if( shift!=0 )
  {
    for(i=1; i<shift; i++)
    {
      CandModeList[ uiFastCandNum-i ] = CandModeList[ uiFastCandNum-1-i ];
      CandCostList[ uiFastCandNum-i ] = CandCostList[ uiFastCandNum-1-i ];
    }
    CandModeList[ uiFastCandNum-shift ] = uiMode;
    CandCostList[ uiFastCandNum-shift ] = uiCost;
    return 1;
  }

  return 0;
}





/** add inter-prediction syntax elements for a CU block
 * \param pcCU
 * \param uiQp
 * \param uiTrMode
 * \param ruiBits
 * \returns Void
 */
Void  TEncSearch::xAddSymbolBitsInter( TComDataCU* pcCU, UInt& ruiBits )
{
  if(pcCU->getMergeFlag( 0 ) && pcCU->getPartitionSize( 0 ) == SIZE_2Nx2N && !pcCU->getQtRootCbf( 0 ))
  {
    pcCU->setSkipFlagSubParts( true, 0, pcCU->getDepth(0) );

    m_pcEntropyCoder->resetBits();
    if(pcCU->getSlice()->getPPS()->getTransquantBypassEnableFlag())
    {
      m_pcEntropyCoder->encodeCUTransquantBypassFlag(pcCU, 0, true);
    }
    m_pcEntropyCoder->encodeSkipFlag(pcCU, 0, true);
    m_pcEntropyCoder->encodeMergeIndex(pcCU, 0, true);

    ruiBits += m_pcEntropyCoder->getNumberOfWrittenBits();
  }
  else
  {
    m_pcEntropyCoder->resetBits();

    if(pcCU->getSlice()->getPPS()->getTransquantBypassEnableFlag())
    {
      m_pcEntropyCoder->encodeCUTransquantBypassFlag(pcCU, 0, true);
    }

    m_pcEntropyCoder->encodeSkipFlag ( pcCU, 0, true );
    m_pcEntropyCoder->encodePredMode( pcCU, 0, true );
    m_pcEntropyCoder->encodePartSize( pcCU, 0, pcCU->getDepth(0), true );
    m_pcEntropyCoder->encodePredInfo( pcCU, 0 );

    Bool codeDeltaQp = false;
    Bool codeChromaQpAdj = false;
    m_pcEntropyCoder->encodeCoeff   ( pcCU, 0, pcCU->getDepth(0), codeDeltaQp, codeChromaQpAdj );

    ruiBits += m_pcEntropyCoder->getNumberOfWrittenBits();
  }
}





/**
 * \brief Generate half-sample interpolated block
 *
 * \param pattern Reference picture ROI
 * \param biPred    Flag indicating whether block is for biprediction
 */
Void TEncSearch::xExtDIFUpSamplingH( TComPattern* pattern )
{
  Int width      = pattern->getROIYWidth();
  Int height     = pattern->getROIYHeight();
  Int srcStride  = pattern->getPatternLStride();

  Int intStride = m_filteredBlockTmp[0].getStride(COMPONENT_Y);
  Int dstStride = m_filteredBlock[0][0].getStride(COMPONENT_Y);
  Pel *intPtr;
  Pel *dstPtr;
  Int filterSize = NTAPS_LUMA;
  Int halfFilterSize = (filterSize>>1);
  Pel *srcPtr = pattern->getROIY() - halfFilterSize*srcStride - 1;

  const ChromaFormat chFmt = m_filteredBlock[0][0].getChromaFormat();

  m_if.filterHor(COMPONENT_Y, srcPtr, srcStride, m_filteredBlockTmp[0].getAddr(COMPONENT_Y), intStride, width+1, height+filterSize, 0, false, chFmt, pattern->getBitDepthY());
  m_if.filterHor(COMPONENT_Y, srcPtr, srcStride, m_filteredBlockTmp[2].getAddr(COMPONENT_Y), intStride, width+1, height+filterSize, 2, false, chFmt, pattern->getBitDepthY());

  intPtr = m_filteredBlockTmp[0].getAddr(COMPONENT_Y) + halfFilterSize * intStride + 1;
  dstPtr = m_filteredBlock[0][0].getAddr(COMPONENT_Y);
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width+0, height+0, 0, false, true, chFmt, pattern->getBitDepthY());

  intPtr = m_filteredBlockTmp[0].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride + 1;
  dstPtr = m_filteredBlock[2][0].getAddr(COMPONENT_Y);
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width+0, height+1, 2, false, true, chFmt, pattern->getBitDepthY());

  intPtr = m_filteredBlockTmp[2].getAddr(COMPONENT_Y) + halfFilterSize * intStride;
  dstPtr = m_filteredBlock[0][2].getAddr(COMPONENT_Y);
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width+1, height+0, 0, false, true, chFmt, pattern->getBitDepthY());

  intPtr = m_filteredBlockTmp[2].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
  dstPtr = m_filteredBlock[2][2].getAddr(COMPONENT_Y);
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width+1, height+1, 2, false, true, chFmt, pattern->getBitDepthY());
}





/**
 * \brief Generate quarter-sample interpolated blocks
 *
 * \param pattern    Reference picture ROI
 * \param halfPelRef Half-pel mv
 * \param biPred     Flag indicating whether block is for biprediction
 */
Void TEncSearch::xExtDIFUpSamplingQ( TComPattern* pattern, TComMv halfPelRef )
{
  Int width      = pattern->getROIYWidth();
  Int height     = pattern->getROIYHeight();
  Int srcStride  = pattern->getPatternLStride();

  Pel *srcPtr;
  Int intStride = m_filteredBlockTmp[0].getStride(COMPONENT_Y);
  Int dstStride = m_filteredBlock[0][0].getStride(COMPONENT_Y);
  Pel *intPtr;
  Pel *dstPtr;
  Int filterSize = NTAPS_LUMA;

  Int halfFilterSize = (filterSize>>1);

  Int extHeight = (halfPelRef.getVer() == 0) ? height + filterSize : height + filterSize-1;

  const ChromaFormat chFmt = m_filteredBlock[0][0].getChromaFormat();

  // Horizontal filter 1/4
  srcPtr = pattern->getROIY() - halfFilterSize * srcStride - 1;
  intPtr = m_filteredBlockTmp[1].getAddr(COMPONENT_Y);
  if (halfPelRef.getVer() > 0)
  {
    srcPtr += srcStride;
  }
  if (halfPelRef.getHor() >= 0)
  {
    srcPtr += 1;
  }
  m_if.filterHor(COMPONENT_Y, srcPtr, srcStride, intPtr, intStride, width, extHeight, 1, false, chFmt, pattern->getBitDepthY());

  // Horizontal filter 3/4
  srcPtr = pattern->getROIY() - halfFilterSize*srcStride - 1;
  intPtr = m_filteredBlockTmp[3].getAddr(COMPONENT_Y);
  if (halfPelRef.getVer() > 0)
  {
    srcPtr += srcStride;
  }
  if (halfPelRef.getHor() > 0)
  {
    srcPtr += 1;
  }
  m_if.filterHor(COMPONENT_Y, srcPtr, srcStride, intPtr, intStride, width, extHeight, 3, false, chFmt, pattern->getBitDepthY());

  // Generate @ 1,1
  intPtr = m_filteredBlockTmp[1].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
  dstPtr = m_filteredBlock[1][1].getAddr(COMPONENT_Y);
  if (halfPelRef.getVer() == 0)
  {
    intPtr += intStride;
  }
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 1, false, true, chFmt, pattern->getBitDepthY());

  // Generate @ 3,1
  intPtr = m_filteredBlockTmp[1].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
  dstPtr = m_filteredBlock[3][1].getAddr(COMPONENT_Y);
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 3, false, true, chFmt, pattern->getBitDepthY());

  if (halfPelRef.getVer() != 0)
  {
    // Generate @ 2,1
    intPtr = m_filteredBlockTmp[1].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
    dstPtr = m_filteredBlock[2][1].getAddr(COMPONENT_Y);
    if (halfPelRef.getVer() == 0)
    {
      intPtr += intStride;
    }
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 2, false, true, chFmt, pattern->getBitDepthY());

    // Generate @ 2,3
    intPtr = m_filteredBlockTmp[3].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
    dstPtr = m_filteredBlock[2][3].getAddr(COMPONENT_Y);
    if (halfPelRef.getVer() == 0)
    {
      intPtr += intStride;
    }
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 2, false, true, chFmt, pattern->getBitDepthY());
  }
  else
  {
    // Generate @ 0,1
    intPtr = m_filteredBlockTmp[1].getAddr(COMPONENT_Y) + halfFilterSize * intStride;
    dstPtr = m_filteredBlock[0][1].getAddr(COMPONENT_Y);
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 0, false, true, chFmt, pattern->getBitDepthY());

    // Generate @ 0,3
    intPtr = m_filteredBlockTmp[3].getAddr(COMPONENT_Y) + halfFilterSize * intStride;
    dstPtr = m_filteredBlock[0][3].getAddr(COMPONENT_Y);
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 0, false, true, chFmt, pattern->getBitDepthY());
  }

  if (halfPelRef.getHor() != 0)
  {
    // Generate @ 1,2
    intPtr = m_filteredBlockTmp[2].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
    dstPtr = m_filteredBlock[1][2].getAddr(COMPONENT_Y);
    if (halfPelRef.getHor() > 0)
    {
      intPtr += 1;
    }
    if (halfPelRef.getVer() >= 0)
    {
      intPtr += intStride;
    }
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 1, false, true, chFmt, pattern->getBitDepthY());

    // Generate @ 3,2
    intPtr = m_filteredBlockTmp[2].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
    dstPtr = m_filteredBlock[3][2].getAddr(COMPONENT_Y);
    if (halfPelRef.getHor() > 0)
    {
      intPtr += 1;
    }
    if (halfPelRef.getVer() > 0)
    {
      intPtr += intStride;
    }
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 3, false, true, chFmt, pattern->getBitDepthY());
  }
  else
  {
    // Generate @ 1,0
    intPtr = m_filteredBlockTmp[0].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride + 1;
    dstPtr = m_filteredBlock[1][0].getAddr(COMPONENT_Y);
    if (halfPelRef.getVer() >= 0)
    {
      intPtr += intStride;
    }
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 1, false, true, chFmt, pattern->getBitDepthY());

    // Generate @ 3,0
    intPtr = m_filteredBlockTmp[0].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride + 1;
    dstPtr = m_filteredBlock[3][0].getAddr(COMPONENT_Y);
    if (halfPelRef.getVer() > 0)
    {
      intPtr += intStride;
    }
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 3, false, true, chFmt, pattern->getBitDepthY());
  }

  // Generate @ 1,3
  intPtr = m_filteredBlockTmp[3].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
  dstPtr = m_filteredBlock[1][3].getAddr(COMPONENT_Y);
  if (halfPelRef.getVer() == 0)
  {
    intPtr += intStride;
  }
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 1, false, true, chFmt, pattern->getBitDepthY());

  // Generate @ 3,3
  intPtr = m_filteredBlockTmp[3].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
  dstPtr = m_filteredBlock[3][3].getAddr(COMPONENT_Y);
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 3, false, true, chFmt, pattern->getBitDepthY());
}





//! set wp tables
Void  TEncSearch::setWpScalingDistParam( TComDataCU* pcCU, Int iRefIdx, RefPicList eRefPicListCur )
{
  if ( iRefIdx<0 )
  {
    m_cDistParam.bApplyWeight = false;
    return;
  }

  TComSlice       *pcSlice  = pcCU->getSlice();
  WPScalingParam  *wp0 , *wp1;

  m_cDistParam.bApplyWeight = ( pcSlice->getSliceType()==P_SLICE && pcSlice->testWeightPred() ) || ( pcSlice->getSliceType()==B_SLICE && pcSlice->testWeightBiPred() ) ;

  if ( !m_cDistParam.bApplyWeight )
  {
    return;
  }

  Int iRefIdx0 = ( eRefPicListCur == REF_PIC_LIST_0 ) ? iRefIdx : (-1);
  Int iRefIdx1 = ( eRefPicListCur == REF_PIC_LIST_1 ) ? iRefIdx : (-1);

  getWpScaling( pcCU, iRefIdx0, iRefIdx1, wp0 , wp1 );

  if ( iRefIdx0 < 0 )
  {
    wp0 = NULL;
  }
  if ( iRefIdx1 < 0 )
  {
    wp1 = NULL;
  }

  m_cDistParam.wpCur  = NULL;

  if ( eRefPicListCur == REF_PIC_LIST_0 )
  {
    m_cDistParam.wpCur = wp0;
  }
  else
  {
    m_cDistParam.wpCur = wp1;
  }
}



//! \}
