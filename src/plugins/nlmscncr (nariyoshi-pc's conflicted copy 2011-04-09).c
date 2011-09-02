/* Free software by Pedro Nariyoshi. Do with as you will. No
   warranty.

   This LADSPA plugin provides a simple echo cancellation implemented in
   C.

   This file has poor memory protection. Failures during malloc() will
   not recover nicely. :( */

/*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*****************************************************************************/

#include "ladspa.h"

/*****************************************************************************/

/* Maximo tempo de eco (cuidado com a memória) */
#define MAX_ECO_MS 600 /* Valor em milissegundos */
#define MAX_DTD_MS 20
#define EPSILON 0.001

/*****************************************************************************/

/* The port numbers for the plugin: */

#define LMS_FILTER_LENGTH 0
#define LMS_DTD_LENGTH    1
#define LMS_DTD_THRESHOLD 2
#define LMS_MU            3
#define LMS_SET_THRESHOLD 4
#define LMS_INPUTD        5
#define LMS_INPUTX        6
#define LMS_OUTPUT        7


/* Quantidade de portas */

#define NOPORTS 8

/*****************************************************************************/

#define ABS(x)       				\
(((x) > 0) ? x : -x)
#define LIMIT_BETWEEN_0_AND_MAX_ECO_MS(x)  	\
(((x) < 0) ? 1 : (((0) > MAX_ECO_MS) ? MAX_ECO_MS : (x)))
#define LIMIT_BETWEEN_0_AND_MAX_DTD_MS(x)  	\
(((x) < 0) ? 1 : (((0) > MAX_DTD_MS) ? MAX_DTD_MS : (x)))
#define DB_CO(g) 				\
(powf(10.0f, (g) * 0.05f))
#define CO_DB(v)				\
(20.0f * log10f(v))


/*****************************************************************************/

/* Estrutura do filtro */
typedef struct
{

    LADSPA_Data m_fSampleRate;

    LADSPA_Data * m_pfBufferX; /* Valores anteriores de X */

    LADSPA_Data * m_pfBufferD; /* Valores anteriores de D */

    LADSPA_Data * m_pfCoefs; /* coeficientes do filtro */

    LADSPA_Data * m_pfPdx; /* Correlacao cruzada de D e X */

    LADSPA_Data * m_fDVar;

    LADSPA_Data * m_fXVar;

    /* O tamanho do buffer em potencia de 2 agiliza a "circularização do vetor, transformando uma equacao de resto de divisão em uma operação de E lógico bit a bit */
    unsigned long m_lFilterSize;

    unsigned long m_lDtdSize;

    /* Indice do ponteiro do buffer de X */
    unsigned long m_lWritePointerX;

    /* Ports:
     ------ */

    /* Tamanho do eco maximo em ms */
    LADSPA_Data * m_pfEchoTime;
    LADSPA_Data * m_fEchoTimeant;
    /* Tamanho do eco maximo em ms */
    LADSPA_Data * m_pfDtdTime;

    /* Limiar do DTD */
    LADSPA_Data * m_pfDtdThreshold;

    /* Valor do fator de convergencia */
    LADSPA_Data * m_pfMu;

    /* Valor do fator do erro maximo para o Śet Membership */
    LADSPA_Data * m_pfSetThreshold;

    /* Input audio port data location. */
    LADSPA_Data * m_pfInputD;

    /* Input audio port data location. */
    LADSPA_Data * m_pfInputX;

    /* Output audio port data location. */
    LADSPA_Data * m_pfOutput;

} Filter;

/*****************************************************************************/

LADSPA_Handle instantiateFilter(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate)
{

    unsigned long lMinimumBufferXSize;
    unsigned long lMinimumBufferDSize;

    Filter * pFilter;

    pFilter = (Filter *)malloc(sizeof(Filter));

    if (pFilter == NULL)
    {
        fputs("Out of memory.\n", stderr);
        exit(EXIT_FAILURE);
    }

    pFilter->m_fSampleRate = (LADSPA_Data)SampleRate;

    /* O tamanho do buffer e' a menor potencia de dois que seja maior que o tamanho necessario */
    /* Isto torna a "circularizacao" do vetor muito mais simples */
    lMinimumBufferXSize = (unsigned long)((LADSPA_Data)SampleRate * MAX_ECO_MS);
    lMinimumBufferDSize = (unsigned long)((LADSPA_Data)SampleRate * MAX_DTD_MS);

    pFilter->m_lFilterSize = 1;
    pFilter->m_lDtdSize = 1;

    while (pFilter->m_lFilterSize < lMinimumBufferXSize) /*multiplica por 2 até ser maior que o buffer mínimo */
    {
        pFilter->m_lFilterSize <<= 1;
    }

    while (pFilter->m_lDtdSize < lMinimumBufferDSize) /*multiplica por 2 até ser maior que o buffer mínimo */
    {
        pFilter->m_lDtdSize <<= 1;
    }

    /* cria um buffer "zerado" com o tamanho achado acima de LADSPA_Datas (ou seja floats, ver em ladspa.h) */
    pFilter->m_pfBufferX  = (LADSPA_Data *)calloc(pFilter->m_lFilterSize, sizeof(LADSPA_Data));
    pFilter->m_pfCoefs  = (LADSPA_Data *)calloc(pFilter->m_lFilterSize, sizeof(LADSPA_Data));

    pFilter->m_pfPdx  = (LADSPA_Data *)calloc(pFilter->m_lDtdSize, sizeof(LADSPA_Data));
    pFilter->m_fXVar = (LADSPA_Data *)calloc(1, sizeof(LADSPA_Data));
    pFilter->m_fDVar = (LADSPA_Data *)calloc(1, sizeof(LADSPA_Data));
    pFilter->m_lWritePointerX = 0; /* Inicializa o vetor de escrita */
    pFilter->m_fEchoTimeant = (LADSPA_Data *)calloc(1, sizeof(LADSPA_Data));

    if (pFilter->m_pfBufferX == NULL || pFilter->m_pfCoefs == NULL || pFilter->m_pfBufferD == NULL || pFilter->m_pfPdx == NULL || pFilter->m_fXVar == NULL || pFilter->m_fDVar == NULL || pFilter->m_fEchoTimeant == NULL)
    {
        fputs("Out of memory.\n", stderr);
        exit(EXIT_FAILURE);
    }

    return pFilter;
}

/*****************************************************************************/

/* Initialise and activate a plugin instance. */
void activateFilter(LADSPA_Handle Instance)
{

    Filter * pFilter;
    pFilter = (Filter *)Instance;

    memset(pFilter->m_pfBufferX, 0, sizeof(LADSPA_Data) * pFilter->m_lFilterSize);
    memset(pFilter->m_pfCoefs, 0, sizeof(LADSPA_Data) * pFilter->m_lFilterSize);
    memset(pFilter->m_pfPdx, 0, sizeof(LADSPA_Data) * pFilter->m_lDtdSize);
    *pFilter->m_fEchoTimeant=0;
    *pFilter->m_pfCoefs = 1;
    *pFilter->m_fXVar = 0;
    *pFilter->m_fDVar = 0;


} /* Atribui zero a todos os "size of... " bytes do m_pfBuffer e do m_pfCoefs*/

/*****************************************************************************/

/* Connect a port to a data location. */
void connectPortToFilter(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * DataLocation)
{

    Filter * pFilter;

    pFilter = (Filter *)Instance;

    switch (Port)
    {
    case LMS_FILTER_LENGTH :
        pFilter->m_pfEchoTime = DataLocation;
        break;
    case LMS_DTD_LENGTH :
        pFilter->m_pfDtdTime = DataLocation;
        break;
    case LMS_DTD_THRESHOLD :
        pFilter->m_pfDtdThreshold = DataLocation;
        break;
    case LMS_MU:
        pFilter->m_pfMu = DataLocation;
        break;
    case LMS_SET_THRESHOLD :
        pFilter->m_pfSetThreshold = DataLocation;
        break;
    case LMS_INPUTD:
        pFilter->m_pfInputD = DataLocation;
        break;
    case LMS_INPUTX:
        pFilter->m_pfInputX = DataLocation;
        break;
    case LMS_OUTPUT:
        pFilter->m_pfOutput = DataLocation;
        break;
    }
}

/*****************************************************************************/

/* Roda a instancia do filtro adaptativo */
void runFilter(LADSPA_Handle Instance, unsigned long SampleCount)
{

    LADSPA_Data * pfBufferX; /* Vetor que armazena os valores antigos de x(n) */
    LADSPA_Data * pfCoefs;  /* Vetor que armazena os valores dos coeficientes do filtro */
    LADSPA_Data * pfInputX; /* Aponta para o bloco de amostras da entrada x(n) */
    LADSPA_Data * pfInputD; /* Aponta para o bloco de amostras da entrada d(n) */
    LADSPA_Data * pfOutput; /* Aponta para o bloco de amostras da saida */
    LADSPA_Data * pfXVar; /* A variancia de X */
    LADSPA_Data * pfDVar; /* A variancia de D */
    LADSPA_Data * pfPdx; /* A correlação cruzada de D e X */
    LADSPA_Data fMu;
    LADSPA_Data fStep=0;
    LADSPA_Data fConvSample;
    LADSPA_Data fDtdThreshold;
    LADSPA_Data fErrSample;
    LADSPA_Data fSetThreshold;
    LADSPA_Data fDNCR=0;
    LADSPA_Data fgammaD;

    Filter * pFilter;

    unsigned long lBufferXSizeMinusOne;
    unsigned long lBufferDXSizeMinusOne;
    unsigned long lBufferXWriteOffset;
    unsigned long lXCoefs; /* Comprimento do filtro (em amostras) */
    unsigned long lDCoefs; /* Comprimento do DTD (em amostras) */
    unsigned long lIndexW; /* Indice usado para gravar no buffer */
    unsigned long lSampleIndex;
    unsigned long lConv=0; /* Contador da convolucao */

    pFilter = (Filter *)Instance;

    lBufferXSizeMinusOne = pFilter->m_lFilterSize - 1;
    lXCoefs = (unsigned long)((LIMIT_BETWEEN_0_AND_MAX_ECO_MS(*pFilter->m_pfEchoTime)) * pFilter->m_fSampleRate * 0.001);
    lBufferDXSizeMinusOne = pFilter->m_lDtdSize - 1;
    lDCoefs = (unsigned long)((LIMIT_BETWEEN_0_AND_MAX_DTD_MS(*pFilter->m_pfDtdTime)) * pFilter->m_fSampleRate * 0.001);
    if (lDCoefs == 0) lDCoefs++;
    fgammaD = ((float)lDCoefs - 1.0f)/ (float)lDCoefs;


    pfInputD      =  pFilter->m_pfInputD;
    pfInputX      =  pFilter->m_pfInputX;
    pfOutput      =  pFilter->m_pfOutput;
    pfCoefs       =  pFilter->m_pfCoefs;
    pfBufferX     =  pFilter->m_pfBufferX;
    pfPdx         =  pFilter->m_pfPdx;
    pfXVar        =  pFilter->m_fXVar;
    pfDVar        =  pFilter->m_fDVar;
    fMu           = *pFilter->m_pfMu;
    fDtdThreshold = DB_CO(*pFilter->m_pfDtdThreshold);
    fSetThreshold = DB_CO(*pFilter->m_pfSetThreshold);
    lBufferXWriteOffset = pFilter->m_lWritePointerX;

    lIndexW = lBufferXWriteOffset + 1;
    for (lSampleIndex = 0; lSampleIndex < SampleCount; lSampleIndex++)
    {
        lIndexW--;

        pfBufferX[(lIndexW & lBufferXSizeMinusOne)] = *pfInputX; /* O buffer recebe a mais recente amostra de x(n) */

        fConvSample = 0; /* zera a convolucao */
        for(lConv = 0; lConv < lXCoefs; lConv++)
        {
            fConvSample+=pfCoefs[lConv]*pfBufferX[((lIndexW + lConv) & lBufferXSizeMinusOne)];
        }

        fErrSample = *pfInputD - fConvSample;
        *(pfOutput++) = fErrSample; /* Joga o "erro" na saida */

        if(*pFilter->m_fEchoTimeant == *pFilter->m_pfEchoTime)
        {
            *pfXVar += pfBufferX[(lIndexW) & lBufferXSizeMinusOne] * pfBufferX[(lIndexW) & lBufferXSizeMinusOne] - pfBufferX[(lIndexW + lXCoefs) & lBufferXSizeMinusOne] * pfBufferX[(lIndexW + lXCoefs) & lBufferXSizeMinusOne];
        }
        else
        {
            *pFilter->m_fEchoTimeant = *pFilter->m_pfEchoTime;
            *pfXVar = 0;
            for(lConv = 0; lConv < lXCoefs; lConv++)
            {
                *pfXVar += pfBufferX[(lIndexW + lConv) & lBufferXSizeMinusOne] * pfBufferX[(lIndexW + lConv) & lBufferXSizeMinusOne];
            }
        }


        *pfDVar *= fgammaD;
        *pfDVar += (1 - fgammaD) * (*pfInputD) * (*pfInputD);

        fDNCR=0;
        for(lConv = 0; lConv < lDCoefs; lConv++)
        {
            pfPdx[lConv] *= fgammaD;
            pfPdx[lConv] += (1 - fgammaD) * pfBufferX[(lIndexW + lConv) & lBufferDXSizeMinusOne] * (*pfInputD);
            fDNCR += pfPdx[lConv] * pfCoefs[lConv];
        }

        fDNCR /= *pfDVar; /* fDNCR = fDNCR / *pfDVar */

        if (fDNCR < fDtdThreshold && ABS(fErrSample) > fSetThreshold)
        {
            if (*pfXVar > EPSILON)
            {
                fStep = fMu * fErrSample / *pfXVar;
            }

            else
            {
                fStep = fMu * fErrSample / EPSILON;
            }

            for(lConv = 0; lConv < lXCoefs; lConv++) /* w(n+1) = w(n) + 2 * mu * e(n) * X(n) */
            {
                pfCoefs[lConv]+=fStep*pfBufferX[(lIndexW + lConv) & lBufferXSizeMinusOne];
            }
        }

        pfInputX++;
        pfInputD++;
    }
//    if (fDNCR < fDtdThreshold)
//    {
//        fprintf(stderr,"S");
//    }
//    else
//    {
//        fprintf(stderr,"D");
//    }
//    for(lConv = 0; lConv < lDCoefs; lConv++)
//    {
//        fprintf(stderr,"PDX[%d] = %g    DNCR = %g    pfCoefs = %g \n",(int)lConv,pfPdx[lConv],fDNCR,pfCoefs[lConv]);
//    }

    fprintf(stderr,"fDNCR = %e %e\n",fDNCR,fDtdThreshold);
    pFilter->m_lWritePointerX = ((pFilter->m_lWritePointerX - SampleCount) & lBufferXSizeMinusOne); /* Atualiza o indice do ponteiro dos vetores circulares*/

}

/*****************************************************************************/

/* Throw away a simple delay line. */
void
cleanupFilter(LADSPA_Handle Instance)
{

    Filter * pFilter;

    pFilter = (Filter *)Instance;

    free(pFilter->m_pfBufferX);
    free(pFilter->m_pfCoefs);
    free(pFilter);
}

/*****************************************************************************/

LADSPA_Descriptor * g_psDescriptor = NULL;

/*****************************************************************************/

/* _init() is called automatically when the plugin library is first
   loaded. */
void _init()
{

    char ** pcPortNames;
    LADSPA_PortDescriptor * piPortDescriptors;
    LADSPA_PortRangeHint * psPortRangeHints;

    g_psDescriptor
    = (LADSPA_Descriptor *)malloc(sizeof(LADSPA_Descriptor));
    if (g_psDescriptor)
    {
        g_psDescriptor->UniqueID
        = 5;
        g_psDescriptor->Label
        = strdup("adapt_nlmscncr");
        g_psDescriptor->Properties
        = LADSPA_PROPERTY_HARD_RT_CAPABLE;
        g_psDescriptor->Name
        = strdup("NLMS com CheapNCR");
        g_psDescriptor->Maker
        = strdup("Pedro Nariyoshi");
        g_psDescriptor->Copyright
        = strdup("None");
        g_psDescriptor->PortCount
        = NOPORTS;
        piPortDescriptors
        = (LADSPA_PortDescriptor *)calloc(NOPORTS, sizeof(LADSPA_PortDescriptor));
        g_psDescriptor->PortDescriptors
        = (const LADSPA_PortDescriptor *)piPortDescriptors;
        piPortDescriptors[LMS_FILTER_LENGTH]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[LMS_DTD_LENGTH]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[LMS_DTD_THRESHOLD]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[LMS_MU]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[LMS_SET_THRESHOLD]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[LMS_INPUTD]
        = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        piPortDescriptors[LMS_INPUTX]
        = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        piPortDescriptors[LMS_OUTPUT]
        = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        pcPortNames
        = (char **)calloc(NOPORTS, sizeof(char *));
        g_psDescriptor->PortNames
        = (const char **)pcPortNames;
        pcPortNames[LMS_FILTER_LENGTH]
        = strdup("Tamanho do filtro (ms)");
        pcPortNames[LMS_DTD_LENGTH]
        = strdup("Comprimento do DTD (ms)");
        pcPortNames[LMS_DTD_THRESHOLD]
        = strdup("Limiar do DTD (dB)");
        pcPortNames[LMS_MU]
        = strdup("µ - Fator de convergencia");
        pcPortNames[LMS_SET_THRESHOLD]
        = strdup("Limiar do Set Membership (dB)");
        pcPortNames[LMS_INPUTD]
        = strdup("Input D");
        pcPortNames[LMS_INPUTX]
        = strdup("Input X");
        pcPortNames[LMS_OUTPUT]
        = strdup("Output");
        psPortRangeHints = ((LADSPA_PortRangeHint *)
                            calloc(NOPORTS, sizeof(LADSPA_PortRangeHint)));
        g_psDescriptor->PortRangeHints
        = (const LADSPA_PortRangeHint *)psPortRangeHints;
        psPortRangeHints[LMS_FILTER_LENGTH].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[LMS_FILTER_LENGTH].LowerBound
        = 0;
        psPortRangeHints[LMS_FILTER_LENGTH].UpperBound
        = (LADSPA_Data)MAX_ECO_MS;
        psPortRangeHints[LMS_DTD_LENGTH ].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[LMS_DTD_LENGTH ].LowerBound
        = 0;
        psPortRangeHints[LMS_DTD_LENGTH ].UpperBound
        = (LADSPA_Data)MAX_DTD_MS;
        psPortRangeHints[LMS_DTD_THRESHOLD].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[LMS_DTD_THRESHOLD].LowerBound
        = -60;
        psPortRangeHints[LMS_DTD_THRESHOLD].UpperBound
        = 0;
        psPortRangeHints[LMS_MU].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[LMS_MU].LowerBound
        = 0;
        psPortRangeHints[LMS_MU].UpperBound
        = 1;
        psPortRangeHints[LMS_SET_THRESHOLD].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[LMS_SET_THRESHOLD].LowerBound
        = -120;
        psPortRangeHints[LMS_SET_THRESHOLD].UpperBound
        = 0;
        psPortRangeHints[LMS_INPUTD].HintDescriptor
        = 1;
        psPortRangeHints[LMS_INPUTX].HintDescriptor
        = 0;
        psPortRangeHints[LMS_OUTPUT].HintDescriptor
        = 0;
        g_psDescriptor->instantiate
        = instantiateFilter;
        g_psDescriptor->connect_port
        = connectPortToFilter;
        g_psDescriptor->activate
        = activateFilter;
        g_psDescriptor->run
        = runFilter;
        g_psDescriptor->run_adding
        = NULL;
        g_psDescriptor->set_run_adding_gain
        = NULL;
        g_psDescriptor->deactivate
        = NULL;
        g_psDescriptor->cleanup
        = cleanupFilter;
    }
}

/*****************************************************************************/

/* _fini() is called automatically when the library is unloaded. */
void _fini()
{
    long lIndexW;
    if (g_psDescriptor)
    {
        free((char *)g_psDescriptor->Label);
        free((char *)g_psDescriptor->Name);
        free((char *)g_psDescriptor->Maker);
        free((char *)g_psDescriptor->Copyright);
        free((LADSPA_PortDescriptor *)g_psDescriptor->PortDescriptors);
        for (lIndexW = 0; lIndexW < g_psDescriptor->PortCount; lIndexW++)
            free((char *)(g_psDescriptor->PortNames[lIndexW]));
        free((char **)g_psDescriptor->PortNames);
        free((LADSPA_PortRangeHint *)g_psDescriptor->PortRangeHints);
        free(g_psDescriptor);
    }
}

/*****************************************************************************/

/* Return a descriptor of the requested plugin type. Only one plugin
   type is available in this library. */
const LADSPA_Descriptor *
ladspa_descriptor(unsigned long Index)
{
    if (Index == 0)
        return g_psDescriptor;
    else
        return NULL;
}

/*****************************************************************************/

/* EOF */
