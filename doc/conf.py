# Configuration file for the Sphinx documentation builder.
#
# Much of this file was generated automatically with sphinx-quickstart
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# http://www.sphinx-doc.org/en/master/config

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
import os
import sys
import subprocess
import re
import datetime

sys.path.append(os.getcwd())
sys.path.append(os.path.abspath('./ext'))

# -- Sphinx Version and Config -----------------------------------------------
# Sphinx will error and refuse to build if not equal to version
needs_sphinx='3.5'

# Sphinx-build fails for any broken __internal__ links. For external use make linkcheck.
nitpicky = True

# -- Project information -----------------------------------------------------

project = 'PETSc'
copyright = '1991-%d, UChicago Argonne, LLC and the PETSc Development Team' % datetime.date.today().year
author = 'The PETSc Development Team'

with open(os.path.join('..', 'include', 'petscversion.h'),'r') as version_file:
    buf = version_file.read()
    petsc_release_flag = re.search(' PETSC_VERSION_RELEASE[ ]*([0-9]*)',buf).group(1)
    major_version      = re.search(' PETSC_VERSION_MAJOR[ ]*([0-9]*)',buf).group(1)
    minor_version      = re.search(' PETSC_VERSION_MINOR[ ]*([0-9]*)',buf).group(1)
    subminor_version   = re.search(' PETSC_VERSION_SUBMINOR[ ]*([0-9]*)',buf).group(1)
    patch_version      = re.search(' PETSC_VERSION_PATCH[ ]*([0-9]*)',buf).group(1)


    git_describe_version = subprocess.check_output(['git', 'describe', '--always']).strip().decode('utf-8')
    if petsc_release_flag == '0':
        version = git_describe_version
        release = git_describe_version
    else:
        version = '.'.join([major_version, minor_version])
        release = '.'.join([major_version,minor_version,subminor_version])

# -- General configuration ---------------------------------------------------

master_doc = 'index'
# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']


# -- Extensions --------------------------------------------------------------
extensions = [
    'sphinx_copybutton',
    'sphinxcontrib.bibtex',
    'sphinxcontrib.katex',
    'sphinxcontrib.rsvgconverter',
    'html5_petsc',
]

copybutton_prompt_text = r"[>]{1,3}"
copybutton_prompt_is_regexp = True


# -- Options for HTML output -------------------------------------------------

html_theme = 'pydata_sphinx_theme'

html_theme_options = {
    "icon_links": [
        {
            "name": "GitLab",
            "url": "https://gitlab.com/petsc/petsc",
            "icon": "fab fa-gitlab",
        },
    ],
    "use_edit_page_button": True,
    "footer_items": ["copyright", "sphinx-version", "last-updated"],
}

# The theme uses "github" here, but it works for GitLab
html_context = {
    "github_url": "https://gitlab.com",
    "github_user": "petsc",
    "github_repo": "petsc",
    "github_version": "main",
    "doc_path": "doc",
}

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']

html_logo = os.path.join('..', 'src', 'docs', 'website','images','PETSc-TAO_RGB.svg')
html_favicon = os.path.join('..', 'src', 'docs', 'website','images','PETSc_RGB-logo.png')

# Extra preprocessing for included "classic" docs
import build_classic_docs
html_extra_dir = build_classic_docs.main()

# Additional files that are simply copied over with an HTML build
html_extra_path = [html_extra_dir]

html_last_updated_fmt = r'%Y-%m-%dT%H:%M:%S%z (' + git_describe_version + ')'

# -- Options for LaTeX output --------------------------------------------

bibtex_bibfiles = [
        os.path.join('..', 'src', 'docs', 'tex', 'petsc.bib'),
        os.path.join('..', 'src', 'docs', 'tex', 'petscapp.bib'),
        ]
latex_engine = 'xelatex'

# Specify how to arrange the documents into LaTeX files.
# This allows building only the manual.
latex_documents = [
        ('manual/index', 'manual.tex', 'PETSc Users Manual', author, 'manual', False)
        ]

latex_additional_files = [
    'manual/anl_tech_report/ArgonneLogo.pdf',
    'manual/anl_tech_report/ArgonneReportTemplateLastPage.pdf',
    'manual/anl_tech_report/ArgonneReportTemplatePage2.pdf',
    'manual/anl_tech_report/first.inc',
    'manual/anl_tech_report/last.inc',
]

latex_elements = {
    'maketitle': r'\newcommand{\techreportversion}{%s}' % version +
r'''
\input{first.inc}
\sphinxmaketitle
''',
    'printindex': r'''
\printindex
\input{last.inc}
''',
    'fontpkg': r'''
\setsansfont{DejaVu Sans}
\setmonofont{DejaVu Sans Mono}
'''
}


# -- General Config Options ---------------------------------------------------

# Set default highlighting language
highlight_language = 'c'
autosummary_generate = True
numfig = True

# We must check what kind of builder the app uses to adjust
def builder_init_handler(app):
    import genteamtable
    print("============================================")
    print("    GENERATING TEAM TABLE FROM CONF.PY      ")
    print("============================================")
    genDirName = "generated"
    cwdPath = os.path.dirname(os.path.realpath(__file__))
    genDirPath = os.path.join(cwdPath, genDirName)
    if "PETSC_GITLAB_PRIVATE_TOKEN" in os.environ:
        token = os.environ["PETSC_GITLAB_PRIVATE_TOKEN"]
    else:
        token = None
    genteamtable.main(genDirPath, token, app.builder.name)
    return None

# Supposedly the safer way to add additional css files. Setting html_css_files will
# overwrite previous versions of the variable that some extension may have set. This will
# add our css files in addition to it.
def setup(app):
    # Register the builder_init_handler to be called __after__ app.builder has been initialized
    app.connect('builder-inited', builder_init_handler)
    app.add_css_file('css/pop-up.css')
    app.add_css_file('css/petsc-team-container.css')
