# python idlc.py example.yaml
# if ($LASTEXITCODE -ne 0) {
# 	exit 1;
# }

python idlc.py D:\mongo\src\mongo\idl\unittest.idl
if ($LASTEXITCODE -ne 0) {
	exit 1;
}

pushd d:\mongo
cl /Fobuild\myvar1\mongo\idl\idl_stub.obj /c src\mongo\idl\idl_stub.cpp /TP /nologo /EHsc /W3 /wd4355 /wd4800 /wd4267 /wd4244 /wd4290 /wd4068 /wd4351 /we4013 /we4099 /we4930 /Z7 /errorReport:none /MDd /Od /RTC1 /bigobj /utf-8 /Zc:rvalueCast /Zc:strictStrings /volatile:iso /DPCRE_STATIC /DBOOST_ALL_NO_LIB /D_UNICODE /DUNICODE /D_CONSOLE /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /DNTDDI_VERSION=0x06010000 /DBOOST_THREAD_VERSION=4 /DBOOST_THREAD_DONT_PROVIDE_VARIADIC_THREAD /DBOOST_SYSTEM_NO_DEPRECATED /DBOOST_MATH_NO_LONG_DOUBLE_MATH_FUNCTIONS /DBOOST_THREAD_DONT_PROVIDE_INTERRUPTIONS /DBOOST_THREAD_HAS_NO_EINTR_BUG /Isrc\third_party\pcre-8.39 /Isrc\third_party\boost-1.60.0 /ID:\lib\sasl\include /ID:\lib\ssl\include /ID:\lib\snmp\include /ID:\lib\curl\include /Ibuild\myvar1 /Isrc /Z7
if ($LASTEXITCODE -ne 0) {
    popd
	exit 1;
}

cl /Fobuild\myvar1\mongo\idl\idl_parser.obj /c src\mongo\idl\idl_parser.cpp /TP /nologo /EHsc /W3 /wd4355 /wd4800 /wd4267 /wd4244 /wd4290 /wd4068 /wd4351 /we4013 /we4099 /we4930 /Z7 /errorReport:none /MDd /Od /RTC1 /bigobj /utf-8 /Zc:rvalueCast /Zc:strictStrings /volatile:iso /DPCRE_STATIC /DBOOST_ALL_NO_LIB /D_UNICODE /DUNICODE /D_CONSOLE /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /DNTDDI_VERSION=0x06010000 /DBOOST_THREAD_VERSION=4 /DBOOST_THREAD_DONT_PROVIDE_VARIADIC_THREAD /DBOOST_SYSTEM_NO_DEPRECATED /DBOOST_MATH_NO_LONG_DOUBLE_MATH_FUNCTIONS /DBOOST_THREAD_DONT_PROVIDE_INTERRUPTIONS /DBOOST_THREAD_HAS_NO_EINTR_BUG /Isrc\third_party\pcre-8.39 /Isrc\third_party\boost-1.60.0 /ID:\lib\sasl\include /ID:\lib\ssl\include /ID:\lib\snmp\include /ID:\lib\curl\include /Ibuild\myvar1 /Isrc /Z7
if ($LASTEXITCODE -ne 0) {
    popd
	exit 1;
}

link '@d:\mongo\buildscripts\idl\idlstub.exe.rsp'
if ($LASTEXITCODE -ne 0) {
	popd
	exit 1;
}

popd

