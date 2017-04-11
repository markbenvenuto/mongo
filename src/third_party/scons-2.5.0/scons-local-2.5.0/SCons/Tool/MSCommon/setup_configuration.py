import ctypes
from ctypes import byref, POINTER, HRESULT
from ctypes.wintypes import FILETIME
import comtypes
from comtypes import GUID, IUnknown, CoCreateInstanceEx, STDMETHOD, COMMETHOD
from comtypes.automation import VARIANT_BOOL, VARIANT
# noinspection PyProtectedMember
from comtypes.automation import _midlSAFEARRAY  # This is technically the public interface for SAFEARRAY


class ISetupPackageReference(IUnknown):
    _iid_ = GUID("{DA8D8A16-B2B6-4487-A2F1-594CCCCD6BF5}")

    _methods_ = [
        COMMETHOD([], HRESULT, "GetId",
                  (['out'], POINTER(comtypes.BSTR), "pbstrId")),
        COMMETHOD([], HRESULT, "GetVersion",
                  (['out'], POINTER(comtypes.BSTR), "pbstrVersion")),
        COMMETHOD([], HRESULT, "GetChip",
                  (['out'], POINTER(comtypes.BSTR), "pbstrChip")),
        COMMETHOD([], HRESULT, "GetLanguage",
                  (['out'], POINTER(comtypes.BSTR), "pbstrLanguage")),
        COMMETHOD([], HRESULT, "GetBranch",
                  (['out'], POINTER(comtypes.BSTR), "pbstrBranch")),
        COMMETHOD([], HRESULT, "GetType",
                  (['out'], POINTER(comtypes.BSTR), "pbstrType")),
        COMMETHOD([], HRESULT, "GetUniqueId",
                  (['out'], POINTER(comtypes.BSTR), "pbstrUniqueId")),
        COMMETHOD([], HRESULT, "GetIsExtension",
                  (['out'], POINTER(VARIANT_BOOL), "pfIsExtension"))
    ]


class ISetupFailedPackageReference(ISetupPackageReference):
    _iid_ = GUID("{E73559CD-7003-4022-B134-27DC650B280F}")


class ISetupInstance(IUnknown):
    _iid_ = GUID("{B41463C3-8866-43B5-BC33-2B0676F7F42E}")

    _methods_ = [
        COMMETHOD([], HRESULT, "GetInstanceId",
                  (['out'], POINTER(comtypes.BSTR), "pbstrInstanceId")),
        COMMETHOD([], HRESULT, "GetInstallDate",
                  (['out'], POINTER(FILETIME), "pInstallDate")),
        COMMETHOD([], HRESULT, "GetInstallationName",
                  (['out'], POINTER(comtypes.BSTR), "pbstrInstallationName")),
        COMMETHOD([], HRESULT, "GetInstallationPath",
                  (['out'], POINTER(comtypes.BSTR), "pbstrInstallationPath")),
        COMMETHOD([], HRESULT, "GetInstallationVersion",
                  (['out'], POINTER(comtypes.BSTR), "pbstrInstallationVersion")),
        COMMETHOD([], HRESULT, "GetDisplayName",
                  (['in'], ctypes.c_uint32, "lcid"),
                  (['out'], POINTER(comtypes.BSTR), "pbstrDisplayName")),
        COMMETHOD([], HRESULT, "GetDescription",
                  (['in'], ctypes.c_uint32, "lcid"),
                  (['out'], POINTER(comtypes.BSTR), "pbstrDescription")),
        COMMETHOD([], HRESULT, "ResolvePath",
                  (['in', 'optional'], comtypes.BSTR, "pwszRelativePath"),  # Should be LPCOLESTR
                  (['out'], POINTER(comtypes.BSTR), "pbstrAbsolutePath"))
    ]


class ISetupErrorState(IUnknown):
    _iid_ = GUID("{46DCCD94-A287-476A-851E-DFBC2FFDBC20}")

    _methods_ = [
        COMMETHOD([], HRESULT, "GetFailedPackages",
                  (['out'], POINTER(_midlSAFEARRAY(POINTER(ISetupFailedPackageReference))), "ppsaFailedPackages")),
        COMMETHOD([], HRESULT, "GetSkippedPackages",
                  (['out'], POINTER(_midlSAFEARRAY(POINTER(ISetupPackageReference))), "ppsaSkippedPackages"))
    ]


class ISetupPropertyStore(IUnknown):
    _iid_ = GUID("{C601C175-A3BE-44BC-91F6-4568D230FC83}")

    _methods_ = [
        COMMETHOD([], HRESULT, "GetNames",
                  (['out'], POINTER(_midlSAFEARRAY(comtypes.BSTR)), "ppsaNames")),
        COMMETHOD([], HRESULT, "GetValue",
                  (['in'], comtypes.BSTR, "pwszName"),
                  (['out'], POINTER(VARIANT), "pvtValue"))
    ]


class InstanceState(object):
    eNone = 0
    eLocal = 1
    eRegistered = 2
    eNoRebootRequired = 4
    eNoErrors = 8
    eComplete = 0x0FFFFFFFF  # Should be max uint32


class ISetupInstance2(ISetupInstance):
    _iid_ = GUID("{89143C9A-05AF-49B0-B717-72E218A2185C}")

    _methods_ = [
        COMMETHOD([], HRESULT, "GetState",
                  (['out'], POINTER(ctypes.c_uint32), "pState")),
        COMMETHOD([], HRESULT, "GetPackages",
                  (['out'], POINTER(_midlSAFEARRAY(POINTER(ISetupPackageReference))), "ppsaPackages")),
        COMMETHOD([], HRESULT, "GetProduct",
                  (['out'], POINTER(POINTER(ISetupPackageReference)), "ppPackage")),
        COMMETHOD([], HRESULT, "GetProductPath",
                  (['out'], POINTER(comtypes.BSTR), "pbstrProductPath")),
        COMMETHOD([], HRESULT, "GetErrors",
                  (['out'], POINTER(POINTER(ISetupErrorState)), "ppErrorState")),
        COMMETHOD([], HRESULT, "IsLaunchable",
                  (['out'], POINTER(VARIANT_BOOL), "pfIsLaunchable")),
        COMMETHOD([], HRESULT, "IsComplete",
                  (['out'], POINTER(VARIANT_BOOL), "pfIsComplete")),
        COMMETHOD([], HRESULT, "GetProperties",
                  (['out'], POINTER(POINTER(ISetupPropertyStore)), "ppProperties")),
        COMMETHOD([], HRESULT, "GetEnginePath",
                  (['out'], POINTER(comtypes.BSTR), "pbstrEnginePath"))
    ]


# The following inspections can be ignored as the naming is attributed to the Windows APIs and the unresolved functions
# are generated at runtime.
# noinspection PyPep8Naming,PyUnresolvedReferences
class IEnumSetupInstances(IUnknown):
    _iid_ = GUID("{6380BCFF-41D3-4B2E-8B2E-BF8A6810C848}")

    _methods_ = [
        STDMETHOD(HRESULT, "Next", [ctypes.c_ulong, POINTER(POINTER(ISetupInstance)), POINTER(ctypes.c_ulong)]),
        STDMETHOD(HRESULT, "Skip", [ctypes.c_ulong]),
        STDMETHOD(HRESULT, "Reset"),
        STDMETHOD(HRESULT, "Clone", [POINTER(ctypes.c_void_p)])
    ]

    def Next(self):
        p = POINTER(ISetupInstance)()
        result = self.__com_Next(1, byref(p), None)
        return p if result == 0 else None

    def Clone(self):
        p = POINTER(IEnumSetupInstances)()
        self.__com_Clone(byref(p))
        return p

    def __iter__(self):
        return self

    def next(self):
        p = self.Next()
        if p is None:
            raise StopIteration

        return p

    def __next__(self):
        return self.next()


class ISetupConfiguration(IUnknown):
    _iid_ = GUID("{42843719-DB4C-46C2-8E7C-64F1816EFD5B}")

    _methods_ = [
        COMMETHOD([], HRESULT, "EnumInstances",
                  (['out'], POINTER(POINTER(IEnumSetupInstances)), "ppEnumInstances")),
        COMMETHOD([], HRESULT, "GetInstanceForCurrentProcess",
                  (['out'], POINTER(POINTER(ISetupInstance)), "ppInstance")),
        COMMETHOD([], HRESULT, "GetInstanceForPath",
                  (['in'], ctypes.c_wchar_p, "wzPath"),
                  (['out'], POINTER(POINTER(ISetupInstance)), "ppInstance"))
    ]


class ISetupConfiguration2(ISetupConfiguration):
    _iid_ = GUID("{26AAB78C-4A60-49D6-AF3B-3C35BC93365D}")

    _methods_ = [
        STDMETHOD(HRESULT, "EnumAllInstances", [POINTER(POINTER(IEnumSetupInstances))])
    ]


class ISetupHelper(IUnknown):
    _iid_ = GUID("{42B21B78-6192-463E-87BF-D577838F1D5C}")

    _methods_ = [
        COMMETHOD([], HRESULT, "ParseVersion",
                  (['in'], comtypes.BSTR, "pwszVersion"),
                  (['out'], POINTER(ctypes.c_ulonglong), "pullVersion")),
        COMMETHOD([], HRESULT, "ParseVersionRange",
                  (['in'], ctypes.c_char_p, "pwszVersionRange"),
                  (['out'], POINTER(ctypes.c_ulonglong), "pullMinVersion"),
                  (['out'], POINTER(ctypes.c_ulonglong), "pullMaxVersion"))
    ]

setup_configuration_clsid = '{177F0C4A-1CD3-4DE7-A32C-71DBBB9FA36D}'

VCToolSetComponent = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
Win10SDKComponent = "Microsoft.VisualStudio.Component.Windows10SDK"
Win81SDKComponent = "Microsoft.VisualStudio.Component.Windows81SDK"
ComponentType = "Component"


class VisualStudioInstance(object):
    def __init__(self, setup_helper, setup_instance2):
        self.instance_id = setup_instance2.GetInstanceId()
        self.version = setup_instance2.GetInstallationVersion()
        self.version_number = setup_helper.ParseVersion(self.version)
        self.installation_path = setup_instance2.GetInstallationPath()
        self.name = setup_instance2.GetInstallationName()

        self.vc_toolset_installed = False
        self.win10_sdk_installed = False
        self.win81_sdk_installed = False
        for package in setup_instance2.GetPackages():
            local_id = package.GetId()
            local_type = package.GetType()

            self.vc_toolset_installed |= local_id == VCToolSetComponent and local_type == ComponentType
            self.win10_sdk_installed |= Win10SDKComponent in local_id and local_type == ComponentType
            self.win81_sdk_installed |= local_id == Win81SDKComponent and local_type == ComponentType


def enumerate_visual_studio():
    setup_config = CoCreateInstanceEx(GUID(setup_configuration_clsid), ISetupConfiguration,
                                      comtypes.CLSCTX_INPROC_SERVER)
    setup_config2 = setup_config.QueryInterface(ISetupConfiguration2)
    setup_helper = setup_config.QueryInterface(ISetupHelper)

    vs_instances = []
    enum_instances = setup_config2.EnumInstances()
    for setup_instance in enum_instances:
        if "VisualStudio/" not in setup_instance.GetInstallationName():
            continue

        setup_instance2 = setup_instance.QueryInterface(ISetupInstance2)
        state = setup_instance2.GetState()
        if (state & InstanceState.eRegistered) != InstanceState.eRegistered:
            continue

        vs_instances.append(VisualStudioInstance(setup_helper, setup_instance2))

    return vs_instances
