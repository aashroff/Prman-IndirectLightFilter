#include <RixLightFilter.h>
#include <RixBxdfLobe.h>

// This is based on the Color By Ray Type Pattern node by Malcom Kesson: https://www.fundza.com/devkit/rixpattern/color_by_ray_type/index.html


//To DO : 
// Args File for the light filter parameters.


class PxrDnIndirectLightFilter : public RixLightFilter
{
public:
    PxrDnIndirectLightFilter();
    ~PxrDnIndirectLightFilter();

    int Init(RixContext &, const RtUString pluginpath);
    void Finalize(RixContext &) override;

    RixSCParamInfo const *GetParamTable() override;

#if _PRMANAPI_VERSION_MAJOR_ >= 23
    void
#else
    int
#endif
    CreateInstanceData(RixContext &ctx,
                        const RtUString handle,
                        RixParameterList const *plist,
                        InstanceData *idata) override;

    void Filter(
        RixLightFilterContext const* lfCtx,
        RtPointer instanceData,
        int const numSamples,
        int const* shadingCtxIndex,
        RtVector3 const* toLight,
        float const* dist,
        float const* lightPdfIllum,
        RixBXLobeWeights* contribution) override;

    void Synchronize(
        RixContext& ctx, RixSCSyncMsg syncMsg,
        RixParameterList const* parameterList) override;

    bool GetRadianceModifier(
        FilterRadianceModifierProperty property,
        RixLightFilterContext const* lfCtx,
#if _PRMANAPI_VERSION_MAJOR_ >= 24
        void* instanceData,
#else
        RtConstPointer instanceData,
#endif
        float* result) const override;

    RixSCDetail GetProperty(
#if _PRMANAPI_VERSION_MAJOR_ >= 24
        void* instanceData,
#else
        RtConstPointer instanceData,
#endif
        LightFilterProperty prop,
        void const** result) const override;
};

PxrDnIndirectLightFilter::PxrDnIndirectLightFilter()
{
}

PxrDnIndirectLightFilter::~PxrDnIndirectLightFilter()
{
}

int
PxrDnIndirectLightFilter::Init(RixContext &ctx, const RtUString pluginpath)
{
    PIXAR_ARGUSED(ctx);
    PIXAR_ARGUSED(pluginpath);

    return 0;
}

void
PxrDnIndirectLightFilter::Finalize(RixContext &ctx)
{
    PIXAR_ARGUSED(ctx);
}


enum paramIds
{
    k_useLpe,
    k_gain,
	k_diffuseGain,
	k_specularGain,
    k_linkingGroups,
    k_customLpe,
    k_radianceMultiplier
};


RixSCParamInfo const *
PxrDnIndirectLightFilter::GetParamTable()
{
    static RixSCParamInfo s_ptable[] = 
    {
        // inputs
        RixSCParamInfo(RtUString("useLpe"), k_RixSCInteger),
        RixSCParamInfo(RtUString("gain"), k_RixSCColor),
        RixSCParamInfo(RtUString("diffuseGain"), k_RixSCColor),
        RixSCParamInfo(RtUString("specularGain"), k_RixSCColor),
        RixSCParamInfo(RtUString("linkingGroups"), k_RixSCString),
        RixSCParamInfo(RtUString("customLpe"), k_RixSCString),
        RixSCParamInfo(RtUString("radianceMultiplier"), k_RixSCFloat),
        // end
        RixSCParamInfo()
    };
    return &s_ptable[0];
}

struct myData
{
    bool useLpe;
    int lpeId;
    RixCustomLPE* lpe;
    RtUString m_linkingGroups;
    RtUString m_customLpe;
    RtColorRGB m_contributionGain;
	RtColorRGB m_diffContributionGain;
    RtColorRGB m_specContributionGain;
	RtFloat m_radianceMultiplier;

};


static
void releaseInstanceData(RtPointer data)
{
    myData* md = (myData*) data;
    delete md;
}


#if _PRMANAPI_VERSION_MAJOR_ >= 23
    void
#else
    int
#endif
PxrDnIndirectLightFilter::CreateInstanceData(RixContext &ctx,
                  const RtUString handle, 
                  RixParameterList const *plist,
                  RixShadingPlugin::InstanceData *idata)
{
    RixCustomLPE *lpe = reinterpret_cast< RixCustomLPE * >(
        ctx.GetRixInterface(k_RixCustomLPE));

    myData* data = new myData();

    data->m_radianceMultiplier = 1;
    data->m_contributionGain = RtColorRGB(1);
	data->m_diffContributionGain = RtColorRGB(1);
	data->m_specContributionGain = RtColorRGB(1);
    data->m_linkingGroups = RtUString();

    RtInt useLpe = 0;
	plist->EvalParam(k_useLpe, 0, &useLpe);

    plist->EvalParam(k_gain, 0, &data->m_contributionGain);
	plist->EvalParam(k_diffuseGain, 0, &data->m_diffContributionGain);
    plist->EvalParam(k_specularGain, 0, &data->m_specContributionGain);
    plist->EvalParam(k_radianceMultiplier, 0, &data->m_radianceMultiplier);

    plist->EvalParam(k_linkingGroups, 0, &data->m_linkingGroups);
    plist->EvalParam(k_customLpe, 0, &data->m_customLpe);

    if(useLpe)
    {
        data->useLpe = true;
        data->lpe = lpe;
        data->lpeId = lpe->LookupLPEByName(data->m_customLpe.CStr());
    }
    else
    {
        data->useLpe = false;
        data->lpe = NULL;
        data->lpeId = -1;
    }

    idata->data = data;
    idata->freefunc = releaseInstanceData;

#if _PRMANAPI_VERSION_MAJOR_ < 23
    return 0;
#endif
}


void
PxrDnIndirectLightFilter::Filter(
    RixLightFilterContext const *lfCtx,
#if _PRMANAPI_VERSION_MAJOR_ >= 22
    RtPointer instanceData,
#else
    RtConstPointer instanceData,
#endif
    RtInt const numSamples,
    RtInt const * shadingCtxIndex,
    RtVector3 const * toLight,
    RtFloat const * dist,
    RtFloat const * lightPdfIllum,
    RixBXLobeWeights *contribution)
{
    myData* data = (myData*) instanceData;

    const RtInt * incidentLobeSampled = NULL;
    lfCtx->GetBuiltinVar(
        RixShadingContext::k_incidentLobeSampled, &incidentLobeSampled);

    RixLPEState* const* lpeState;
    RtColorRGB thruput;
    lfCtx->GetBuiltinVar(RixShadingContext::k_lpeState, &lpeState);

    for (int i=0; i<numSamples; ++i) {

        RtColorRGB mult(1.0f);
        const RixBXLobeSampled& incidentRayType = 
            incidentLobeSampled[shadingCtxIndex[i]];

        bool match = true;
        if(data->useLpe && data->lpe)
        {
            mult = data->m_contributionGain;
            match = data->lpe->MatchesLPE(
                data->lpeId, lpeState[shadingCtxIndex[i]], thruput);
        }
        else
        {
            if(incidentRayType.GetDiffuse())
                mult = data->m_diffContributionGain;
            else if(incidentRayType.GetSpecular())
                mult = data->m_specContributionGain;
        }
        if(match)
        {
            for(int j = 0; j < contribution->GetNumDiffuseLobes(); ++j)
                contribution->GetDiffuseLobe(j)[i] *= mult;

            for(int j = 0; j < contribution->GetNumSpecularLobes(); ++j)
                contribution->GetSpecularLobe(j)[i] *= mult;

        }


    }        
}

#if _PRMANAPI_VERSION_MAJOR_ >= 22

void
PxrDnIndirectLightFilter::Synchronize(
    RixContext& ctx,
    RixSCSyncMsg syncMsg,
    RixParameterList const* parameterList)
{
    PIXAR_ARGUSED(ctx);
    PIXAR_ARGUSED(syncMsg);
    PIXAR_ARGUSED(parameterList);
}


bool
PxrDnIndirectLightFilter::GetRadianceModifier(
    FilterRadianceModifierProperty property,
    RixLightFilterContext const* lfCtx,
#if _PRMANAPI_VERSION_MAJOR_ >= 24
    void* instanceData,
#else
    RtConstPointer instanceData,
#endif
    float* result) const
{
    PIXAR_ARGUSED(lfCtx);

    myData* data = (myData*)instanceData;

    if (RixLightFilter::k_Multiplier == property)
    {
        RtFloat radianceMultiplier = data->m_radianceMultiplier;
        if(data->useLpe)
        {
            radianceMultiplier = std::min(
                radianceMultiplier, data->m_contributionGain.ChannelMax());
        }
        else
        {
            radianceMultiplier = std::min(
                radianceMultiplier, std::max(
                    data->m_diffContributionGain.ChannelMax(),
                    data->m_specContributionGain.ChannelMax()));
        }

        *result = radianceMultiplier;
        return true;
    }
    else
    {
        return false;
    }
}


RixSCDetail
PxrDnIndirectLightFilter::GetProperty(
#if _PRMANAPI_VERSION_MAJOR_ >= 24
    void* instanceData,
#else
    RtConstPointer instanceData,
#endif
    LightFilterProperty property,
    void const** result) const
{
    myData* data = (myData*)instanceData;

    if (RixLightFilter::k_LinkingGroups == property)
    {
        (*result) = &data->m_linkingGroups;
        return k_RixSCUniform;
    }
    return k_RixSCInvalidDetail;
}

#endif

extern "C" PRMANEXPORT RixLightFilter* CreateRixLightFilter(const char* hint)
{
    PIXAR_ARGUSED(hint);
    return new PxrDnIndirectLightFilter();
}

extern "C" PRMANEXPORT void DestroyRixLightFilter(RixLightFilter* filter)
{
    delete (PxrDnIndirectLightFilter*)filter;
}