//
//  libavg - Media Playback Engine. 
//  Copyright (C) 2003-2014 Ulrich von Zadow
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//  Current versions can be found at www.libavg.de
//

#include "GPUChromaKeyFilter.h"
#include "Bitmap.h"
#include "ShaderRegistry.h"
#include "OGLShader.h"
#include "GLContextManager.h"
#include "ImagingProjection.h"
#include "FBO.h"
#include "GLTexture.h"

#include "../base/ObjectCounter.h"
#include "../base/Exception.h"

#include <iostream>

#define SHADERID_CHROMAKEY "chromakey"
#define SHADERID_EROSION "chromakey_erosion"

using namespace std;

namespace avg {

GPUChromaKeyFilter::GPUChromaKeyFilter(const IntPoint& size, bool bStandalone)
    : GPUFilter(SHADERID_CHROMAKEY, true, bStandalone, 2),
      m_Color(0, 255, 0),
      m_HTolerance(0.0),
      m_STolerance(0.0),
      m_LTolerance(0.0),
      m_Softness(0.0),
      m_Erosion(0),
      m_SpillThreshold(0.0)
{
    ObjectCounter::get()->incRef(&typeid(*this));

    GLContext::getCurrent()->ensureFullShaders("GPUChromaKeyFilter");

    setDimensions(size);
    GLContextManager* pCM = GLContextManager::get();
    m_pTextureParam = pCM->createShaderParam<int>(SHADERID_CHROMAKEY, "u_Texture");
    
    m_pHKeyParam = pCM->createShaderParam<float>(SHADERID_CHROMAKEY, "u_HKey");
    m_pHToleranceParam = pCM->createShaderParam<float>(SHADERID_CHROMAKEY,
            "u_HTolerance");
    m_pHSoftToleranceParam = pCM->createShaderParam<float>(SHADERID_CHROMAKEY,
            "u_HSoftTolerance");
    
    m_pSKeyParam = pCM->createShaderParam<float>(SHADERID_CHROMAKEY, "u_SKey");
    m_pSToleranceParam = pCM->createShaderParam<float>(SHADERID_CHROMAKEY,
            "u_STolerance");
    m_pSSoftToleranceParam = pCM->createShaderParam<float>(SHADERID_CHROMAKEY,
            "u_SSoftTolerance");
    
    m_pLKeyParam = pCM->createShaderParam<float>(SHADERID_CHROMAKEY, "u_LKey");
    m_pLToleranceParam = pCM->createShaderParam<float>(SHADERID_CHROMAKEY,
            "u_LTolerance");
    m_pLSoftToleranceParam = pCM->createShaderParam<float>(SHADERID_CHROMAKEY,
            "u_LSoftTolerance");
    
    m_pSpillThresholdParam = pCM->createShaderParam<float>(SHADERID_CHROMAKEY,
            "u_SpillThreshold");
    m_pIsLastParam = pCM->createShaderParam<int>(SHADERID_CHROMAKEY, "u_bIsLast");

    GLContextManager::get()->createShader(SHADERID_EROSION);
    m_pErosionTextureParam = pCM->createShaderParam<int>(SHADERID_EROSION, "u_Texture");
    m_pErosionIsLastParam = pCM->createShaderParam<int>(SHADERID_EROSION, "u_bIsLast");
    
    m_pProjection2 = ImagingProjectionPtr(new ImagingProjection(size));
}

GPUChromaKeyFilter::~GPUChromaKeyFilter()
{
    ObjectCounter::get()->decRef(&typeid(*this));
}

void GPUChromaKeyFilter::setParams(const Pixel32& color, float hTolerance, 
        float sTolerance, float lTolerance, float softness, int erosion,
        float spillThreshold)
{
    m_Color = color;
    m_HTolerance = hTolerance;
    m_STolerance = sTolerance;
    m_LTolerance = lTolerance;
    m_Softness = softness;
    m_Erosion = erosion;
    m_SpillThreshold = spillThreshold;
    if (m_SpillThreshold <= m_HTolerance) {
        m_SpillThreshold = m_HTolerance;
    }
}

void GPUChromaKeyFilter::applyOnGPU(GLContext* pContext, GLTexturePtr pSrcTex)
{
    // Set up double-buffering
    int curBufferIndex = m_Erosion%2;
    getFBO(pContext, curBufferIndex)->activate();
    getShader()->activate();
    m_pTextureParam->set(pContext, 0);

    float h, s, l;
    m_Color.toHSL(h, s, l);
    m_pHKeyParam->set(pContext, h);
    m_pHToleranceParam->set(pContext, m_HTolerance*360);
    m_pHSoftToleranceParam->set(pContext, (m_HTolerance+m_Softness)*360.0f);
    m_pSKeyParam->set(pContext, s);
    m_pSToleranceParam->set(pContext, m_STolerance);
    m_pSSoftToleranceParam->set(pContext, m_STolerance+m_Softness);
    m_pLKeyParam->set(pContext, l);
    m_pLToleranceParam->set(pContext, m_LTolerance);
    m_pLSoftToleranceParam->set(pContext, m_LTolerance+m_Softness);
    m_pSpillThresholdParam->set(pContext, m_SpillThreshold*360);
    m_pIsLastParam->set(pContext, int(m_Erosion==0));
    draw(pContext, pSrcTex, WrapMode());

    for (int i = 0; i < m_Erosion; ++i) {
        curBufferIndex = (curBufferIndex+1)%2;
        getFBO(pContext, curBufferIndex)->activate();
        OGLShaderPtr pShader = avg::getShader(SHADERID_EROSION);
        pShader->activate();
        m_pErosionTextureParam->set(pContext, 0);
        m_pErosionIsLastParam->set(pContext, int(i==m_Erosion-1));
        getDestTex(pContext, (curBufferIndex+1)%2)->activate(WrapMode(), GL_TEXTURE0);
        m_pProjection2->draw(pContext, avg::getShader(SHADERID_EROSION));
    }
}

}
