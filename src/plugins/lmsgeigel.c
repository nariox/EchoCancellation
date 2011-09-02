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

/*****************************************************************************/

#include "../ladspa.h"

/*****************************************************************************/

/* Maximo tempo de eco (cuidado com a memória) */
#define MAX_ECO_MS 600 /* Valor em milissegundos */
#define MAX_DTD_MS 20
#define EPSILON 0.0001

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

#define ABS(x)          \
(((x) > 0) ? x : -x)
#define LIMIT_BETWEEN_0_AND_1(x)          \
(((x) < 0) ? 0 : (((x) > 1) ? 1 : (x)))
#define LIMIT_BETWEEN_0_AND_MAX_ECO_MS(x)  \
(((x) < 0) ? 0 : (((x) > MAX_ECO_MS) ? MAX_ECO_MS : (x)))
#define LIMIT_BETWEEN_0_AND_MAX_DTD_MS(x)  \
(((x) < 0) ? 0 : (((x) > MAX_DTD_MS) ? MAX_DTD_MS : (x)))

/*****************************************************************************/

/* Estrutura do filtro */
typedef struct
{

    LADSPA_Data m_fSampleRate;

    LADSPA_Data * m_pfBufferX; /* Valores anteriores de X */

    LADSPA_Data * m_pfCoefs; /* coeficientes do filtro */

    /* O tamanho do buffer em potencia de 2 agiliza a "circularização do vetor, transformando uma equacao de resto de divisão em uma operação de E lógico bit a bit */
    unsigned long m_lFilterSize;

    unsigned long m_lDtdSize;

    /* Indice do ponteiro do buffer de X */
    unsigned long m_lWritePointerX;

    /* Ports:
     ------ */

    /* Tamanho do eco maximo em ms */
    LADSPA_Data * m_pfEchoTime;

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
    lMinimumBufferXSize = (unsigned long)((LADSPA_Data)SampleRate * MAX_ECO_MS *0.001);

    pFilter->m_lFilterSize = 1;
    pFilter->m_lDtdSize = 1;

    while (pFilter->m_lFilterSize < lMinimumBufferXSize) /*multiplica por 2 até ser maior que o buffer mínimo */
    {
        pFilter->m_lFilterSize <<= 1;
    }

    /* cria um buffer "zerado" com o tamanho achado acima de LADSPA_Datas (ou seja floats, ver em ladspa.h) */
    pFilter->m_pfBufferX  = (LADSPA_Data *)calloc(pFilter->m_lFilterSize, sizeof(LADSPA_Data));
    pFilter->m_pfCoefs  = (LADSPA_Data *)calloc(pFilter->m_lFilterSize, sizeof(LADSPA_Data));

    if (pFilter->m_pfBufferX == NULL || pFilter->m_pfCoefs == NULL)
    {
        fputs("Out of memory.\n", stderr);
        exit(EXIT_FAILURE);
    }

    pFilter->m_lWritePointerX = 0; /* Inicializa o vetor de escrita */

    return pFilter;
}

/*****************************************************************************/

/* Initialise and activate a plugin instance. */
void activateFilter(LADSPA_Handle Instance) {

    Filter * pFilter;
    pFilter = (Filter *)Instance;

    memset(pFilter->m_pfBufferX, 0, sizeof(LADSPA_Data) * pFilter->m_lFilterSize);
    memset(pFilter->m_pfCoefs, 0, sizeof(LADSPA_Data) * pFilter->m_lFilterSize);
    pFilter->m_lWritePointerX = 0;

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
    LADSPA_Data * pfInputD; /* Aponta para o bloco de amostras da entrada d(n) */
    LADSPA_Data * pfInputX; /* Aponta para o bloco de amostras da entrada x(n) */
    LADSPA_Data * pfOutput; /* Aponta para o bloco de amostras da saida */
    LADSPA_Data fMu;
    LADSPA_Data fStep;
    LADSPA_Data fConvSample;
    LADSPA_Data fDtdThreshold;
    LADSPA_Data fErrSample;
    LADSPA_Data fMaxX; /* Variavel para DTD pelo metodo de Geigel*/
    LADSPA_Data fSetThreshold;

    Filter * pFilter;

    unsigned long lBufferXSizeMinusOne;
    unsigned long lBufferXWriteOffset;
    unsigned long lXCoefs; /* Comprimento do filtro (em amostras) */
    unsigned long lDCoefs; /* Comprimento do DTD (em amostras) */
    unsigned long lBufferDSizeMinusOne;
    unsigned long lIndex;
    unsigned long lSampleIndex;
    unsigned long lCorrIndex;
    unsigned long lConv; /* Contador da convolucao */

    pFilter = (Filter *)Instance;
    lBufferXSizeMinusOne = pFilter->m_lFilterSize - 1;
    lXCoefs = (unsigned long)(LIMIT_BETWEEN_0_AND_MAX_ECO_MS(*pFilter->m_pfEchoTime))* pFilter->m_fSampleRate * 0.001;
    lBufferDSizeMinusOne = pFilter->m_lDtdSize - 1;
    lDCoefs = (unsigned long)(LIMIT_BETWEEN_0_AND_MAX_DTD_MS(*pFilter->m_pfDtdTime)) * pFilter->m_fSampleRate * 0.001;

    pfInputD      =  pFilter->m_pfInputD;
    pfInputX      =  pFilter->m_pfInputX;
    pfOutput      =  pFilter->m_pfOutput;
    pfCoefs       =  pFilter->m_pfCoefs;
    pfBufferX     =  pFilter->m_pfBufferX;
    fMu           = *pFilter->m_pfMu;
    fDtdThreshold = *pFilter->m_pfDtdThreshold;
    fSetThreshold = *pFilter->m_pfSetThreshold;
    lBufferXWriteOffset = pFilter->m_lWritePointerX;

    for (lSampleIndex = 0; lSampleIndex < SampleCount; lSampleIndex++)
    {
        lIndex = lSampleIndex + lBufferXWriteOffset;

        pfBufferX[(lIndex & lBufferXSizeMinusOne)] = *(pfInputX++); /* O buffer recebe a mais recente amostra de x(n) */
        fConvSample = 0; /* zera a convolucao */
        for(lConv = 0; lConv < lXCoefs; lConv++)
        {
            fConvSample+=pfCoefs[lConv]*pfBufferX[((lIndex - lConv) & lBufferXSizeMinusOne)];
        }

        fMaxX = 0; /* Zera a variavel */
        for (lCorrIndex = 0; lCorrIndex < lDCoefs; lCorrIndex++)
        {
            if (fMaxX < ABS(pfBufferX[(lIndex + lCorrIndex) & lBufferXSizeMinusOne]))
            {
                fMaxX = ABS(pfBufferX[(lIndex + lCorrIndex) & lBufferXSizeMinusOne]);
            }
        }

        fErrSample = *pfInputD - fConvSample;
        *(pfOutput++) = fErrSample; /* Joga o "erro" na saida */

        if (ABS(*pfInputD)/fMaxX < fDtdThreshold && ABS(fErrSample) > fSetThreshold)
        {
                fStep = fMu * fErrSample;

            for(lConv = 0; lConv < lXCoefs; lConv++) /* w(n+1) = w(n) + 2 * mu * e(n) * X(n) */
            {
                pfCoefs[lConv]+=fStep*pfBufferX[((lIndex - lConv) & lBufferXSizeMinusOne)];
            }
        }
        pfInputD++;
    }
    pFilter->m_lWritePointerX = ((pFilter->m_lWritePointerX + SampleCount) & lBufferXSizeMinusOne); /* Atualiza o indice do ponteiro dos vetores circulares*/
}

/*****************************************************************************/

void cleanupFilter(LADSPA_Handle Instance)
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
void _init() {

  char ** pcPortNames;
  LADSPA_PortDescriptor * piPortDescriptors;
  LADSPA_PortRangeHint * psPortRangeHints;

  g_psDescriptor
    = (LADSPA_Descriptor *)malloc(sizeof(LADSPA_Descriptor));
  if (g_psDescriptor) {
    g_psDescriptor->UniqueID
      = 2;
    g_psDescriptor->Label
      = strdup("adapt_lmsgeigel");
    g_psDescriptor->Properties
      = LADSPA_PROPERTY_HARD_RT_CAPABLE;
    g_psDescriptor->Name
      = strdup("LMS com Geigel");
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
      = strdup("Limiar do DTD");
    pcPortNames[LMS_MU]
      = strdup("µ - Fator de convergencia");
    pcPortNames[LMS_SET_THRESHOLD]
      = strdup("Limiar do Set Membership");
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
      = 0;
    psPortRangeHints[LMS_DTD_THRESHOLD].UpperBound
      = 1;
    psPortRangeHints[LMS_MU].HintDescriptor
      = (LADSPA_HINT_BOUNDED_BELOW
	 | LADSPA_HINT_BOUNDED_ABOVE
	 | LADSPA_HINT_DEFAULT_LOW);
    psPortRangeHints[LMS_MU].LowerBound
      = 0;
    psPortRangeHints[LMS_MU].UpperBound
      = 0.2;
    psPortRangeHints[LMS_SET_THRESHOLD].HintDescriptor
      = (LADSPA_HINT_BOUNDED_BELOW
	 | LADSPA_HINT_BOUNDED_ABOVE
	 | LADSPA_HINT_DEFAULT_MIDDLE);
    psPortRangeHints[LMS_SET_THRESHOLD].LowerBound
      = 0;
    psPortRangeHints[LMS_SET_THRESHOLD].UpperBound
      = 1;
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
void _fini() {
  long lIndex;
  if (g_psDescriptor) {
    free((char *)g_psDescriptor->Label);
    free((char *)g_psDescriptor->Name);
    free((char *)g_psDescriptor->Maker);
    free((char *)g_psDescriptor->Copyright);
    free((LADSPA_PortDescriptor *)g_psDescriptor->PortDescriptors);
    for (lIndex = 0; lIndex < g_psDescriptor->PortCount; lIndex++)
      free((char *)(g_psDescriptor->PortNames[lIndex]));
    free((char **)g_psDescriptor->PortNames);
    free((LADSPA_PortRangeHint *)g_psDescriptor->PortRangeHints);
    free(g_psDescriptor);
  }
}

/*****************************************************************************/

/* Return a descriptor of the requested plugin type. Only one plugin
   type is available in this library. */
const LADSPA_Descriptor *
ladspa_descriptor(unsigned long Index) {
  if (Index == 0)
    return g_psDescriptor;
  else
    return NULL;
}

/*****************************************************************************/

/* EOF */
