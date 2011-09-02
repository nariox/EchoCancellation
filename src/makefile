###############################################################################
#
# Installation DIRECTORIES
#
# Change these if you want to install somewhere else.

INSTALL_PLUGINS_DIR	=	/usr/lib/ladspa/
INSTALL_INCLUDE_DIR	=	/usr/include/

###############################################################################
#
# GENERAL
#

INCLUDES	=	-I.
LIBRARIES	=	-lblas -latlas -lm -ldl # -llapack_atlas -llapack 
CFLAGS		=	$(INCLUDES) -Wall -Werror -O3 -fPIC -march=native 
CXXFLAGS	=	$(CFLAGS)
PLUGINS		=	../plugins/nlmsgeigel.so	\
          	    ../plugins/lmsgeigel.so	   	\
          	    ../plugins/adapt.so			\
				../plugins/fnlmscncr.so		\
				../plugins/nlmscncr.so		\
				../plugins/nlnlmscncr.so	\
				../plugins/nlnlmscncr2.so	\
				../plugins/nlnlmscncr3.so	\
				../plugins/16coefs.so		\
				../plugins/nl16coefs.so		\
				../plugins/noise.so
CC		=	cc
CPP		=	c++

###############################################################################
#
# RULES TO BUILD PLUGINS FROM C OR C++ CODE
#

../plugins/%.so:	plugins/%.c ladspa.h
	$(CC) $(CFLAGS) -o plugins/$*.o -c plugins/$*.c
	$(LD) -o ../plugins/$*.so plugins/$*.o -shared $(LIBRARIES)

../plugins/%.so:	plugins/%.cpp ladspa.h
	$(CPP) $(CXXFLAGS) -o plugins/$*.o -c plugins/$*.cpp
	$(CPP) -o ../plugins/$*.so plugins/$*.o -shared  $(LIBRARIES)

###############################################################################
#
# TARGETS
#

install:	targets
	-mkdir $(INSTALL_PLUGINS_DIR)
	-mkdir $(INSTALL_INCLUDE_DIR)
	cp ../plugins/* $(INSTALL_PLUGINS_DIR)
	cp ladspa.h $(INSTALL_INCLUDE_DIR)

targets:	$(PLUGINS)

###############################################################################

#	
# UTILITIES
#

always:

clean:
	-rm -f `find . -name "*.o"` ../bin/* ../plugins/*
	-rm -f `find .. -name "*~"`
	-rm -f *.bak core score.srt
	-rm -f *.bb *.bbg *.da *-ann gmon.out bb.out
	-rm -f `find .. -name "*.class"`

###############################################################################

