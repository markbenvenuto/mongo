$ErrorActionPreference = “Stop”

##c:\\python36\\scripts\\mypy.bat --py2 --disallow-untyped-defs --ignore-missing-imports .
#if ($LASTEXITCODE -ne 0) {
#	exit 1;
#}

python -m unittest discover -s tests
#python -m tests.test_parser
if ($LASTEXITCODE -ne 0) {
	exit 1;
}

#python -m tests.test_binder
