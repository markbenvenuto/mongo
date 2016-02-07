
import SCons.Defaults
import SCons.Util
import SCons.Tool.msvs

def EtwEmitter(target, source, env):
    fileName, ext = SCons.Util.splitext(str(target[0]))

    header = fileName + ".h"
    resource = fileName + ".rc"

    t = [header, resource]

    return (t, source)

etwpp = SCons.Builder.Builder(action = '$ETWPPCOM',
                              suffix = '.h',
                              emitter = EtwEmitter)

def generate(env):
    env['MC'] = 'mc.exe'
    env['ETWPPFLAGS'] = '-um -b'
    env["ETWPPCOM"] = '$MC $ETWPPFLAGS -h ${TARGETS[0].dir} -r ${TARGETS[0].dir} $SOURCE'

    env['BUILDERS']['EtwPreprocessor'] = etwpp

def exists(env):
    if SCons.Tool.msvs.is_msvs_installed():
        return 1
    else:
        env.Detect('mc')
