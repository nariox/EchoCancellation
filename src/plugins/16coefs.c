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

#include "ladspa.h"

/*****************************************************************************/

/* The port numbers for the plugin: */

#define SF_INPUT   0
#define SF_COEF0   1
#define SF_COEF1   2
#define SF_COEF2   3
#define SF_COEF3   4
#define SF_COEF4   5
#define SF_COEF5   6
#define SF_COEF6   7
#define SF_COEF7   8
#define SF_COEF8   9
#define SF_COEF9  10
#define SF_COEF10 11
#define SF_COEF11 12
#define SF_COEF12 13
#define SF_COEF13 14
#define SF_COEF14 15
#define SF_COEF15 16
#define SF_OUTPUT 17

/* Quantidade de portas */

#define NOPORTS 18
#define TAM_FILTRO 16
#define TAM_FILTRO_1 15

/*****************************************************************************/

/* Estrutura do filtro */
typedef struct
{

    LADSPA_Data * m_pfBuffer; /* Valores anteriores de X */

    /* Input audio port data location. */
    LADSPA_Data * m_pfInput;

    /* Input audio peort data location. */
    LADSPA_Data * m_pfCoef0;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef1;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef2;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef3;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef4;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef5;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef6;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef7;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef8;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef9;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef10;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef11;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef12;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef13;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef14;

    /* Input audio port data location. */
    LADSPA_Data * m_pfCoef15;

    /* Output audio port data location. */
    LADSPA_Data * m_pfOutput;

    unsigned long m_lBufferOffset;

} Filter;

/*****************************************************************************/

LADSPA_Handle instantiateFilter(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate)
{

    Filter * pFilter;

    pFilter = (Filter *)malloc(sizeof(Filter));

    if (pFilter == NULL)
    {
        fputs("Out of memory.\n", stderr);
        exit(EXIT_FAILURE);
    }

    /* cria um buffer "zerado" com o tamanho achado acima de LADSPA_Datas (ou seja floats, ver em ladspa.h) */
    pFilter->m_pfBuffer  = (LADSPA_Data *)calloc(TAM_FILTRO, sizeof(LADSPA_Data));

    if (pFilter->m_pfBuffer == NULL)
    {
        fputs("Out of memory.\n", stderr);
        exit(EXIT_FAILURE);
    }

    pFilter->m_lBufferOffset = 0;

    return pFilter;
}

/*****************************************************************************/

/* Initialise and activate a plugin instance. */
void activateFilter(LADSPA_Handle Instance)
{

    Filter * pFilter;
    pFilter = (Filter *)Instance;

    memset(pFilter->m_pfBuffer, 0, sizeof(LADSPA_Data) * TAM_FILTRO);

    pFilter->m_lBufferOffset = 0;

} /* Atribui zero a todos os "size of... " bytes do m_pfBuffer e do m_pfCoef*/

/*****************************************************************************/

/* Connect a port to a data location. */
void connectPortToFilter(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * DataLocation)
{

    Filter * pFilter;

    pFilter = (Filter *)Instance;

    switch (Port)
    {
    case SF_INPUT :
        pFilter->m_pfInput = DataLocation;
        break;
    case SF_COEF0:
        pFilter->m_pfCoef0 = DataLocation;
        break;
    case SF_COEF1:
        pFilter->m_pfCoef1 = DataLocation;
        break;
    case SF_COEF2:
        pFilter->m_pfCoef2 = DataLocation;
        break;
    case SF_COEF3:
        pFilter->m_pfCoef3 = DataLocation;
        break;
    case SF_COEF4:
        pFilter->m_pfCoef4 = DataLocation;
        break;
    case SF_COEF5:
        pFilter->m_pfCoef5 = DataLocation;
        break;
    case SF_COEF6:
        pFilter->m_pfCoef6 = DataLocation;
        break;
    case SF_COEF7:
        pFilter->m_pfCoef7 = DataLocation;
        break;
    case SF_COEF8:
        pFilter->m_pfCoef8 = DataLocation;
        break;
    case SF_COEF9:
        pFilter->m_pfCoef9 = DataLocation;
        break;
    case SF_COEF10:
        pFilter->m_pfCoef10 = DataLocation;
        break;
    case SF_COEF11:
        pFilter->m_pfCoef11 = DataLocation;
        break;
    case SF_COEF12:
        pFilter->m_pfCoef12 = DataLocation;
        break;
    case SF_COEF13:
        pFilter->m_pfCoef13 = DataLocation;
        break;
    case SF_COEF14:
        pFilter->m_pfCoef14 = DataLocation;
        break;
    case SF_COEF15:
        pFilter->m_pfCoef15 = DataLocation;
        break;
    case SF_OUTPUT:
        pFilter->m_pfOutput = DataLocation;
        break;
    }
}

/*****************************************************************************/

/* Roda a instancia do filtro adaptativo */
void runFilter(LADSPA_Handle Instance, unsigned long SampleCount)
{

    LADSPA_Data * pfBuffer; /* Vetor que armazena os valores antigos de x(n) */
    LADSPA_Data * pfCoef0;  /* Vetor que armazena os valores dos coeficientes do filtro */
    LADSPA_Data * pfCoef1;
    LADSPA_Data * pfCoef2;
    LADSPA_Data * pfCoef3;
    LADSPA_Data * pfCoef4;
    LADSPA_Data * pfCoef5;
    LADSPA_Data * pfCoef6;
    LADSPA_Data * pfCoef7;
    LADSPA_Data * pfCoef8;
    LADSPA_Data * pfCoef9;
    LADSPA_Data * pfCoef10;
    LADSPA_Data * pfCoef11;
    LADSPA_Data * pfCoef12;
    LADSPA_Data * pfCoef13;
    LADSPA_Data * pfCoef14;
    LADSPA_Data * pfCoef15;
    LADSPA_Data * pfInput; /* Aponta para o bloco de amostras da entrada x(n) */
    LADSPA_Data * pfOutput; /* Aponta para o bloco de amostras da saida */

    Filter * pFilter;

    unsigned long lBufferOffset;
    unsigned long lSampleIndex;

    pFilter = (Filter *)Instance;

    pfInput       =  pFilter->m_pfInput;
    pfOutput      =  pFilter->m_pfOutput;
    pfBuffer      =  pFilter->m_pfBuffer;
    pfCoef0       =  pFilter->m_pfCoef0;
    pfCoef1       =  pFilter->m_pfCoef1;
    pfCoef2       =  pFilter->m_pfCoef2;
    pfCoef3       =  pFilter->m_pfCoef3;
    pfCoef4       =  pFilter->m_pfCoef4;
    pfCoef5       =  pFilter->m_pfCoef5;
    pfCoef6       =  pFilter->m_pfCoef6;
    pfCoef7       =  pFilter->m_pfCoef7;
    pfCoef8       =  pFilter->m_pfCoef8;
    pfCoef9       =  pFilter->m_pfCoef9;
    pfCoef10      =  pFilter->m_pfCoef10;
    pfCoef11      =  pFilter->m_pfCoef11;
    pfCoef12      =  pFilter->m_pfCoef12;
    pfCoef13      =  pFilter->m_pfCoef13;
    pfCoef14      =  pFilter->m_pfCoef14;
    pfCoef15      =  pFilter->m_pfCoef15;

    lBufferOffset = pFilter->m_lBufferOffset;

    for (lSampleIndex = 0; lSampleIndex < SampleCount; lSampleIndex++)
    {
        /* O buffer recebe a mais recente amostra de x(n) */
        pfBuffer[((lBufferOffset + lSampleIndex) & TAM_FILTRO_1)] = *pfInput;

        /* Faz a "convolucao" do filtro */
        *pfOutput  = *pfCoef0  * pfBuffer[(lBufferOffset + lSampleIndex     ) & TAM_FILTRO_1];
        *pfOutput += *pfCoef1  * pfBuffer[(lBufferOffset + lSampleIndex -  1) & TAM_FILTRO_1];
        *pfOutput += *pfCoef2  * pfBuffer[(lBufferOffset + lSampleIndex -  2) & TAM_FILTRO_1];
        *pfOutput += *pfCoef3  * pfBuffer[(lBufferOffset + lSampleIndex -  3) & TAM_FILTRO_1];
        *pfOutput += *pfCoef4  * pfBuffer[(lBufferOffset + lSampleIndex -  4) & TAM_FILTRO_1];
        *pfOutput += *pfCoef5  * pfBuffer[(lBufferOffset + lSampleIndex -  5) & TAM_FILTRO_1];
        *pfOutput += *pfCoef6  * pfBuffer[(lBufferOffset + lSampleIndex -  6) & TAM_FILTRO_1];
        *pfOutput += *pfCoef7  * pfBuffer[(lBufferOffset + lSampleIndex -  7) & TAM_FILTRO_1];
        *pfOutput += *pfCoef8  * pfBuffer[(lBufferOffset + lSampleIndex -  8) & TAM_FILTRO_1];
        *pfOutput += *pfCoef9  * pfBuffer[(lBufferOffset + lSampleIndex -  9) & TAM_FILTRO_1];
        *pfOutput += *pfCoef10 * pfBuffer[(lBufferOffset + lSampleIndex - 10) & TAM_FILTRO_1];
        *pfOutput += *pfCoef11 * pfBuffer[(lBufferOffset + lSampleIndex - 11) & TAM_FILTRO_1];
        *pfOutput += *pfCoef12 * pfBuffer[(lBufferOffset + lSampleIndex - 12) & TAM_FILTRO_1];
        *pfOutput += *pfCoef13 * pfBuffer[(lBufferOffset + lSampleIndex - 13) & TAM_FILTRO_1];
        *pfOutput += *pfCoef14 * pfBuffer[(lBufferOffset + lSampleIndex - 14) & TAM_FILTRO_1];
        *pfOutput += *pfCoef15 * pfBuffer[(lBufferOffset + lSampleIndex - 15) & TAM_FILTRO_1];

        ++pfInput;
        ++pfOutput;

    }

    pFilter->m_lBufferOffset = (pFilter->m_lBufferOffset + SampleCount) & TAM_FILTRO_1; /* Atualiza o indice do ponteiro dos vetores circulares*/

}

/*****************************************************************************/

/* Throw away a simple delay line. */
void
cleanupFilter(LADSPA_Handle Instance)
{

    Filter * pFilter;

    pFilter = (Filter *)Instance;

    free(pFilter->m_pfBuffer);

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
        = 999;
        g_psDescriptor->Label
        = strdup("16coeffilter");
        g_psDescriptor->Properties
        = LADSPA_PROPERTY_HARD_RT_CAPABLE;
        g_psDescriptor->Name
        = strdup("Filtro com 16 coeficientes");
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
        piPortDescriptors[SF_COEF0]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF1]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF2]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF3]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF4]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF5]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF6]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF7]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF8]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF9]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF10]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF11]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF12]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF13]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF14]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_COEF15]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[SF_INPUT]
        = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        piPortDescriptors[SF_OUTPUT]
        = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        pcPortNames
        = (char **)calloc(NOPORTS, sizeof(char *));
        g_psDescriptor->PortNames
        = (const char **)pcPortNames;
        pcPortNames[SF_COEF0]
        = strdup("Coeficiente 1");
        pcPortNames[SF_COEF1]
        = strdup("Coeficiente 2");
        pcPortNames[SF_COEF2]
        = strdup("Coeficiente 3");
        pcPortNames[SF_COEF3]
        = strdup("Coeficiente 4");
        pcPortNames[SF_COEF4]
        = strdup("Coeficiente 5");
        pcPortNames[SF_COEF5]
        = strdup("Coeficiente 6");
        pcPortNames[SF_COEF6]
        = strdup("Coeficiente 7");
        pcPortNames[SF_COEF7]
        = strdup("Coeficiente 8");
        pcPortNames[SF_COEF8]
        = strdup("Coeficiente 9");
        pcPortNames[SF_COEF9]
        = strdup("Coeficiente 10");
        pcPortNames[SF_COEF10]
        = strdup("Coeficiente 11");
        pcPortNames[SF_COEF11]
        = strdup("Coeficiente 12");
        pcPortNames[SF_COEF12]
        = strdup("Coeficiente 13");
        pcPortNames[SF_COEF13]
        = strdup("Coeficiente 14");
        pcPortNames[SF_COEF14]
        = strdup("Coeficiente 15");
        pcPortNames[SF_COEF15]
        = strdup("Coeficiente 16");
        pcPortNames[SF_INPUT]
        = strdup("Input");
        pcPortNames[SF_OUTPUT]
        = strdup("Output");
        psPortRangeHints = ((LADSPA_PortRangeHint *)
                            calloc(NOPORTS, sizeof(LADSPA_PortRangeHint)));
        g_psDescriptor->PortRangeHints
        = (const LADSPA_PortRangeHint *)psPortRangeHints;
        psPortRangeHints[SF_COEF0].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF0].LowerBound
        = -1;
        psPortRangeHints[SF_COEF0].UpperBound
        = +1;
        psPortRangeHints[SF_COEF1].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF1].LowerBound
        = -1;
        psPortRangeHints[SF_COEF1].UpperBound
        = +1;
        psPortRangeHints[SF_COEF2].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF2].LowerBound
        = -1;
        psPortRangeHints[SF_COEF2].UpperBound
        = +1;
        psPortRangeHints[SF_COEF3].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF3].LowerBound
        = -1;
        psPortRangeHints[SF_COEF3].UpperBound
        = +1;
        psPortRangeHints[SF_COEF4].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF4].LowerBound
        = -1;
        psPortRangeHints[SF_COEF4].UpperBound
        = +1;
        psPortRangeHints[SF_COEF5].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF5].LowerBound
        = -1;
        psPortRangeHints[SF_COEF5].UpperBound
        = +1;
        psPortRangeHints[SF_COEF6].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF6].LowerBound
        = -1;
        psPortRangeHints[SF_COEF6].UpperBound
        = +1;
        psPortRangeHints[SF_COEF7].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF7].LowerBound
        = -1;
        psPortRangeHints[SF_COEF7].UpperBound
        = +1;
        psPortRangeHints[SF_COEF8].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF8].LowerBound
        = -1;
        psPortRangeHints[SF_COEF8].UpperBound
        = +1;
        psPortRangeHints[SF_COEF9].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF9].LowerBound
        = -1;
        psPortRangeHints[SF_COEF9].UpperBound
        = +1;
        psPortRangeHints[SF_COEF10].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF10].LowerBound
        = -1;
        psPortRangeHints[SF_COEF10].UpperBound
        = +1;
        psPortRangeHints[SF_COEF11].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF11].LowerBound
        = -1;
        psPortRangeHints[SF_COEF11].UpperBound
        = +1;
        psPortRangeHints[SF_COEF12].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF12].LowerBound
        = -1;
        psPortRangeHints[SF_COEF12].UpperBound
        = +1;
        psPortRangeHints[SF_COEF13].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF13].LowerBound
        = -1;
        psPortRangeHints[SF_COEF13].UpperBound
        = +1;
        psPortRangeHints[SF_COEF14].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF14].LowerBound
        = -1;
        psPortRangeHints[SF_COEF14].UpperBound
        = +1;
        psPortRangeHints[SF_COEF15].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
           | LADSPA_HINT_BOUNDED_ABOVE
           | LADSPA_HINT_DEFAULT_MIDDLE);
        psPortRangeHints[SF_COEF15].LowerBound
        = -1;
        psPortRangeHints[SF_COEF15].UpperBound
        = +1;
        psPortRangeHints[SF_INPUT].HintDescriptor
        = 0;
        psPortRangeHints[SF_OUTPUT].HintDescriptor
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
