import * as fs from 'fs';
import * as path from 'path';
import * as project from './project';

// Without this, #include <ForgeApi.h> is a red squiggle in every mod and nothing completes. The
// C++ extension reads it; MSBuild does not, so it can never be what a build actually uses.
export function write(dir: string, sdkDir: string): string
{
    const file = path.join(dir, '.vscode', 'c_cpp_properties.json');

    const contents = {
        version: 4,
        configurations: [
            {
                name: 'AzForge mod (Win32)',
                includePath: ['${workspaceFolder}/**', path.join(sdkDir, 'include').replace(/\\/g, '/')],
                defines: ['_WINDOWS', '_USRDLL', 'UNICODE', '_UNICODE'],
                windowsSdkVersion: '10.0',
                cStandard: 'c17',
                cppStandard: 'c++20',
                intelliSenseMode: 'windows-msvc-x86'
            }
        ]
    };

    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, JSON.stringify(contents, null, 4) + '\n', 'utf8');

    return file;
}

export function refresh(mod: project.ModProject): string
{
    return write(mod.dir, project.sdkDir(mod));
}
