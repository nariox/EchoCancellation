/* adapt.c

   Free software by Pedro Nariyoshi. Do with as you will. No
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

/* Maximo tempo de eco (cuidado com a memória) */
#define MAX_COEFS 8000

/*****************************************************************************/

/* The port numbers for the plugin: */

#define SDL_FILTER_LENGTH 0
#define SDL_MU            1
#define SDL_INPUTD        2
#define SDL_INPUTX        3
#define SDL_OUTPUT        4

/* Quantidade de portas */

#define NOPORTS 5

/*****************************************************************************/

#define LIMIT_BETWEEN_0_AND_1(x)          \
(((x) < 0) ? 0 : (((x) > 1) ? 1 : (x)))
#define LIMIT_BETWEEN_0_AND_MAX_COEFS(x)  \
(((x) < 0) ? 0 : (((x) > MAX_COEFS) ? MAX_COEFS : (x)))

/*****************************************************************************/

/* Instance data for the simple adaptive filter plugin. */
typedef struct {

  LADSPA_Data m_fSampleRate;

  LADSPA_Data * m_pfBuffer;

  LADSPA_Data * m_pfCoefs; /* coeficientes do filtro */

  /* Buffer size, a power of two. */ /* O tamanho do buffer em potencia de 2 agiliza a "circularização do vetor, transformando uma equacao de resto de divisão em uma operação de E lógico bit a bit */
  unsigned long m_lFilterSize;

  /* Write pointer in buffer. */
  unsigned long m_lWritePointer;

  /* Ports:
     ------ */

  /* Tamanho do echo maximo em ms */
  LADSPA_Data * m_pfEchoTime;

  /* Valor do fator de convergencia */
  LADSPA_Data * m_pfMu;

  /* Input audio port data location. */
  LADSPA_Data * m_pfInputD;

  /* Input audio port data location. */
  LADSPA_Data * m_pfInputX;

  /* Output audio port data location. */
  LADSPA_Data * m_pfOutput;

} SimpleAdaptiveFilter;

/*****************************************************************************/

/* Construct a new plugin instance. */
LADSPA_Handle
instantiateSimpleAdaptiveFilter(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {

  unsigned long lMinimumBufferSize;
  SimpleAdaptiveFilter * psAdaptiveFilter;

  psAdaptiveFilter
    = (SimpleAdaptiveFilter *)malloc(sizeof(SimpleAdaptiveFilter));

  if (psAdaptiveFilter == NULL)

    return NULL; /* Melhorar este return */

  psAdaptiveFilter->m_fSampleRate = (LADSPA_Data)SampleRate;

  /* O tamanho do buffer e' a menor potencia de dois que seja maior que o tamanho necessario */
  /* Isto torna a "circularizacao" do vetor muito mais simples */
  lMinimumBufferSize = (unsigned long) MAX_COEFS;
  psAdaptiveFilter->m_lFilterSize = 1;
  while (psAdaptiveFilter->m_lFilterSize < lMinimumBufferSize) /*multiplica por 2 até ser maior que o buffer mínimo */
    psAdaptiveFilter->m_lFilterSize <<= 1;

  psAdaptiveFilter->m_pfBuffer  = (LADSPA_Data *)calloc(psAdaptiveFilter->m_lFilterSize, sizeof(LADSPA_Data)); /* cria um buffer "zerado" com o tamanho achado acima de LADSPA_Datas (ou seja floats, ver em ladspa.h) */
  psAdaptiveFilter->m_pfCoefs  = (LADSPA_Data *)calloc(psAdaptiveFilter->m_lFilterSize, sizeof(LADSPA_Data));

  if (psAdaptiveFilter->m_pfBuffer == NULL) {
    free(psAdaptiveFilter);
    return NULL;  /* Melhorar este return */
  }

  if (psAdaptiveFilter->m_pfCoefs == NULL) {
    free(psAdaptiveFilter);
    return NULL;  /* Melhorar este return */
  }

  psAdaptiveFilter->m_lWritePointer = 0; /* Inicializa o vetor de escrita */

  return psAdaptiveFilter;
}

/*****************************************************************************/

/* Initialise and activate a plugin instance. */
void
activateSimpleAdaptiveFilter(LADSPA_Handle Instance) {

  SimpleAdaptiveFilter * psSimpleAdaptiveFilter;
  psSimpleAdaptiveFilter = (SimpleAdaptiveFilter *)Instance;

  /* Need to reset the delay history in this function rather than
     instantiate() in case deactivate() followed by activate() have
     been called to reinitialise a delay line. */
  memset(psSimpleAdaptiveFilter->m_pfBuffer, 0, sizeof(LADSPA_Data) * psSimpleAdaptiveFilter->m_lFilterSize);
  memset(psSimpleAdaptiveFilter->m_pfCoefs, 0, sizeof(LADSPA_Data) * psSimpleAdaptiveFilter->m_lFilterSize);
} /* Atribui zero a todos os "size of... " bytes do m_pfBuffer e do m_pfCoefs*/

/*****************************************************************************/

/* Connect a port to a data location. */
void
connectPortToSimpleAdaptiveFilter(LADSPA_Handle Instance,
			     unsigned long Port,
			     LADSPA_Data * DataLocation) {

  SimpleAdaptiveFilter * psSimpleAdaptiveFilter;

  psSimpleAdaptiveFilter = (SimpleAdaptiveFilter *)Instance;
  switch (Port) {
  case SDL_FILTER_LENGTH :
    psSimpleAdaptiveFilter->m_pfEchoTime = DataLocation;
    break;
  case SDL_MU:
    psSimpleAdaptiveFilter->m_pfMu = DataLocation;
    break;
  case SDL_INPUTD:
    psSimpleAdaptiveFilter->m_pfInputD = DataLocation;
    break;
  case SDL_INPUTX:
    psSimpleAdaptiveFilter->m_pfInputX = DataLocation;
    break;
  case SDL_OUTPUT:
    psSimpleAdaptiveFilter->m_pfOutput = DataLocation;
    break;
  }
}

/*****************************************************************************/

/* Roda a instancia do filtro adaptativo */
void
runSimpleAdaptiveFilter(LADSPA_Handle Instance,
		   unsigned long SampleCount) {

  LADSPA_Data * pfBuffer; /* Vetor que armazena os valores antigos de x(n) */
  LADSPA_Data * pfCoefs;  /* Vetor que armazena os valores dos coeficientes do filtro */
  LADSPA_Data * pfInputD; /* Aponta para o bloco de amostras da entrada d(n) */
  LADSPA_Data * pfInputX; /* Aponta para o bloco de amostras da entrada x(n) */
  LADSPA_Data * pfOutput; /* Aponta para o bloco de amostras da saida */
  LADSPA_Data fConvSample;
  LADSPA_Data fErrSample;
  LADSPA_Data fMu;
  SimpleAdaptiveFilter * psSimpleAdaptiveFilter;
  unsigned long lBufferSizeMinusOne;
  unsigned long lBufferWriteOffset;
  unsigned long lCoefs;
  unsigned long lIndex;
  unsigned short lSampleIndex;
  unsigned short lConv; /* Contador da convolucao */

  psSimpleAdaptiveFilter = (SimpleAdaptiveFilter *)Instance;
  lBufferSizeMinusOne = psSimpleAdaptiveFilter->m_lFilterSize - 1;
  lCoefs = (unsigned long)LIMIT_BETWEEN_0_AND_MAX_COEFS(*psSimpleAdaptiveFilter->m_pfEchoTime);

  pfInputD =  psSimpleAdaptiveFilter->m_pfInputD;
  pfInputX =  psSimpleAdaptiveFilter->m_pfInputX;
  pfOutput =  psSimpleAdaptiveFilter->m_pfOutput;
  pfCoefs  =  psSimpleAdaptiveFilter->m_pfCoefs;
  pfBuffer =  psSimpleAdaptiveFilter->m_pfBuffer;
  fMu      = *psSimpleAdaptiveFilter->m_pfMu;
  lBufferWriteOffset = psSimpleAdaptiveFilter->m_lWritePointer;

  for (lSampleIndex = 0; lSampleIndex < SampleCount; lSampleIndex++)
  {
      lIndex = lSampleIndex + lBufferWriteOffset;

      pfBuffer[(lIndex & lBufferSizeMinusOne)] = *(pfInputX++); /* O buffer recebe a mais recente amostra de x(n) */

      fConvSample = 0; /* zera a convolucao */
      for(lConv = 0; lConv < lCoefs; lConv++)
      {
          fConvSample+=pfCoefs[lConv]*pfBuffer[((lIndex - lConv) & lBufferSizeMinusOne)];
      }

    fErrSample = *(pfInputD++) - fConvSample;
    *(pfOutput++) = fErrSample; /* Joga o "erro" na saida */

        if (fErrSample > 0)
        {
            for(lConv = 0; lConv < lCoefs; lConv++) /* w(n+1) = w(n) + 2 * mu * e(n) * X(n) */
                pfCoefs[lConv]+=fMu*pfBuffer[((lIndex - lConv) & lBufferSizeMinusOne)];
        }
        else if (fErrSample < 0)
        {
            for(lConv = 0; lConv < lCoefs; lConv++) /* w(n+1) = w(n) + 2 * mu * e(n) * X(n) */
                pfCoefs[lConv]-=fMu*pfBuffer[((lIndex - lConv) & lBufferSizeMinusOne)];
        }

  }

  psSimpleAdaptiveFilter->m_lWritePointer = ((psSimpleAdaptiveFilter->m_lWritePointer + SampleCount) & lBufferSizeMinusOne); /* Atualiza o indice do ponteiro dos vetores circulares*/
}

/*****************************************************************************/

/* Throw away a simple delay line. */
void
cleanupSimpleAdaptiveFilter(LADSPA_Handle Instance) {

  SimpleAdaptiveFilter * psSimpleAdaptiveFilter;

  psSimpleAdaptiveFilter = (SimpleAdaptiveFilter *)Instance;

  free(psSimpleAdaptiveFilter->m_pfBuffer);
  free(psSimpleAdaptiveFilter->m_pfCoefs);
  free(psSimpleAdaptiveFilter);
}

/*****************************************************************************/

LADSPA_Descriptor * g_psDescriptor = NULL;

/*****************************************************************************/

/* _init() is called automatically when the plugin library is first
   loaded. */
void
_init() {

  char ** pcPortNames;
  LADSPA_PortDescriptor * piPortDescriptors;
  LADSPA_PortRangeHint * psPortRangeHints;

  g_psDescriptor
    = (LADSPA_Descriptor *)malloc(sizeof(LADSPA_Descriptor));
  if (g_psDescriptor) {
    g_psDescriptor->UniqueID
      = 4;
    g_psDescriptor->Label
      = strdup("adapt_lms4");
    g_psDescriptor->Properties
      = LADSPA_PROPERTY_HARD_RT_CAPABLE;
    g_psDescriptor->Name
      = strdup("Simple LMS : SIGN LMS");
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
    piPortDescriptors[SDL_FILTER_LENGTH]
      = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
    piPortDescriptors[SDL_MU]
      = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
    piPortDescriptors[SDL_INPUTD]
      = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
    piPortDescriptors[SDL_INPUTX]
      = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
    piPortDescriptors[SDL_OUTPUT]
      = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
    pcPortNames
      = (char **)calloc(NOPORTS, sizeof(char *));
    g_psDescriptor->PortNames
      = (const char **)pcPortNames;
    pcPortNames[SDL_FILTER_LENGTH]
      = strdup("Tamanho do filtro (s)");
    pcPortNames[SDL_MU]
      = strdup("µ - Fator de convergencia");
    pcPortNames[SDL_INPUTD]
      = strdup("Input D");
    pcPortNames[SDL_INPUTX]
      = strdup("Input X");
    pcPortNames[SDL_OUTPUT]
      = strdup("Output");
    psPortRangeHints = ((LADSPA_PortRangeHint *)
			calloc(NOPORTS, sizeof(LADSPA_PortRangeHint)));
    g_psDescriptor->PortRangeHints
      = (const LADSPA_PortRangeHint *)psPortRangeHints;
    psPortRangeHints[SDL_FILTER_LENGTH].HintDescriptor
      = (LADSPA_HINT_BOUNDED_BELOW
	 | LADSPA_HINT_BOUNDED_ABOVE
	 | LADSPA_HINT_DEFAULT_MIDDLE);
    psPortRangeHints[SDL_FILTER_LENGTH].LowerBound
      = 0;
    psPortRangeHints[SDL_FILTER_LENGTH].UpperBound
      = (LADSPA_Data)MAX_COEFS;
    psPortRangeHints[SDL_MU].HintDescriptor
      = (LADSPA_HINT_BOUNDED_BELOW
	 | LADSPA_HINT_BOUNDED_ABOVE
	 | LADSPA_HINT_DEFAULT_LOW);
    psPortRangeHints[SDL_MU].LowerBound
      = 0;
    psPortRangeHints[SDL_MU].UpperBound
      = 0.05;
    psPortRangeHints[SDL_INPUTD].HintDescriptor
      = 1;
    psPortRangeHints[SDL_INPUTX].HintDescriptor
      = 0;
    psPortRangeHints[SDL_OUTPUT].HintDescriptor
      = 0;
    g_psDescriptor->instantiate
      = instantiateSimpleAdaptiveFilter;
    g_psDescriptor->connect_port
      = connectPortToSimpleAdaptiveFilter;
    g_psDescriptor->activate
      = activateSimpleAdaptiveFilter;
    g_psDescriptor->run
      = runSimpleAdaptiveFilter;
    g_psDescriptor->run_adding
      = NULL;
    g_psDescriptor->set_run_adding_gain
      = NULL;
    g_psDescriptor->deactivate
      = NULL;
    g_psDescriptor->cleanup
      = cleanupSimpleAdaptiveFilter;
  }
}

/*****************************************************************************/

/* _fini() is called automatically when the library is unloaded. */
void
_fini() {
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
