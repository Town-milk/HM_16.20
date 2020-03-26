Void TEncCu::xCompressCU_NonRecursive_64x64To8x8(TComDataCU* pCtu)
{
    // initialize CU data
    m_ppcBestCU[0]->initCtu(pCtu->getPic(), pCtu->getCtuRsAddr());
    m_ppcTempCU[0]->initCtu(pCtu->getPic(), pCtu->getCtuRsAddr());
    ISPL_InitFlag_NonRecursive(); // [LTS] CU Split Flag and Boundary Flag 변수 선언 일단 보류 쓸일 없을것 같음

    // analysis of CU
    DEBUG_STRING_NEW(sDebug)

        // 바운더리 체크 부분 필요 <- xCompressCU_NonRecursive안에 RPelX, BPelY 변수 설정 후 바운더리 체크 하여서 필요가 있을지는 보류

        // [LTS] 64x64 들어가기
        ISPL_xCompressCU_NonRecursive(m_ppcBestCU[0], m_ppcTempCU[0], 0 DEBUG_STRING_PASS_INTO(sDebug));

    // [LTS] 32x32 들어가기
    for (UInt uiDepOnePartIdx = 0; uiDepOnePartIdx < 4; uiDepOnePartIdx++)
    {
        ISPL_InitSubCU_NonRecursive(m_ppcTempCU[0], m_ppcBestCU[1], m_ppcTempCU[1], uiDepOnePartIdx, 0, ip_iQP); // [LTS] 다음 뎁스 정보 넘기기
        //32x32 다음 뎁스에 대한 정보 만들기 그리고 밑에 뎁스 1에 그 정보를 보내줘야한다. <-여기에 QP에대한 정보도 같이 보내줘야함
        ISPL_xCompressCU_NonRecursive(m_ppcBestCU[1], m_ppcTempCU[1], 1 DEBUG_STRING_PASS_INTO(sDebug));
        // 16x16 들어가기
        for (UInt uiDepTwoPartIdx = 0; uiDepTwoPartIdx < 4; uiDepTwoPartIdx++)
        {
            ISPL_InitSubCU_NonRecursive(m_ppcTempCU[1], m_ppcBestCU[2], m_ppcTempCU[2], uiDepTwoPartIdx, 1, ip_iQP);
            ISPL_xCompressCU_NonRecursive(m_ppcBestCU[2], m_ppcTempCU[2], 2 DEBUG_STRING_PASS_INTO(sDebug));
            // 8x8 들어가기
            for (UInt uiDepThrPartIdx = 0; uiDepThrPartIdx < 4; uiDepThrPartIdx++)
            {
                ISPL_InitSubCU_NonRecursive(m_ppcTempCU[2], m_ppcBestCU[3], m_ppcTempCU[3], uiDepThrPartIdx, 2, ip_iQP);
                ISPL_xCompressCU_NonRecursive(m_ppcBestCU[3], m_ppcTempCU[3], 3 DEBUG_STRING_PASS_INTO(sDebug));
                ISPL_StoreUpperCU_NonRecursive(m_ppcTempCU[2], m_ppcBestCU[3], uiDepThrPartIdx, 2)
                    // 8x8에 각 사분면에 대한 Cost 제외나머지 정보 저장
            }
            // 8x8 4사분면 Cost 합치기.
            // 8x8 4사분면합들과 바로 상위뎁스인 16x16 비교
        }
        // 16x16 4사분면 Cost 합치기.
        //16x16 4사분면 합들과 바로 상위뎁스인 32x32 비교
    }
    // 32x32 4사분면 Cost 합치기.
    //32x32 4사분면 합들과 64x64 비교

}

// 보류 쓸일 없을것 같음
Void TEncCu::ISPL_InitFlag_NonRecursive()
{


    // ECU Parameters
    pCU_Split_Flag_64 = &CU_Split_Flag_64;
    pCU_Split_Flag_32 = &CU_Split_Flag_32;
    pCU_Split_Flag_16 = &CU_Split_Flag_16;
    pCU_Split_Flag_8 = &CU_Split_Flag_8;

    // Boundary Parameters
    pBoundary_Flag_64 = &Boundary_Flag_64;
    pBoundary_Flag_32 = &Boundary_Flag_32;
    pBoundary_Flag_16 = &Boundary_Flag_16;
    pBoundary_Flag_8 = &Boundary_Flag_8;

}

Void TEncCu::ISPL_xCompressCU_NonRecursive(TComDataCU*& rpcBestCU, TComDataCU*& rpcTempCU, const UInt uiDepth DEBUG_STRING_FN_DECLARE(sDebug_), PartSize eParentPartSize)
{
    TComPic* pcPic = rpcBestCU->getPic();
    DEBUG_STRING_NEW(sDebug)

        const TComPPS& pps = *(rpcTempCU->getSlice()->getPPS());
    const TComSPS& sps = *(rpcTempCU->getSlice()->getSPS());

    // These are only used if getFastDeltaQp() is true
    const UInt fastDeltaQPCuMaxSize = Clip3(sps.getMaxCUHeight() >> sps.getLog2DiffMaxMinCodingBlockSize(), sps.getMaxCUHeight(), 32u);

    // get Original YUV data from picture
    m_ppcOrigYuv[uiDepth]->copyFromPicYuv(pcPic->getPicYuvOrg(), rpcBestCU->getCtuRsAddr(), rpcBestCU->getZorderIdxInCtu());

    // variable for Cbf fast mode PU decision
    Bool    doNotBlockPu = true;
    Bool    earlyDetectionSkipMode = false;

    const UInt uiLPelX = rpcBestCU->getCUPelX();
    const UInt uiRPelX = uiLPelX + rpcBestCU->getWidth(0) - 1;
    const UInt uiTPelY = rpcBestCU->getCUPelY();
    const UInt uiBPelY = uiTPelY + rpcBestCU->getHeight(0) - 1;
    const UInt uiWidth = rpcBestCU->getWidth(0);

    Int iBaseQP = xComputeQP(rpcBestCU, uiDepth);
    Int iQP;
    Int iMinQP;
    Int iMaxQP;
    Bool isAddLowestQP = false;

    const UInt numberValidComponents = rpcBestCU->getPic()->getNumberValidComponents();


    if (uiDepth <= pps.getMaxCuDQPDepth()) // [LTS] CTU일 때
    {
        Int idQP = m_pcEncCfg->getMaxDeltaQP();
        iMinQP = Clip3(-sps.getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, iBaseQP - idQP);
        iMaxQP = Clip3(-sps.getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, iBaseQP + idQP);
    }
    else //[LTS] CU일 때
    {
        iMinQP = rpcTempCU->getQP(0);
        iMaxQP = rpcTempCU->getQP(0);
    }

    if (m_pcEncCfg->getLumaLevelToDeltaQPMapping().isEnabled())
    {
        if (uiDepth <= pps.getMaxCuDQPDepth())
        {
            // keep using the same m_QP_LUMA_OFFSET in the same CTU
            m_lumaQPOffset = calculateLumaDQP(rpcTempCU, 0, m_ppcOrigYuv[uiDepth]);
        }
        iMinQP = Clip3(-sps.getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, iBaseQP - m_lumaQPOffset);
        iMaxQP = iMinQP; // force encode choose the modified QO
    }

    if (m_pcEncCfg->getUseRateCtrl())
    {
        iMinQP = m_pcRateCtrl->getRCQP();
        iMaxQP = m_pcRateCtrl->getRCQP();
    }

    // transquant-bypass (TQB) processing loop variable initialisation ---

    const Int lowestQP = iMinQP; // For TQB, use this QP which is the lowest non TQB QP tested (rather than QP'=0) - that way delta QPs are smaller, and TQB can be tested at all CU levels.

    if ((pps.getTransquantBypassEnabledFlag()))
    {
        isAddLowestQP = true; // mark that the first iteration is to cost TQB mode.
        iMinQP = iMinQP - 1;  // increase loop variable range by 1, to allow testing of TQB mode along with other QPs
        if (m_pcEncCfg->getCUTransquantBypassFlagForceValue())
        {
            iMaxQP = iMinQP;
        }
    }

    TComSlice* pcSlice = rpcTempCU->getPic()->getSlice(rpcTempCU->getPic()->getCurrSliceIdx());

    const Bool bBoundary = !(uiRPelX < sps.getPicWidthInLumaSamples() && uiBPelY < sps.getPicHeightInLumaSamples());

    // [LTS] 바운더리가 아닐시 본격적인 xCompressCU Intre, Inter, Skip/Merge 시작
    if (!bBoundary)
    {
        for (Int iQP = iMinQP; iQP <= iMaxQP; iQP++)
        {
            const Bool bIsLosslessMode = isAddLowestQP && (iQP == iMinQP);

            if (bIsLosslessMode)
            {
                iQP = lowestQP;
            }
            if (m_pcEncCfg->getLumaLevelToDeltaQPMapping().isEnabled() && uiDepth <= pps.getMaxCuDQPDepth())
            {
                getSliceEncoder()->updateLambda(pcSlice, iQP);
            }

            m_cuChromaQpOffsetIdxPlus1 = 0;
            if (pcSlice->getUseChromaQpAdj())
            {
                /* Pre-estimation of chroma QP based on input block activity may be performed
                 * here, using for example m_ppcOrigYuv[uiDepth] */
                 /* To exercise the current code, the index used for adjustment is based on
                  * block position
                  */
                Int lgMinCuSize = sps.getLog2MinCodingBlockSize() +
                    std::max<Int>(0, sps.getLog2DiffMaxMinCodingBlockSize() - Int(pps.getPpsRangeExtension().getDiffCuChromaQpOffsetDepth()));
                m_cuChromaQpOffsetIdxPlus1 = ((uiLPelX >> lgMinCuSize) + (uiTPelY >> lgMinCuSize)) % (pps.getPpsRangeExtension().getChromaQpOffsetListLen() + 1);
            }

            rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);

            // do inter modes, SKIP and 2Nx2N
            // [TS] xCheck RDcost 시작
            if (rpcBestCU->getSlice()->getSliceType() != I_SLICE)
            {
                // 2Nx2N
                if (m_pcEncCfg->getUseEarlySkipDetection()) // [TS] Fast Algorithm
                {
                    xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2Nx2N DEBUG_STRING_PASS_INTO(sDebug));
                    rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);//by Competition for inter_2Nx2N
                }
                // SKIP 
                xCheckRDCostMerge2Nx2N(rpcBestCU, rpcTempCU DEBUG_STRING_PASS_INTO(sDebug), &earlyDetectionSkipMode);//by Merge for inter_2Nx2N
                rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);

                if (!m_pcEncCfg->getUseEarlySkipDetection()) // [TS] No Fast Algorithm
                {
                    // 2Nx2N, NxN
                    xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2Nx2N DEBUG_STRING_PASS_INTO(sDebug));
                    rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                    if (m_pcEncCfg->getUseCbfFastMode())
                    {
                        doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
                    }
                }
            }

            if (bIsLosslessMode) // Restore loop variable if lossless mode was searched.
            {
                iQP = iMinQP;
            }
        }

        if (!earlyDetectionSkipMode)
        {
            for (Int iQP = iMinQP; iQP <= iMaxQP; iQP++)
            {
                const Bool bIsLosslessMode = isAddLowestQP && (iQP == iMinQP); // If lossless, then iQP is irrelevant for subsequent modules.

                if (bIsLosslessMode)
                {
                    iQP = lowestQP;
                }

                rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);

                // do inter modes, NxN, 2NxN, and Nx2N
                if (rpcBestCU->getSlice()->getSliceType() != I_SLICE)
                {
                    // 2Nx2N, NxN

                    if (!((rpcBestCU->getWidth(0) == 8) && (rpcBestCU->getHeight(0) == 8)))
                    {
                        if (uiDepth == sps.getLog2DiffMaxMinCodingBlockSize() && doNotBlockPu)
                        {
                            xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_NxN DEBUG_STRING_PASS_INTO(sDebug));
                            rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                        }
                    }

                    if (doNotBlockPu)
                    {
                        xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_Nx2N DEBUG_STRING_PASS_INTO(sDebug));
                        rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                        if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_Nx2N)
                        {
                            doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
                        }
                    }
                    if (doNotBlockPu)
                    {
                        xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxN DEBUG_STRING_PASS_INTO(sDebug));
                        rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                        if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_2NxN)
                        {
                            doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
                        }
                    }

                    //! Try AMP (SIZE_2NxnU, SIZE_2NxnD, SIZE_nLx2N, SIZE_nRx2N)
                    if (sps.getUseAMP() && uiDepth < sps.getLog2DiffMaxMinCodingBlockSize())
                    {
#if AMP_ENC_SPEEDUP
                        Bool bTestAMP_Hor = false, bTestAMP_Ver = false;

#if AMP_MRG
                        Bool bTestMergeAMP_Hor = false, bTestMergeAMP_Ver = false;

                        deriveTestModeAMP(rpcBestCU, eParentPartSize, bTestAMP_Hor, bTestAMP_Ver, bTestMergeAMP_Hor, bTestMergeAMP_Ver);
#else
                        deriveTestModeAMP(rpcBestCU, eParentPartSize, bTestAMP_Hor, bTestAMP_Ver);
#endif

                        //! Do horizontal AMP
                        if (bTestAMP_Hor)
                        {
                            if (doNotBlockPu)
                            {
                                xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxnU DEBUG_STRING_PASS_INTO(sDebug));
                                rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                                if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_2NxnU)
                                {
                                    doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
                                }
                            }
                            if (doNotBlockPu)
                            {
                                xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxnD DEBUG_STRING_PASS_INTO(sDebug));
                                rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                                if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_2NxnD)
                                {
                                    doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
                                }
                            }
                        }
#if AMP_MRG
                        else if (bTestMergeAMP_Hor)
                        {
                            if (doNotBlockPu)
                            {
                                xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxnU DEBUG_STRING_PASS_INTO(sDebug), true);
                                rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                                if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_2NxnU)
                                {
                                    doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
                                }
                            }
                            if (doNotBlockPu)
                            {
                                xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxnD DEBUG_STRING_PASS_INTO(sDebug), true);
                                rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                                if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_2NxnD)
                                {
                                    doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
                                }
                            }
                        }
#endif

                        //! Do horizontal AMP
                        if (bTestAMP_Ver)
                        {
                            if (doNotBlockPu)
                            {
                                xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_nLx2N DEBUG_STRING_PASS_INTO(sDebug));
                                rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                                if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_nLx2N)
                                {
                                    doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
                                }
                            }
                            if (doNotBlockPu)
                            {
                                xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_nRx2N DEBUG_STRING_PASS_INTO(sDebug));
                                rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                            }
                        }
#if AMP_MRG
                        else if (bTestMergeAMP_Ver)
                        {
                            if (doNotBlockPu)
                            {
                                xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_nLx2N DEBUG_STRING_PASS_INTO(sDebug), true);
                                rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                                if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_nLx2N)
                                {
                                    doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
                                }
                            }
                            if (doNotBlockPu)
                            {
                                xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_nRx2N DEBUG_STRING_PASS_INTO(sDebug), true);
                                rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                            }
                        }
#endif

#else
                        xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxnU);
                        rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                        xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxnD);
                        rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                        xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_nLx2N);
                        rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                        xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_nRx2N);
                        rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);

#endif
                    }
                }

                // do normal intra modes
                // speedup for inter frames
#if MCTS_ENC_CHECK
                if (m_pcEncCfg->getTMCTSSEITileConstraint() || (rpcBestCU->getSlice()->getSliceType() == I_SLICE) ||
                    ((!m_pcEncCfg->getDisableIntraPUsInInterSlices()) && (
                    (rpcBestCU->getCbf(0, COMPONENT_Y) != 0) ||
                        ((rpcBestCU->getCbf(0, COMPONENT_Cb) != 0) && (numberValidComponents > COMPONENT_Cb)) ||
                        ((rpcBestCU->getCbf(0, COMPONENT_Cr) != 0) && (numberValidComponents > COMPONENT_Cr))  // avoid very complex intra if it is unlikely
                        )))
                {
#else
                if ((rpcBestCU->getSlice()->getSliceType() == I_SLICE) ||
                    ((!m_pcEncCfg->getDisableIntraPUsInInterSlices()) && (
                    (rpcBestCU->getCbf(0, COMPONENT_Y) != 0) ||
                        ((rpcBestCU->getCbf(0, COMPONENT_Cb) != 0) && (numberValidComponents > COMPONENT_Cb)) ||
                        ((rpcBestCU->getCbf(0, COMPONENT_Cr) != 0) && (numberValidComponents > COMPONENT_Cr))  // avoid very complex intra if it is unlikely
                        )))
                {
#endif 
                    xCheckRDCostIntra(rpcBestCU, rpcTempCU, SIZE_2Nx2N DEBUG_STRING_PASS_INTO(sDebug));
                    rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                    if (uiDepth == sps.getLog2DiffMaxMinCodingBlockSize())
                    {
                        if (rpcTempCU->getWidth(0) > (1 << sps.getQuadtreeTULog2MinSize()))
                        {
                            xCheckRDCostIntra(rpcBestCU, rpcTempCU, SIZE_NxN DEBUG_STRING_PASS_INTO(sDebug));
                            rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                        }
                    }
                }

                // test PCM
                if (sps.getUsePCM()
                    && rpcTempCU->getWidth(0) <= (1 << sps.getPCMLog2MaxSize())
                    && rpcTempCU->getWidth(0) >= (1 << sps.getPCMLog2MinSize()))
                {
                    UInt uiRawBits = getTotalBits(rpcBestCU->getWidth(0), rpcBestCU->getHeight(0), rpcBestCU->getPic()->getChromaFormat(), sps.getBitDepths().recon);
                    UInt uiBestBits = rpcBestCU->getTotalBits();
                    if ((uiBestBits > uiRawBits) || (rpcBestCU->getTotalCost() > m_pcRdCost->calcRdCost(uiRawBits, 0)))
                    {
                        xCheckIntraPCM(rpcBestCU, rpcTempCU);
                        rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
                    }
                }

                if (bIsLosslessMode) // Restore loop variable if lossless mode was searched.
                {
                    iQP = iMinQP;
                }
                }
            }

        if (rpcBestCU->getTotalCost() != MAX_DOUBLE)
        {
            m_pcRDGoOnSbacCoder->load(m_pppcRDSbacCoder[uiDepth][CI_NEXT_BEST]);
            m_pcEntropyCoder->resetBits();
            m_pcEntropyCoder->encodeSplitFlag(rpcBestCU, 0, uiDepth, true);
            rpcBestCU->getTotalBits() += m_pcEntropyCoder->getNumberOfWrittenBits(); // split bits
            rpcBestCU->getTotalBins() += ((TEncBinCABAC*)((TEncSbac*)m_pcEntropyCoder->m_pcEntropyCoderIf)->getEncBinIf())->getBinsCoded();
            rpcBestCU->getTotalCost() = m_pcRdCost->calcRdCost(rpcBestCU->getTotalBits(), rpcBestCU->getTotalDistortion());
            m_pcRDGoOnSbacCoder->store(m_pppcRDSbacCoder[uiDepth][CI_NEXT_BEST]);
        }
        }

    // copy original YUV samples to PCM buffer
    if (rpcBestCU->getTotalCost() != MAX_DOUBLE && rpcBestCU->isLosslessCoded(0) && (rpcBestCU->getIPCMFlag(0) == false))
    {
        xFillPCMBuffer(rpcBestCU, m_ppcOrigYuv[uiDepth]);
    }

    if (uiDepth == pps.getMaxCuDQPDepth())
    {
        Int idQP = m_pcEncCfg->getMaxDeltaQP();
        iMinQP = Clip3(-sps.getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, iBaseQP - idQP);
        iMaxQP = Clip3(-sps.getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, iBaseQP + idQP);
    }
    else if (uiDepth < pps.getMaxCuDQPDepth())
    {
        iMinQP = iBaseQP;
        iMaxQP = iBaseQP;
    }
    else
    {
        const Int iStartQP = rpcTempCU->getQP(0);
        iMinQP = iStartQP;
        iMaxQP = iStartQP;
    }

    if (m_pcEncCfg->getLumaLevelToDeltaQPMapping().isEnabled())
    {
        iMinQP = Clip3(-sps.getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, iBaseQP - m_lumaQPOffset);
        iMaxQP = iMinQP;
    }

    if (m_pcEncCfg->getUseRateCtrl())
    {
        iMinQP = m_pcRateCtrl->getRCQP();
        iMaxQP = m_pcRateCtrl->getRCQP();
    }

    if (m_pcEncCfg->getCUTransquantBypassFlagForceValue())
    {
        iMaxQP = iMinQP; // If all TUs are forced into using transquant bypass, do not loop here.
    }

    ip_iQP = &iMinQP; // [LTS] 야매 QP선언

    // [LTS] 하나의 뎁스가 끝낫을 시 나와야 함
    DEBUG_STRING_APPEND(sDebug_, sDebug);

    rpcBestCU->copyToPic(uiDepth);                                                     // Copy Best data to Picture for next partition prediction.
    xCopyYuv2Pic(rpcBestCU->getPic(), rpcBestCU->getCtuRsAddr(), rpcBestCU->getZorderIdxInCtu(), uiDepth, uiDepth);   // Copy Yuv data to picture Yuv
    if (bBoundary)
    {
        return;
    }

    // Assert if Best prediction mode is NONE
    // Selected mode's RD-cost must be not MAX_DOUBLE.
    assert(rpcBestCU->getPartitionSize(0) != NUMBER_OF_PART_SIZES);
    assert(rpcBestCU->getPredictionMode(0) != NUMBER_OF_PREDICTION_MODES);
    assert(rpcBestCU->getTotalCost() != MAX_DOUBLE);
    // [LTS] 끝 하위뎁스로 내려감 없을시 다음 CTU로 이동.
    }

Void TEncCu::ISPL_StoreUpperCU_NonRecursive(TComDataCU * &rpcTempCu, TComDataCU * &pcSubBestPartCU, UInt uiPartUnitIdx, UInt uiDepth)
{
    UChar uhNextDepth = uiDepth + 1;
    rpcTempCU->copyPartFrom(pcSubBestPartCU, uiPartUnitIdx, uhNextDepth);
    xCopyYuv2Tmp(pcSubBestPartCU->getTotalNumPart() * uiPartUnitIdx, uhNextDepth);
}


Bool TEncCu::ISPL_InitSubCU_NonRecursive(TComDataCU * &rpcTempCu, TComDataCU * &pcSubBestPartCU,
    TComDataCU * &pcSubTempPartCU,
    UInt       uiPartUnitIdx,
    UInt       uiDepth,
    Int * &iQP)
{
    Bool bInSlice, bFlag;
    UChar uhNextDepth = uiDepth + 1;
    TComSlice* pcSlice = rpcTempCu->getPic()->getSlice(rpcTempCu->getPic()->getCurrSliceIdx());
    const TComSPS& sps = *(rpcTempCu->getSlice()->getSPS());

    pcSubBestPartCU->initSubCU(rpcTempCu, uiPartUnitIdx, uhNextDepth, *iQP);
    pcSubTempPartCU->initSubCU(rpcTempCu, uiPartUnitIdx, uhNextDepth, *iQP);

    bInSlice = pcSubBestPartCU->getSCUAddr() + pcSubBestPartCU->getTotalNumPart() > pcSlice->getSliceSegmentCurEndCtuTsAddr()
        && pcSubBestPartCU->getSCUAddr() < pcSlice->getSliceSegmentCurEndCtuTsAddr();

    if (bInSlice &&
        (pcSubBestPartCU->getCUPelX() < sps.getPicWidthInLumaSamples()) && (pcSubBestPartCU->getCUPelY() < sps.getPicHeightInLumaSamples()))
    {
        if (0 == uiPartUnitIdx) //initialize RD with previous depth buffer 이전 뎁스 버퍼 사용 RD초기화
        {
            m_pppcRDSbacCoder[uhNextDepth][CI_CURR_BEST]->load(m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST]);
        }
        else
        {
            m_pppcRDSbacCoder[uhNextDepth][CI_CURR_BEST]->load(m_pppcRDSbacCoder[uhNextDepth][CI_NEXT_BEST]);
        }
        bFlag = true;
    }
    else if (bInSlice)
    {
        pcSubBestPartCU->copyToPic(uhNextDepth);
        rpcTempCu->copyPartFrom(pcSubBestPartCU, uiPartUnitIdx, uhNextDepth);
        bFlag = false;
    }

    return bFlag;
}