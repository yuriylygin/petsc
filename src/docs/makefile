
CFLAGS   =
FFLAGS   =
SOURCEC  =
SOURCEF  =
SOURCEH  =
TAODOCS  =  \
	   tao_tex/tao.bib

DOCS     = manualpages-sec/header_* \
           tex/petsc.bib tex/petscapp.bib tex/petscconsult.bib website/index.html \
           website/download/index.html \
           website/download/petsc-patches.html website/publications/index.html \
           website/features/index.html website/features/details.html website/features/diagram.html \
           website/miscellaneous/index.html website/miscellaneous/funding.html \
           website/miscellaneous/acknwldg.html website/miscellaneous/petscthreads.html \
           website/miscellaneous/usage.html  \
           website/documentation/index.html website/documentation/benchmarks.html \
           website/documentation/bugreporting.html website/documentation/codemanagement.html \
           website/documentation/copyright.html website/documentation/license.html website/documentation/faq.html \
           website/documentation/installation.html website/documentation/linearsolvertable.html \
           website/documentation/referencing.html\
           website/documentation/exercises/index.html website/documentation/exercises/*/index.html \
           website/documentation/changes/*.html ${TAODOCS}
LIBBASE  =
LINCLUDE = ${SOURCEH}
DIRS     =
LOCDIR   = src/docs/

CLEANFILES = website/publications/petscapps.html website/publications/petscapps-bib.html

include ${PETSC_DIR}/lib/petsc/conf/variables
include ${PETSC_DIR}/lib/petsc/conf/rules
include ${PETSC_DIR}/lib/petsc/conf/test

# petscapps* get built with 'builddist' and 'rsync'ed over to petsc web via the crontab
bib2html: clean
	${BIB2HTML} -group doctext/petsc.group -listfile website/publications/petscapps.html -listconfig doctext/petscapp.cfg tex/petscapp.bib -nospecials > website/publications/petscapps-bib.html

years:
	for i in 1993 1994 1995 1996 1997 1998 1999 2000 2001 2002 2003 2004; do\
          echo -n $${i};\
          grep $${i} tex/petscapp.bib | grep -i year | wc -l;\
        done

