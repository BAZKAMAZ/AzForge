import { execFile } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';

let found: string | undefined;

function run(file: string, args: string[]): Promise<string>
{
    return new Promise(resolve =>
    {
        execFile(file, args, { windowsHide: true }, (error, stdout) => resolve(error ? '' : stdout));
    });
}

// vswhere ships with every installer since 2017 and is the only supported way to ask where the
// build tools are. It answers for Build Tools alone as well as for a full Visual Studio.
async function askVsWhere(): Promise<string | undefined>
{
    const roots = [process.env['ProgramFiles(x86)'], process.env['ProgramFiles']];

    for (const root of roots)
    {
        if (!root)
        {
            continue;
        }

        const vswhere = path.join(root, 'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
        if (!fs.existsSync(vswhere))
        {
            continue;
        }

        const output = await run(vswhere, [
            '-latest', '-prerelease', '-products', '*',
            '-requires', 'Microsoft.Component.MSBuild',
            '-find', 'MSBuild\\**\\Bin\\MSBuild.exe'
        ]);

        const line = output.split(/\r?\n/).map(one => one.trim()).find(one => one.toLowerCase().endsWith('msbuild.exe'));
        if (line && fs.existsSync(line))
        {
            return line;
        }
    }

    return undefined;
}

export async function msbuild(): Promise<string | undefined>
{
    const configured = vscode.workspace.getConfiguration('azforge').get<string>('msbuildPath') ?? '';
    if (configured)
    {
        if (!fs.existsSync(configured))
        {
            vscode.window.showErrorMessage(`azforge.msbuildPath points at '${configured}', which is not there.`);
            return undefined;
        }
        return configured;
    }

    if (found && fs.existsSync(found))
    {
        return found;
    }

    found = await askVsWhere();
    if (found)
    {
        return found;
    }

    // Last resort: an MSBuild already on PATH, which a developer command prompt puts there
    if (await run('where', ['msbuild.exe']))
    {
        found = 'msbuild.exe';
        return found;
    }

    const answer = await vscode.window.showErrorMessage(
        'MSBuild was not found. A mod is a 32-bit DLL built with MSVC, so the C++ build tools have to be installed.',
        'Set the path');

    if (answer)
    {
        vscode.commands.executeCommand('workbench.action.openSettings', 'azforge.msbuildPath');
    }

    return undefined;
}

export function forget(): void
{
    found = undefined;
}
