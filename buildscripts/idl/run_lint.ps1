$ErrorActionPreference = "Stop"

# Format code
C:\Python36\Scripts\yapf.exe -i -r .

c:\\python36\\scripts\\mypy.bat --py2 --disallow-untyped-defs --ignore-missing-imports .
if ($LASTEXITCODE -ne 0) {
	exit 1;
}

# TODO: 
# Run pylint
# Run pydocstyle
#python -m tests.test_binder
# pylint -f msvs -r n .\idl