AM_CPPFLAGS = \
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\"

AM_CFLAGS =\
	 -Wall\
	 -O3

man_MANS = rmlinks.1

bin_PROGRAMS = rmlinks

rmlinks_SOURCES = \
	main.c

rmlinks_LDFLAGS = 

#ifdef linux
rmlinks_LDADD = -lpthread
#endif

